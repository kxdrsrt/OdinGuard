// OdinGuard Anti Cheat – aura unlocks (module "auras"). Players earn auras through quests and in-game milestones
// (MVP kills, battleground wins, castles held at the end of WoE, events); never with real money. The catalog comes
// from og_aura (name, hat effect, rarity, unlock condition) and only lists effects the client can draw
// (tools/check_auras.py), so an unknown or missing effect can never be set.
// Unlocks live in og_aura_unlock: per account (og_aura_scope: account, char_id 0) or per character.

#include "og_map.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <common/odinguard.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>

#include "clif.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"

static std::string og_aura_owner(map_session_data* sd) {
	bool per_char = !strcmpi(og_setting("og_aura_scope", "account"), "character");
	return "`account_id` = " + std::to_string(sd->status.account_id) + " AND `char_id` = " + std::to_string(per_char ? sd->status.char_id : 0);
}

static std::string og_aura_esc(const std::string& s) {
	std::string out(s.size() * 2 + 1, '\0');
	out.resize(Sql_EscapeStringLen(mmysql_handle, &out[0], s.c_str(), s.size()));
	return out;
}

bool og_aura_unlocked(map_session_data* sd, const char* name) {
	const s_og_aura* a = og_aura_find(name);
	if (a == nullptr)
		return false;
	std::string q = "SELECT 1 FROM `og_aura_unlock` WHERE " + og_aura_owner(sd) + " AND `aura` = '" + og_aura_esc(a->name) + "'";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return false; }
	bool found = Sql_NumRows(mmysql_handle) > 0;
	Sql_FreeResult(mmysql_handle);
	return found;
}

int32 og_aura_unlock(map_session_data* sd, const char* name, const char* source) {
	const s_og_aura* a = og_aura_find(name);
	if (a == nullptr || !og_module_on("og_auras_enable", true))
		return -1;
	if (og_aura_unlocked(sd, a->name.c_str()))
		return 0;
	bool per_char = !strcmpi(og_setting("og_aura_scope", "account"), "character");
	std::string q = "INSERT IGNORE INTO `og_aura_unlock` (`account_id`,`char_id`,`aura`,`source`) VALUES (" +
		std::to_string(sd->status.account_id) + "," + std::to_string(per_char ? sd->status.char_id : 0) + ",'" +
		og_aura_esc(a->name) + "','" + og_aura_esc(std::string(source ? source : "").substr(0, 64)) + "')";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return -1; }
	char msg[CHAT_SIZE_MAX];
	safesnprintf(msg, sizeof(msg), "New aura unlocked: %s (%s). Wear it with @aura %s.", a->name.c_str(), a->rarity.c_str(), a->name.c_str());
	clif_displaymessage(sd->fd, msg);
	return 1;
}

void og_aura_unlocked_list(map_session_data* sd, std::vector<std::string>& out) {
	std::string q = "SELECT `aura` FROM `og_aura_unlock` WHERE " + og_aura_owner(sd) + " ORDER BY `time`";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return; }
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char* d;
		Sql_GetData(mmysql_handle, 0, &d, nullptr);
		if (d != nullptr && og_aura_find(d) != nullptr) // removed from the catalog = not wearable any more
			out.emplace_back(d);
	}
	Sql_FreeResult(mmysql_handle);
}

/// GMs (group level of @auraunlock) may wear and give every aura of the catalog
bool og_aura_gm(map_session_data* sd) {
	return pc_get_group_level(sd) >= og_setting_int("og_cmdgroup_auraunlock", 60);
}

/// @aura for everyone: list / wear / off; GMs also "<aura>, <character>"
void og_aura_command(map_session_data* sd, const char* aura, map_session_data* target) {
	int32 fd = sd->fd;
	if (!*aura || !strcmpi(aura, "list")) {
		std::vector<std::string> mine;
		og_aura_unlocked_list(sd, mine);
		std::string line = mine.empty() ? "You have not unlocked an aura yet." : "Your auras:";
		for (const std::string& n : mine) line += " " + n;
		clif_displaymessage(fd, line.c_str());
		clif_displaymessage(fd, "@aura <name> wears one, @aura off removes it. How to unlock the others:");
		std::string row;
		for (const s_og_aura& a : og_aura_catalog()) {
			if (std::find(mine.begin(), mine.end(), a.name) != mine.end()) continue;
			std::string e = a.name + " (" + a.rarity + ": " + a.unlock + ")";
			if (row.size() + e.size() > 110) { clif_displaymessage(fd, row.c_str()); row.clear(); }
			row += (row.empty() ? "" : ", ") + e;
		}
		if (!row.empty()) clif_displaymessage(fd, row.c_str());
		return;
	}
	if (target != sd && !og_aura_gm(sd)) {
		clif_displaymessage(fd, "You can only change your own aura.");
		return;
	}
	if (!strcmpi(aura, "off")) {
		og_map_set_aura(target, 0);
		clif_displaymessage(fd, "Aura removed.");
		return;
	}
	const s_og_aura* a = og_aura_find(aura);
	if (a == nullptr) {
		clif_displaymessage(fd, "Unknown aura (see @aura).");
		return;
	}
	if (!og_aura_unlocked(target, a->name.c_str())) {
		if (!og_aura_gm(sd)) {
			std::string m = "You have not unlocked " + a->name + " yet (" + a->unlock + ").";
			clif_displaymessage(fd, m.c_str());
			return;
		}
		og_aura_unlock(target, a->name.c_str(), "gm");
	}
	og_map_set_aura(target, a->effect);
	clif_displaymessage(fd, "Aura set.");
}

/// an aura stored on the character stays only while it is still unlocked and in the catalog
bool og_aura_valid_on_load(map_session_data* sd, int32 effect) {
	for (const s_og_aura& a : og_aura_catalog())
		if (a.effect == effect)
			return og_aura_unlocked(sd, a.name.c_str());
	return false;
}

/// milestone counter reached: unlocks every catalog aura "<type>:<n>" with n <= count; @return new unlocks
int32 og_aura_check(map_session_data* sd, const char* type, int32 count) {
	int32 n = 0;
	size_t tl = strlen(type);
	for (const s_og_aura& a : og_aura_catalog()) {
		if (a.unlock.size() <= tl || strncmpi(a.unlock.c_str(), type, tl) != 0)
			continue;
		bool hit = a.unlock[tl] == ':' && (count < 0 ? strcmpi(a.unlock.c_str() + tl + 1, "") != 0 : atoi(a.unlock.c_str() + tl + 1) <= count);
		if (hit && og_aura_unlock(sd, a.name.c_str(), a.unlock.c_str()) > 0)
			n++;
	}
	return n;
}

/// names of the catalog auras with exactly this unlock condition that the player does not have yet, ':' separated
std::string og_aura_list(map_session_data* sd, const char* unlock) {
	std::string out;
	for (const s_og_aura& a : og_aura_catalog())
		if (!strcmpi(a.unlock.c_str(), unlock) && (sd == nullptr || !og_aura_unlocked(sd, a.name.c_str())))
			out += (out.empty() ? "" : ":") + a.name;
	return out;
}
