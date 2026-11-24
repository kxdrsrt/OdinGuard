// OdinGuard on the map-server. See og_map.hpp.

#include "og_map.hpp"

#include <algorithm>
#include <string>

#include <common/odinguard.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utilities.hpp>

#include <common/og_proto.h>

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "unit.hpp"

// character registry keys
#define OG_REG_FLAGS "og_flags"         // bit 1: lite ground skills, bit 3: only SG/LoV/MS (bit 0 was "hide auras", now src/map/aura.cpp)
#define OG_REG_LGP_STYLE "og_lgp_style" // 0 = square, 1 = circle, 2.. = one colour for every skill (og_lgp_color order)
#define OG_REG_NAMECOLOR "og_namecolor" // 0x1RRGGBB (bit 24 marks "set")
#define OG_FLAG_LGP 2
#define OG_FLAG_LGP_AOES 8

TIMER_FUNC(og_lgp_flash_end);

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

static void og_map_event(int32 fd, const char* type, const char* detail, e_og_action action, int32 score) {
	const s_og_info* info = og_info(fd);
	map_session_data* sd = og_sd(fd);
	std::string uid = info ? info->unique_id : std::string();

	std::string q = "INSERT INTO `og_event` (`unique_id`,`account_id`,`char_name`,`type`,`detail`,`action`) VALUES ('" +
		uid + "'," + std::to_string(sd ? sd->status.account_id : 0) + ",'" +
		og_esc(sd ? sd->status.name : "") + "','" + og_esc(std::string(type).substr(0, 64)) + "','" +
		og_esc(std::string(detail).substr(0, 255)) + "','" + og_action_name(action) + "')";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str()))
		Sql_ShowDebug(mmysql_handle);

	switch (action) {
	case OG_ACTION_FLAG:
	case OG_ACTION_FLAGBAN:
		// flag now, act later: the player cannot tell which check fired (ban-system module)
		og_map_flag(sd, uid.c_str(), type, score, action == OG_ACTION_FLAGBAN, detail);
		break;
	case OG_ACTION_BAN:
		if (!uid.empty())
			og_insert_ban(uid, "OdinGuard", type);
		[[fallthrough]];
	case OG_ACTION_KICK:
		if (sd != nullptr)
			clif_authfail_fd(fd, 15); // "You have been forced to disconnect by the GM Team"
		else
			set_eof(fd);
		break;
	default:
		break;
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
	add_timer_func_list(og_lgp_flash_end, "og_lgp_flash_end");
	og_map_guard_init();
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
	// client commands can be renamed in the profile (og_cmdname_<id>)
	const char* id = !strcmpi(cmd, og_map_command_name("ping")) ? "ping" : !strcmpi(cmd, og_map_command_name("vsync")) ? "vsync"
		: !strcmpi(cmd, og_map_command_name("crash")) ? "crash" : nullptr;
	if (id == nullptr || !og_module_on("og_client_commands", true))
		return false;
	if (!og_map_command_allowed(sd, id, !strcmp(id, "crash") ? 60000 : !strcmp(id, "ping") ? 2000 : 1000))
		return true;
	safestrncpy(cmd, id, sizeof(cmd));
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
	og_map_event(target->fd, "gm-ban", reason, OG_ACTION_LOG, 0);
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
	safesnprintf(out, size, "%s: protocol v%d, build '%s', machine %.16s, exe %.16s, guard %.16s, flags %d, signatures v%u", target->status.name,
		info->version, info->build.c_str(), info->unique_id.c_str(), info->exe_code.c_str(), info->guard_code.c_str(), info->flags, og_sigpack_version());
}

// ---------------------------------------------------------------- display features

static int32 og_flags(const map_session_data* sd) {
	return (int32)pc_readglobalreg(const_cast<map_session_data*>(sd), add_str(OG_REG_FLAGS));
}

static bool og_lgp_in_aoes(uint16 skill_id) {
	// @aoes: only the three big area skills (og_lgp_aoes: comma separated skill ids)
	char list[128];
	safestrncpy(list, og_setting("og_lgp_aoes", "89,83,85"), sizeof(list));
	for (char* t = strtok(list, ", "); t != nullptr; t = strtok(nullptr, ", "))
		if (atoi(t) == skill_id) return true;
	return false;
}

uint16 og_map_lgp_unit(const map_session_data* viewer, uint16 skill_id) {
	if (viewer == nullptr || !og_module_on("og_lgp_enable", true))
		return 0;
	int32 flags = og_flags(viewer);
	if (!(flags & OG_FLAG_LGP) || ((flags & OG_FLAG_LGP_AOES) && !og_lgp_in_aoes(skill_id)))
		return 0;
	// clients with the colour patch (og_lgp.c) draw the palette units, others the blue fallback units
	const s_og_info* info = og_info(viewer->fd);
	bool palette = info != nullptr && info->lgp_palette;
	uint16 base = og_lgp_unit_style("square", skill_id, palette);
	if (base == 0)
		return 0;
	int32 style = (int32)pc_readglobalreg(const_cast<map_session_data*>(viewer), add_str(OG_REG_LGP_STYLE));
	if (style <= 0)
		return base;
	char name[16];
	if (style == 1) safestrncpy(name, "circle", sizeof(name));
	else safesnprintf(name, sizeof(name), "set%d", style);
	if (uint16 unit = og_lgp_unit_style(name, skill_id, palette); unit != 0)
		return unit;
	// @square <n>: every skill in colour n-1 of the og_lgp_color list
	if (style >= 2 && style - 2 < og_lgp_color_count())
		return og_lgp_color_unit(style - 2, palette);
	return base;
}

// Flash cells: ground skills without a unit (Arrow Shower, Hammer Fall ...) only have the position effect. LGP
// players get one lite unit per cell of the splash area instead, removed again after og_lgp_flash_ms. The ids
// are only known to that viewer's client and come from the gap between account ids (END_ACCOUNT_NUM) and NPC/mob
// ids (START_NPC_NUM) that no game object uses (the 2018 client ignores units with ids from 0x7E000000 on).
#define OG_LGP_FLASH_BASE 100000000u
#define OG_LGP_FLASH_IDS 0x100000u
static uint32 og_lgp_flash_next = 0;

TIMER_FUNC(og_lgp_flash_end) {
	map_session_data* sd = map_id2sd(id);
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd))
		return 0;
	uint32 first = OG_LGP_FLASH_BASE + (uint32)(data >> 8);
	for (int32 i = 0; i < (int32)(data & 0xff); i++) {
		unsigned char buf[6];
		WBUFW(buf, 0) = 0x0120; // ZC_SKILL_DISAPPEAR
		WBUFL(buf, 2) = first + i;
		clif_send(buf, sizeof(buf), sd, SELF);
	}
	return 0;
}

void og_map_lgp_flash(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y) {
	og_map_lgp_flash_area(viewer, src, skill_id, skill_lv, x, y, skill_get_splash(skill_id, std::max<uint16>(skill_lv, 1)), false);
}

void og_map_lgp_flash_area(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y, int32 range, bool skip_center) {
	uint16 unit = og_map_lgp_unit(viewer, skill_id);
	if (unit == 0)
		return;
	int32 r = std::clamp(range, 0, 3);
	int32 n = (2 * r + 1) * (2 * r + 1);
	if (og_lgp_flash_next + n >= OG_LGP_FLASH_IDS)
		og_lgp_flash_next = 0;
	uint32 start = og_lgp_flash_next;
	og_lgp_flash_next += n;
	int32 sent = 0;
	for (int32 dy = -r; dy <= r; dy++) {
		for (int32 dx = -r; dx <= r; dx++) {
			int16 cx = x + dx, cy = y + dy;
			if ((skip_center && dx == 0 && dy == 0) || cx < 0 || cy < 0 || cx >= map[src.m].xs || cy >= map[src.m].ys || map_getcell(src.m, cx, cy, CELL_CHKWALL))
				continue;
			unsigned char buf[23];
			WBUFW(buf, 0) = 0x09ca; // ZC_SKILL_ENTRY5
			WBUFW(buf, 2) = sizeof(buf);
			WBUFL(buf, 4) = OG_LGP_FLASH_BASE + start + sent;
			WBUFL(buf, 8) = src.id;
			WBUFW(buf, 12) = cx;
			WBUFW(buf, 14) = cy;
			WBUFL(buf, 16) = unit;
			WBUFB(buf, 20) = 0;
			WBUFB(buf, 21) = 1;
			WBUFB(buf, 22) = 1; // see clif_getareachar_skillunit: lite units always with level 1
			clif_send(buf, sizeof(buf), viewer, SELF);
			sent++;
		}
	}
	if (sent > 0)
		add_timer(gettick() + og_setting_int("og_lgp_flash_ms", 1000), og_lgp_flash_end, viewer->id, (intptr_t)((start << 8) | (uint32)sent));
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

/// @lgp / @square / @circle / @aoes. kind: 0 lgp, 1 square, 2 circle, 3 aoes; arg: "", "on", "off" or a set number
void og_map_lgp_command(map_session_data* sd, int32 kind, const char* arg) {
	int32 flags = og_flags(sd), style = (int32)pc_readglobalreg(sd, add_str(OG_REG_LGP_STYLE));
	bool on = !(flags & OG_FLAG_LGP);
	if (!strcmpi(arg, "on")) on = true;
	else if (!strcmpi(arg, "off")) on = false;
	else if (kind == 1 && atoi(arg) > 0) {
		int32 set = atoi(arg), sets = 1 + og_lgp_color_count();
		if (set > sets) {
			char m[CHAT_SIZE_MAX];
			safesnprintf(m, sizeof(m), "Colour sets: 1 = every skill in its own colour, 2-%d = one colour for every skill.", sets);
			clif_displaymessage(sd->fd, m);
			return;
		}
		style = set == 1 ? 0 : set;
		on = true;
	} else if (kind != 0 && *arg == '\0')
		on = true;
	if (kind == 1 && atoi(arg) <= 0) style = 0;
	if (kind == 2) style = 1;
	if (kind == 3) flags = on ? (flags | OG_FLAG_LGP_AOES) : (flags & ~OG_FLAG_LGP_AOES);
	else if (kind != 0) flags &= ~OG_FLAG_LGP_AOES;
	flags = on ? (flags | OG_FLAG_LGP) : (flags & ~(OG_FLAG_LGP | OG_FLAG_LGP_AOES));
	pc_setglobalreg(sd, add_str(OG_REG_FLAGS), flags);
	pc_setglobalreg(sd, add_str(OG_REG_LGP_STYLE), style);
	char msg[CHAT_SIZE_MAX];
	if (!on)
		safesnprintf(msg, sizeof(msg), "Lite ground skills off: normal skill effects.");
	else
		safesnprintf(msg, sizeof(msg), "Lite ground skills on (%s%s): %s are shown as flat coloured cells.",
			style == 1 ? "circle" : style > 1 ? "colour set " : "square", style > 1 ? std::to_string(style).c_str() : "",
			(flags & OG_FLAG_LGP_AOES) ? "Storm Gust, Lord of Vermilion and Meteor Storm" : "area skills");
	clif_displaymessage(sd->fd, msg);
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
	og_map_guard_quit(sd);
	if (og_enabled() && og_namecolor(sd) >= 0)
		og_announce_name(sd, true);
}
