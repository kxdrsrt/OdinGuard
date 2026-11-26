// OdinGuard Anti Cheat on the map-server: server-side protection modules that work for every client (also
// without the guard): flags and delayed ban waves (ban-system), packet rate limit (rate-limit), behaviour
// signatures (behaviour: no-delay attempts, autopot reaction time, macro timing), the honeypot unit
// (ch-honeypot), the bot captcha (ch-captcha) and the command registry (enable, rate limit, rename).
// These checks are the real security of OdinGuard: they do not trust anything the client says.

#include "og_map.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/odinguard.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"

#define OG_HONEYPOT_BASE 0x7A000000

struct s_og_bh {
	t_tick win_start = 0;
	int32 packets = 0, over_seconds = 0;
	bool rate_reported = false;
	t_tick nd_start = 0;
	int32 nd_count = 0;
	bool nd_flagged = false;
	bool hp_low = false;
	t_tick hp_low_at = 0;
	std::vector<int32> reactions;
	bool ap_flagged = false;
	t_tick last_action = 0;
	std::vector<int32> intervals;
	bool macro_flagged = false;
	bool honeypot_flagged = false;
	bool captcha_sent = false;
	std::unordered_map<std::string, t_tick> cmd_last;
};

static std::unordered_map<int32, s_og_bh> og_bh; // by account id

static s_og_bh& og_state_of(const map_session_data* sd) { return og_bh[sd->status.account_id]; }

void og_map_guard_quit(map_session_data* sd) { og_bh.erase(sd->status.account_id); }

static std::string og_esc(const std::string& s) {
	std::string out(s.size() * 2 + 1, '\0');
	out.resize(Sql_EscapeStringLen(mmysql_handle, &out[0], s.c_str(), s.size()));
	return out;
}

// ---------------------------------------------------------------- flags and ban waves

void og_map_flag(map_session_data* sd, const char* uid, const char* rule, int32 score, bool ban, const char* detail) {
	// a flag raised in warn mode never counts for a later ban wave: the rules were not trusted yet
	if (og_log_only())
		ban = false;
	std::string q = "INSERT INTO `og_flag` (`unique_id`,`account_id`,`char_name`,`rule`,`score`,`ban`,`detail`) VALUES ('" +
		og_esc(uid ? uid : "") + "'," + std::to_string(sd ? sd->status.account_id : 0) + ",'" + og_esc(sd ? sd->status.name : "") + "','" +
		og_esc(std::string(rule).substr(0, 64)) + "'," + std::to_string(score) + "," + (ban ? "1" : "0") + ",'" +
		og_esc(std::string(detail).substr(0, 255)) + "')";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str()))
		Sql_ShowDebug(mmysql_handle);
}

/// bans every account whose open ban flags reach og_ban_score; @return number of accounts (dry: only listed)
int32 og_map_banwave(bool dry, std::vector<std::string>* report) {
	int32 limit = og_setting_int("og_ban_score", 100), n = 0;
	if (og_log_only() && !dry) {
		dry = true; // warn mode never bans
		if (report != nullptr) report->push_back("warn mode (og_log_only): only listed, nobody banned");
	}
	std::string q = "SELECT `account_id`, MAX(`unique_id`), SUM(`score`), GROUP_CONCAT(DISTINCT `rule` SEPARATOR ',') FROM `og_flag`"
		" WHERE `state` = 'open' AND `ban` = 1 GROUP BY `account_id` HAVING SUM(`score`) >= " + std::to_string(limit);
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return 0; }
	std::vector<std::tuple<uint32, std::string, int32, std::string>> hits;
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char *acc, *uid, *score, *rules;
		Sql_GetData(mmysql_handle, 0, &acc, nullptr);
		Sql_GetData(mmysql_handle, 1, &uid, nullptr);
		Sql_GetData(mmysql_handle, 2, &score, nullptr);
		Sql_GetData(mmysql_handle, 3, &rules, nullptr);
		hits.emplace_back((uint32)strtoul(acc, nullptr, 10), uid ? uid : "", atoi(score), rules ? rules : "");
	}
	Sql_FreeResult(mmysql_handle);
	for (const auto& h : hits) {
		uint32 acc = std::get<0>(h);
		const std::string& uid = std::get<1>(h);
		if (report != nullptr)
			report->push_back("account " + std::to_string(acc) + ": score " + std::to_string(std::get<2>(h)) + " (" + std::get<3>(h).substr(0, 80) + ")");
		n++;
		if (dry)
			continue;
		if (!uid.empty()) {
			std::string b = "INSERT INTO `og_ban` (`unique_id`,`reason`,`banned_by`) VALUES ('" + og_esc(uid) + "','ban wave: " + og_esc(std::get<3>(h).substr(0, 200)) + "','OdinGuard')";
			if (SQL_ERROR == Sql_QueryStr(mmysql_handle, b.c_str())) Sql_ShowDebug(mmysql_handle);
		}
		std::string u = "UPDATE `og_flag` SET `state` = 'banned' WHERE `state` = 'open' AND `account_id` = " + std::to_string(acc);
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, u.c_str())) Sql_ShowDebug(mmysql_handle);
		if (map_session_data* sd = map_id2sd(acc); sd != nullptr)
			clif_authfail_fd(sd->fd, 15);
	}
	if (!dry && n > 0)
		ShowStatus("OdinGuard: ban wave banned %d account(s).\n", n);
	return n;
}

static TIMER_FUNC(og_banwave_timer) {
	static int32 last_day = -1;
	int32 hour = og_setting_int("og_banwave_hour", 4);
	if (hour < 0 || !og_module_on("og_ban_system", true) || og_log_only())
		return 0;
	time_t t = time(nullptr);
	struct tm* lt = localtime(&t);
	if (lt->tm_hour == hour && lt->tm_yday != last_day) {
		last_day = lt->tm_yday;
		og_map_banwave(false, nullptr);
	}
	return 0;
}

void og_map_flags_text(uint32 account_id, std::vector<std::string>& out) {
	std::string q = account_id
		? "SELECT `time`,`rule`,`score`,`ban`,`state`,`detail` FROM `og_flag` WHERE `account_id` = " + std::to_string(account_id) + " ORDER BY `id` DESC LIMIT 15"
		: "SELECT MAX(`time`), CONCAT('account ', `account_id`), SUM(`score`), MAX(`ban`), 'open', GROUP_CONCAT(DISTINCT `rule` SEPARATOR ',') FROM `og_flag` WHERE `state` = 'open' GROUP BY `account_id` ORDER BY SUM(`score`) DESC LIMIT 10";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return; }
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char* d[6];
		for (int32 i = 0; i < 6; i++) Sql_GetData(mmysql_handle, i, &d[i], nullptr);
		out.push_back(std::string(d[0] ? d[0] : "") + " " + (d[1] ? d[1] : "") + " score " + (d[2] ? d[2] : "0") +
			(d[3] && atoi(d[3]) ? " ban" : "") + " [" + (d[4] ? d[4] : "") + "] " + std::string(d[5] ? d[5] : "").substr(0, 60));
	}
	Sql_FreeResult(mmysql_handle);
}

/// sets the open flags of an account (optionally of one rule) to 'cleared'; @return number of flags
int32 og_map_flags_clear(uint32 account_id, const char* rule) {
	std::string q = "UPDATE `og_flag` SET `state` = 'cleared' WHERE `state` = 'open' AND `account_id` = " + std::to_string(account_id);
	if (rule != nullptr && *rule)
		q += " AND `rule` = '" + og_esc(std::string(rule).substr(0, 64)) + "'";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return 0; }
	return (int32)Sql_NumRowsAffected(mmysql_handle);
}

// ---------------------------------------------------------------- rate limit

void og_map_packet(int32 fd, map_session_data* sd, uint16 cmd) {
	(void)cmd;
	if (sd == nullptr || !og_module_on("og_rate_limit", false))
		return;
	s_og_bh& b = og_state_of(sd);
	t_tick now = gettick();
	if (DIFF_TICK(now, b.win_start) >= 1000) {
		if (b.packets > og_setting_int("og_rate_pps", 150)) b.over_seconds++;
		else b.over_seconds = 0;
		b.win_start = now;
		b.packets = 0;
	}
	b.packets++;
	if (b.over_seconds >= 3 && !b.rate_reported) {
		b.rate_reported = true;
		char detail[96];
		safesnprintf(detail, sizeof(detail), "more than %d packets/s for 3 s", og_setting_int("og_rate_pps", 150));
		og_report(fd, "rate-limit", detail, og_setting_action("og_rate_action", OG_ACTION_KICK), 30);
	}
}

// ---------------------------------------------------------------- behaviour

static void og_bh_flag(map_session_data* sd, const char* rule, const char* detail, int32 score) {
	og_report(sd->fd, rule, detail, og_setting_action("og_bh_action", OG_ACTION_FLAGBAN), score);
	// a captcha only outside of WoE/BG/PvP fights, once per session
	s_og_bh& b = og_state_of(sd);
	if (og_module_on("og_ch_captcha", false) && !b.captcha_sent && sd->state.active) {
		struct map_data* m = map_getmapdata(sd->m);
		if (m != nullptr && !map_flag_gvg2(sd->m) && !m->getMapFlag(MF_BATTLEGROUND) && !m->getMapFlag(MF_PVP)) {
			b.captcha_sent = true;
			pc_macro_reporter_process(*sd, 0);
		}
	}
}

void og_map_skill_refused(map_session_data* sd) {
	if (!og_module_on("og_behaviour", false))
		return;
	s_og_bh& b = og_state_of(sd);
	t_tick now = gettick();
	if (DIFF_TICK(now, b.nd_start) > 60000) { b.nd_start = now; b.nd_count = 0; }
	if (++b.nd_count > og_setting_int("og_bh_nodelay", 120) && !b.nd_flagged) {
		b.nd_flagged = true;
		char detail[96];
		safesnprintf(detail, sizeof(detail), "%d skill requests inside the delay within 60 s (modified client or macro)", b.nd_count);
		og_bh_flag(sd, "bh:nodelay", detail, 40);
	}
}

static void og_bh_action(map_session_data* sd, s_og_bh& b) {
	t_tick now = gettick();
	if (b.last_action != 0) {
		int32 iv = (int32)DIFF_TICK(now, b.last_action);
		if (iv > 30000) b.intervals.clear();
		else b.intervals.push_back(iv);
	}
	b.last_action = now;
	if (b.intervals.size() > 40) b.intervals.erase(b.intervals.begin());
	if (b.intervals.size() == 40 && !b.macro_flagged) {
		double mean = 0, var = 0;
		for (int32 v : b.intervals) mean += v;
		mean /= 40;
		for (int32 v : b.intervals) var += (v - mean) * (v - mean);
		double sd_ms = std::sqrt(var / 40);
		if (mean >= 40 && sd_ms < og_setting_int("og_bh_macro_jitter", 4)) {
			b.macro_flagged = true;
			char detail[96];
			safesnprintf(detail, sizeof(detail), "40 actions every %.0f ms with %.1f ms jitter", mean, sd_ms);
			og_bh_flag(sd, "bh:macro", detail, 30);
		}
	}
}

void og_map_item_used(map_session_data* sd, int32 index) {
	if (!og_module_on("og_behaviour", false) || index < 0 || index >= MAX_INVENTORY || sd->inventory_data[index] == nullptr)
		return;
	s_og_bh& b = og_state_of(sd);
	og_bh_action(sd, b);
	if (sd->inventory_data[index]->type != IT_HEALING || !b.hp_low)
		return;
	int32 reaction = (int32)DIFF_TICK(gettick(), b.hp_low_at);
	b.hp_low = false;
	if (reaction > 5000)
		return;
	b.reactions.push_back(reaction);
	if (b.reactions.size() > 20) b.reactions.erase(b.reactions.begin());
	if (b.reactions.size() == 20 && !b.ap_flagged) {
		std::vector<int32> s = b.reactions;
		std::sort(s.begin(), s.end());
		int32 median = s[10];
		if (median < og_setting_int("og_bh_autopot_ms", 90) && s[17] < og_setting_int("og_bh_autopot_ms", 90) * 2) {
			b.ap_flagged = true;
			char detail[96];
			safesnprintf(detail, sizeof(detail), "healing item %d ms after HP < 50%% (median of 20, human ~250 ms)", median);
			og_bh_flag(sd, "bh:autopot", detail, 40);
		}
	}
}

void og_map_skill_used(map_session_data* sd) {
	if (og_module_on("og_behaviour", false))
		og_bh_action(sd, og_state_of(sd));
}

void og_map_hp_update(map_session_data* sd) {
	if (!og_module_on("og_behaviour", false) || sd->battle_status.max_hp == 0)
		return;
	s_og_bh& b = og_state_of(sd);
	bool low = sd->battle_status.hp * 2 < sd->battle_status.max_hp && sd->battle_status.hp > 0;
	if (low && !b.hp_low) { b.hp_low = true; b.hp_low_at = gettick(); }
	else if (!low) b.hp_low = false;
}

// ---------------------------------------------------------------- honeypot

static uint32 og_honeypot_id(const map_session_data* sd) { return OG_HONEYPOT_BASE | (sd->status.account_id & 0xFFFFFF); }

void og_map_honeypot_spawn(map_session_data* sd) {
	// an invisible monster at the player's position: the game client never draws or targets it, bots that read
	// the unit list do
	if (og_module_on("og_ch_honeypot", false) && pc_get_group_level(sd) == 0)
		clif_og_honeypot(*sd, og_honeypot_id(sd));
}

bool og_map_honeypot_hit(map_session_data* sd, int32 target_id) {
	if (target_id != (int32)og_honeypot_id(sd) || !og_module_on("og_ch_honeypot", false))
		return false;
	s_og_bh& b = og_state_of(sd);
	if (!b.honeypot_flagged) {
		b.honeypot_flagged = true;
		og_report(sd->fd, "challenge:honeypot", "attacked an invisible decoy unit (bot)", og_setting_action("og_ch_honeypot_action", OG_ACTION_FLAGBAN), 80);
	}
	return true;
}

// ---------------------------------------------------------------- command registry

/// og_cmd_<id>: on/off, og_cmdrate_<id>: ms, og_cmdgroup_<id>: lowest group level (conf/odinguard_commands.conf)
bool og_map_command_allowed(map_session_data* sd, const char* id, int32 def_rate_ms) {
	char key[64];
	safesnprintf(key, sizeof(key), "og_cmd_%s", id);
	if (!config_switch(og_setting(key, "yes"))) {
		clif_displaymessage(sd->fd, og_text(sd->fd, "cmd_disabled", "This command is disabled on this server.").c_str());
		return false;
	}
	safesnprintf(key, sizeof(key), "og_cmdgroup_%s", id);
	if (pc_get_group_level(sd) < og_setting_int(key, 0)) {
		clif_displaymessage(sd->fd, og_text(sd->fd, "cmd_denied", "You are not allowed to use this command.").c_str());
		return false;
	}
	safesnprintf(key, sizeof(key), "og_cmdrate_%s", id);
	int32 rate = og_setting_int(key, def_rate_ms);
	if (rate > 0 && pc_get_group_level(sd) == 0) {
		s_og_bh& b = og_state_of(sd);
		t_tick now = gettick();
		auto it = b.cmd_last.find(id);
		if (it != b.cmd_last.end() && DIFF_TICK(now, it->second) < rate) {
			clif_displaymessage(sd->fd, og_text(sd->fd, "cmd_wait", "Please wait a moment before using this command again.").c_str());
			return false;
		}
		b.cmd_last[id] = now;
	}
	return true;
}

/// client command name after renaming (og_cmdname_<id>), e.g. "ping"
const char* og_map_command_name(const char* id) {
	char key[64];
	safesnprintf(key, sizeof(key), "og_cmdname_%s", id);
	return og_setting(key, id);
}

void og_map_guard_init() {
	add_timer_func_list(og_banwave_timer, "og_banwave_timer");
	add_timer_interval(gettick() + 60000, og_banwave_timer, 0, 0, 60000);
}
