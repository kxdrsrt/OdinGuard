// OdinGuard checks of the login-server. See og_login.hpp.

#include "og_login.hpp"

#include <string>

#include <common/odinguard.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>

// same database as the ip ban list (ipban.cpp)
extern std::string ipban_db_hostname;
extern uint16 ipban_db_port;
extern std::string ipban_db_username;
extern std::string ipban_db_password;
extern std::string ipban_db_database;

static Sql* og_sql = nullptr;

// components that identify one machine; the others (cpu, board model, ...) are shared by many players
static const char* og_unique_components[] = { "machine_guid", "volume_serial", "disk_serial", "mac", "smbios_uuid" };

void og_login_init() {
	if (!og_enabled())
		return;
	og_sql = Sql_Malloc();
	if (SQL_ERROR == Sql_Connect(og_sql, ipban_db_username.c_str(), ipban_db_password.c_str(), ipban_db_hostname.c_str(), ipban_db_port, ipban_db_database.c_str())) {
		Sql_ShowDebug(og_sql);
		Sql_Free(og_sql);
		og_sql = nullptr;
		ShowError("OdinGuard: no database connection, hardware bans are not checked.\n");
		return;
	}
	for (const char* q : og_sql_ddl())
		if (SQL_ERROR == Sql_QueryStr(og_sql, q))
			Sql_ShowDebug(og_sql);
}

void og_login_final() {
	if (og_sql != nullptr) {
		Sql_Free(og_sql);
		og_sql = nullptr;
	}
}

static std::string og_escape(const std::string& s) {
	std::string out(s.size() * 2 + 1, '\0');
	out.resize(Sql_EscapeStringLen(og_sql, &out[0], s.c_str(), s.size()));
	return out;
}

static bool og_count(const std::string& query) {
	if (SQL_ERROR == Sql_QueryStr(og_sql, query.c_str())) {
		Sql_ShowDebug(og_sql);
		return false;
	}
	bool found = Sql_NumRows(og_sql) > 0 && SQL_SUCCESS == Sql_NextRow(og_sql);
	Sql_FreeResult(og_sql);
	return found;
}

static void og_log_event(const s_og_info* info, uint32 account_id, const char* type, const std::string& detail, const char* action) {
	std::string q = "INSERT INTO `og_event` (`unique_id`,`account_id`,`type`,`detail`,`action`) VALUES ('" + info->unique_id + "'," +
		std::to_string(account_id) + ",'" + og_escape(type) + "','" + og_escape(detail.substr(0, 255)) + "','" + action + "')";
	if (SQL_ERROR == Sql_QueryStr(og_sql, q.c_str()))
		Sql_ShowDebug(og_sql);
}

int32 og_login_check(int32 fd, uint32 account_id, const char* userid) {
	if (!og_enabled())
		return 0;
	const s_og_info* info = og_info(fd);
	if (info == nullptr) {
		if (og_required()) {
			ShowStatus("OdinGuard: login of '%s' refused, client without OdinGuard.\n", userid);
			return 5;
		}
		return 0;
	}
	if (og_sql == nullptr)
		return 0;

	const std::string& uid = info->unique_id; // hex from the socket layer, safe for SQL
	const char* active = "(b.`expires` IS NULL OR b.`expires` > NOW())";
	bool banned = og_count("SELECT 1 FROM `og_ban` b WHERE b.`unique_id` = '" + uid + "' AND " + active + " LIMIT 1");
	std::string linked;

	if (!banned) {
		// fuzzy match: enough identifying components equal to those of a banned machine
		std::string cond;
		for (const auto& c : info->components) {
			for (const char* name : og_unique_components) {
				if (c.first != name)
					continue;
				if (!cond.empty())
					cond += " OR ";
				cond += "(c.`name` = '" + og_escape(c.first) + "' AND c.`hash` = '" + og_escape(c.second) + "')";
			}
		}
		if (!cond.empty()) {
			std::string q = "SELECT c.`unique_id` FROM `og_component` c JOIN `og_ban` b ON b.`unique_id` = c.`unique_id` AND " + std::string(active) +
				" WHERE c.`unique_id` <> '" + uid + "' AND (" + cond + ") GROUP BY c.`unique_id` HAVING COUNT(DISTINCT c.`name`) >= " +
				std::to_string(og_ban_match()) + " LIMIT 1";
			if (SQL_ERROR == Sql_QueryStr(og_sql, q.c_str()))
				Sql_ShowDebug(og_sql);
			else {
				if (SQL_SUCCESS == Sql_NextRow(og_sql)) {
					char* data;
					Sql_GetData(og_sql, 0, &data, nullptr);
					linked = data;
					banned = true;
				}
				Sql_FreeResult(og_sql);
			}
		}
	}

	// registry of machines and their components (needed for bans and fuzzy matching)
	char ip[16];
	ip2str(session[fd]->client_addr, ip);
	std::string q = "INSERT INTO `og_client` (`unique_id`,`account_id`,`last_ip`,`build`,`exe_code`) VALUES ('" + uid + "'," + std::to_string(account_id) +
		",'" + ip + "','" + og_escape(info->build) + "','" + info->exe_code + "') ON DUPLICATE KEY UPDATE `last_seen` = NOW(), `last_ip` = VALUES(`last_ip`),"
		" `build` = VALUES(`build`), `exe_code` = VALUES(`exe_code`)";
	if (SQL_ERROR == Sql_QueryStr(og_sql, q.c_str()))
		Sql_ShowDebug(og_sql);
	for (const auto& c : info->components) {
		q = "REPLACE INTO `og_component` (`unique_id`,`name`,`hash`) VALUES ('" + uid + "','" + og_escape(c.first) + "','" + og_escape(c.second) + "')";
		if (SQL_ERROR == Sql_QueryStr(og_sql, q.c_str()))
			Sql_ShowDebug(og_sql);
	}

	if (!banned)
		return 0;
	if (!linked.empty()) {
		// the new id of a banned machine is banned as well
		q = "INSERT INTO `og_ban` (`unique_id`,`reason`,`banned_by`) VALUES ('" + uid + "','linked to " + og_escape(linked) + "','OdinGuard')";
		if (SQL_ERROR == Sql_QueryStr(og_sql, q.c_str()))
			Sql_ShowDebug(og_sql);
	}
	og_log_event(info, account_id, "login-banned", linked.empty() ? uid : "linked to " + linked, "refuse");
	ShowStatus("OdinGuard: login of '%s' refused, machine %.16s... is banned.\n", userid, uid.c_str());
	return 4;
}
