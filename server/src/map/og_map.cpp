// OdinGuard on the map-server. See og_map.hpp.

#include "og_map.hpp"

#include <algorithm>
#include <string>

#include <common/odinguard.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/utilities.hpp>

#include <common/og_proto.h>

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"
#include "script.hpp"
#include "unit.hpp"

// character registry keys
#define OG_REG_FLAGS "og_flags"         // bit 0: hide auras of others, bit 1: lite ground skills
#define OG_REG_AURA "og_aura"           // hat effect id of the permanent aura
#define OG_REG_NAMECOLOR "og_namecolor" // 0x1RRGGBB (bit 24 marks "set")
#define OG_FLAG_HIDE_AURAS 1
#define OG_FLAG_LGP 2

static std::string og_esc(const std::string& s) {
	std::string out(s.size() * 2 + 1, '\0');
	out.resize(Sql_EscapeStringLen(mmysql_handle, &out[0], s.c_str(), s.size()));
	return out;
}

static map_session_data* og_sd(int32 fd) {
	if (!session_isValid(fd) || session[fd]->session_data == nullptr)
		return nullptr;
	map_session_data* sd = static_cast<map_session_data*>(session[fd]->session_data);
	return sd->state.active ? sd : nullptr;
}

static void og_insert_ban(const std::string& uid, const char* by, const char* reason) {
	std::string q = "INSERT INTO `og_ban` (`unique_id`,`reason`,`banned_by`) VALUES ('" + uid + "','" + og_esc(reason) + "','" + og_esc(by) + "')";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str()))
		Sql_ShowDebug(mmysql_handle);
}

static void og_map_event(int32 fd, const char* type, const char* detail, e_og_action action) {
	const s_og_info* info = og_info(fd);
	map_session_data* sd = og_sd(fd);
	static const char* names[] = { "log", "kick", "ban" };

	std::string q = "INSERT INTO `og_event` (`unique_id`,`account_id`,`char_name`,`type`,`detail`,`action`) VALUES ('" +
		(info ? info->unique_id : std::string()) + "'," + std::to_string(sd ? sd->status.account_id : 0) + ",'" +
		og_esc(sd ? sd->status.name : "") + "','" + og_esc(std::string(type).substr(0, 64)) + "','" +
		og_esc(std::string(detail).substr(0, 255)) + "','" + names[action] + "')";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str()))
		Sql_ShowDebug(mmysql_handle);

	if (action == OG_ACTION_BAN && info != nullptr)
		og_insert_ban(info->unique_id, "OdinGuard", type);
	if (action != OG_ACTION_LOG) {
		if (sd != nullptr)
			clif_authfail_fd(fd, 15); // "You have been forced to disconnect by the GM Team"
		else
			set_eof(fd);
	}
}

static void og_map_text(int32 fd, const char* text) {
	if (og_sd(fd) != nullptr)
		clif_displaymessage(fd, text);
}

static void og_map_pong(int32 fd, int64 rtt) {
	if (og_sd(fd) == nullptr)
		return;
	char msg[64];
	safesnprintf(msg, sizeof(msg), "Ping: %d ms", (int32)rtt);
	clif_displaymessage(fd, msg);
}

void og_map_init() {
	if (!og_enabled())
		return;
	for (const char* q : og_sql_ddl())
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q))
			Sql_ShowDebug(mmysql_handle);
	og_set_handlers(og_map_event, og_map_text, og_map_pong);
}

bool og_map_chat_command(map_session_data* sd, const char* message) {
	// newer clients put a colour code in front of the text ("|00")
	if (message[0] == '|' && ISXDIGIT(message[1]) && ISXDIGIT(message[2]))
		message += 3;
	if (message[0] != '!')
		return false;
	char cmd[32];
	safestrncpy(cmd, message + 1, sizeof(cmd));
	char* arg = strchr(cmd, ' ');
	if (arg != nullptr)
		*arg++ = '\0';
	bool known = !strcmpi(cmd, "ping") || !strcmpi(cmd, "vsync") || !strcmpi(cmd, "crash");
	if (!known)
		return false;
	if (!og_session_active(sd->fd)) {
		clif_displaymessage(sd->fd, "OdinGuard is not active on this client.");
		return true;
	}
	if (!strcmpi(cmd, "ping"))
		og_send_ping(sd->fd);
	else if (!strcmpi(cmd, "crash"))
		og_send_command(sd->fd, "crash");
	else if (arg != nullptr && (!strcmpi(arg, "on") || !strcmpi(arg, "off")))
		og_send_command(sd->fd, !strcmpi(arg, "on") ? "vsync on" : "vsync off");
	else
		og_send_command(sd->fd, "vsync");
	return true;
}

bool og_map_machine_limit(map_session_data* sd) {
	int32 limit = og_max_per_machine();
	const s_og_info* info = og_info(sd->fd);
	if (limit <= 0 || info == nullptr || pc_get_group_level(sd) > 0)
		return true;
	struct s_count { const std::string* uid; uint32 account_id; int32 count; } c = { &info->unique_id, sd->status.account_id, 0 };
	map_foreachpc([](map_session_data* other, va_list ap) -> int32 {
		auto* c = va_arg(ap, s_count*);
		if (other->fd <= 0 || other->state.autotrade || !other->state.active || other->status.account_id == c->account_id)
			return 0;
		const s_og_info* oi = og_info(other->fd);
		if (oi != nullptr && oi->unique_id == *c->uid)
			c->count++;
		return 0;
	}, &c);
	if (c.count < limit)
		return true;
	ShowWarning("OdinGuard: %s (account %u) rejected, %d characters already online from the same machine (og_max_per_machine %d).\n",
		sd->status.name, sd->status.account_id, c.count, limit);
	return false;
}

bool og_map_ban(map_session_data* target, const char* by, const char* reason) {
	const s_og_info* info = og_info(target->fd);
	if (info == nullptr)
		return false;
	og_insert_ban(info->unique_id, by, reason);
	og_map_event(target->fd, "gm-ban", reason, OG_ACTION_LOG);
	clif_authfail_fd(target->fd, 15);
	return true;
}

int32 og_map_unban_account(uint32 account_id) {
	std::string q = "DELETE FROM `og_ban` WHERE `unique_id` IN (SELECT `unique_id` FROM `og_client` WHERE `account_id` = " + std::to_string(account_id) + ")";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) {
		Sql_ShowDebug(mmysql_handle);
		return 0;
	}
	return (int32)Sql_NumRowsAffected(mmysql_handle);
}

void og_map_info(map_session_data* target, char* out, size_t size) {
	const s_og_info* info = og_info(target->fd);
	if (info == nullptr) {
		safesnprintf(out, size, "%s: no OdinGuard session.", target->status.name);
		return;
	}
	safesnprintf(out, size, "%s: OdinGuard v%d, build '%s', machine %.16s, exe %.16s, flags %d", target->status.name,
		info->version, info->build.c_str(), info->unique_id.c_str(), info->exe_code.c_str(), info->flags);
}

// ---------------------------------------------------------------- display features

static int32 og_flags(const map_session_data* sd) {
	return (int32)pc_readglobalreg(const_cast<map_session_data*>(sd), add_str(OG_REG_FLAGS));
}

bool og_map_hides_auras(const map_session_data* viewer, const block_list& owner) {
	return viewer != nullptr && viewer->id != owner.id && (og_flags(viewer) & OG_FLAG_HIDE_AURAS);
}

uint16 og_map_lgp_unit(const map_session_data* viewer, uint16 skill_id) {
	if (viewer == nullptr || !(og_flags(viewer) & OG_FLAG_LGP))
		return 0;
	return og_lgp_unit(skill_id);
}

struct s_og_area { const block_list* src; const void* buf; int32 len; bool (*keep)(map_session_data*, const block_list&, void*); void* ctx; };

void og_map_send_area(const block_list& src, const void* buf, int32 len, bool (*keep)(map_session_data* viewer, const block_list& src, void* ctx), void* ctx) {
	s_og_area a = { &src, buf, len, keep, ctx };
	map_foreachinallrange([](block_list* bl, va_list ap) -> int32 {
		s_og_area* a = va_arg(ap, s_og_area*);
		map_session_data* sd = reinterpret_cast<map_session_data*>(bl);
		if (sd->fd <= 0 || !session_isActive(sd->fd) || (a->keep != nullptr && !a->keep(sd, *a->src, a->ctx)))
			return 0;
		clif_send(a->buf, a->len, bl, SELF);
		return 1;
	}, const_cast<block_list*>(&src), AREA_SIZE, BL_PC, &a);
}

void og_map_toggle_auras(map_session_data* sd) {
	int32 flags = og_flags(sd) ^ OG_FLAG_HIDE_AURAS;
	pc_setglobalreg(sd, add_str(OG_REG_FLAGS), flags);
	clif_displaymessage(sd->fd, (flags & OG_FLAG_HIDE_AURAS) ? "Auras of other players are hidden (they disappear after a map change or @refresh)." : "Auras of other players are shown again (after a map change or @refresh).");
}

void og_map_toggle_lgp(map_session_data* sd) {
	int32 flags = og_flags(sd) ^ OG_FLAG_LGP;
	pc_setglobalreg(sd, add_str(OG_REG_FLAGS), flags);
	clif_displaymessage(sd->fd, (flags & OG_FLAG_LGP) ? "Lite ground skills on: Storm Gust, Meteor Storm and Lord of Vermilion are shown as coloured cells." : "Lite ground skills off: normal skill effects.");
}

int32 og_map_aura_by_name(const char* name) {
	// only auras from the og_aura list: hat effects without client resources show an error box and can crash the client
	int32 id = atoi(name);
	for (const auto& a : og_auras())
		if (!strcmpi(a.first.c_str(), name) || (id > 0 && a.second == id))
			return a.second;
	return -1;
}

static bool og_aura_allowed(int32 id) {
	for (const auto& a : og_auras())
		if (a.second == id)
			return true;
	return false;
}

static void og_apply_aura(map_session_data* sd, int32 old_id, int32 new_id, bool notify) {
	unit_data* ud = unit_bl2ud(sd);
	if (ud == nullptr)
		return;
	if (old_id > 0 && rathena::util::vector_erase_if_exists(ud->hatEffects, static_cast<int16>(old_id)) && notify)
		clif_hat_effect_single(*sd, (uint16)old_id, false);
	if (new_id > 0 && !rathena::util::vector_exists(ud->hatEffects, static_cast<int16>(new_id))) {
		ud->hatEffects.push_back(static_cast<int16>(new_id));
		if (notify)
			clif_hat_effect_single(*sd, (uint16)new_id, true);
	}
}

bool og_map_set_aura(map_session_data* sd, int32 aura) {
	if (aura != 0 && !og_aura_allowed(aura))
		return false;
	int32 old_id = (int32)pc_readglobalreg(sd, add_str(OG_REG_AURA));
	pc_setglobalreg(sd, add_str(OG_REG_AURA), aura);
	og_apply_aura(sd, old_id, aura, true);
	return true;
}

typedef std::vector<std::pair<uint32, std::string>> og_color_list;

// colour message: kind, op, entries (colour, text)
static std::vector<uint8> og_colors_msg(uint8 kind, uint8 op, const std::vector<std::pair<uint32, std::string>>& entries) {
	std::vector<uint8> m = { OG_S_COLORS, kind, op, 0, 0 };
	uint16 n = 0;
	for (const auto& e : entries) {
		size_t len = std::min(e.second.size(), (size_t)63);
		if (m.size() + 4 + len > 60000)
			break;
		m.push_back((uint8)(e.first >> 16));
		m.push_back((uint8)(e.first >> 8));
		m.push_back((uint8)e.first);
		m.push_back((uint8)len);
		m.insert(m.end(), e.second.begin(), e.second.begin() + len);
		n++;
	}
	m[3] = (uint8)n;
	m[4] = (uint8)(n >> 8);
	return m;
}

/// name colour of a character: own setting first, then the group colour; -1 = none
static int32 og_namecolor(map_session_data* sd) {
	int32 reg = (int32)pc_readglobalreg(sd, add_str(OG_REG_NAMECOLOR));
	if (reg & 0x1000000)
		return reg & 0xFFFFFF;
	for (const auto& g : og_group_namecolors())
		if (g.first == sd->group_id)
			return (int32)g.second;
	return -1;
}

static void og_broadcast(const std::vector<uint8>& msg) {
	map_foreachpc([](map_session_data* other, va_list ap) -> int32 {
		const std::vector<uint8>* m = va_arg(ap, const std::vector<uint8>*);
		if (other->fd > 0 && og_session_active(other->fd))
			og_send_control(other->fd, *m);
		return 0;
	}, &msg);
}

static void og_announce_name(map_session_data* sd, bool remove) {
	int32 rgb = og_namecolor(sd);
	if (rgb < 0 && !remove)
		return;
	og_broadcast(og_colors_msg(OG_COLOR_NAME, remove ? OG_COLOR_OP_REMOVE : OG_COLOR_OP_SET, { { (uint32)std::max(rgb, 0), sd->status.name } }));
}

void og_map_set_namecolor(map_session_data* sd, int32 rgb) {
	og_announce_name(sd, true);
	pc_setglobalreg(sd, add_str(OG_REG_NAMECOLOR), rgb < 0 ? 0 : (0x1000000 | (rgb & 0xFFFFFF)));
	og_announce_name(sd, false);
}

void og_map_on_load(map_session_data* sd, bool first) {
	if (!og_enabled())
		return;
	if (first) {
		int32 aura = (int32)pc_readglobalreg(sd, add_str(OG_REG_AURA));
		if (aura > 0 && !og_aura_allowed(aura)) {
			// removed from the list since it was set
			pc_setglobalreg(sd, add_str(OG_REG_AURA), 0);
			aura = 0;
		}
		og_apply_aura(sd, 0, aura, false);
		if (og_session_active(sd->fd)) {
			// item colours (names from the item database) and the coloured names of everyone online
			std::vector<std::pair<uint32, std::string>> items, names;
			for (const auto& c : og_item_colors())
				if (std::shared_ptr<item_data> id = item_db.find(c.first); id != nullptr)
					items.emplace_back(c.second, id->name);
			og_send_control(sd->fd, og_colors_msg(OG_COLOR_ITEM, OG_COLOR_OP_REPLACE, items));
			map_foreachpc([](map_session_data* other, va_list ap) -> int32 {
				auto* v = va_arg(ap, og_color_list*);
				int32 rgb = og_namecolor(other);
				if (rgb >= 0)
					v->emplace_back((uint32)rgb, other->status.name);
				return 0;
			}, &names);
			og_send_control(sd->fd, og_colors_msg(OG_COLOR_NAME, OG_COLOR_OP_REPLACE, names));
		}
		og_announce_name(sd, false);
	}
}

void og_map_on_quit(map_session_data* sd) {
	if (og_enabled() && og_namecolor(sd) >= 0)
		og_announce_name(sd, true);
}
