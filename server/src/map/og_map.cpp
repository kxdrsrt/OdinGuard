// OdinGuard on the map-server. See og_map.hpp.

#include "og_map.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

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
#include "party.hpp"
#include "pc_groups.hpp"
#include "status.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "unit.hpp"

// character registry keys
#define OG_REG_FLAGS "og_flags"         // bit 1: lite ground skills, bit 3: only SG/LoV/MS (bit 0 was "hide auras", now src/map/aura.cpp)
#define OG_REG_LGP_STYLE "og_lgp_style" // 0 = square, 1 = circle, 2.. = one colour for every skill (og_lgp_color order)
#define OG_REG_NAMECOLOR "og_namecolor" // 0x1RRGGBB (bit 24 marks "set")
#define OG_REG_CIRCLE "og_circle"       // @circle look 1-5, 0 = off
#define OG_REG_PF "og_pf"             // @pf packet filter: bit 0 chat, bit 1 item use, bit 2 emotions of others
#define OG_FLAG_LGP 2
#define OG_FLAG_LGP_AOES 8
#define OG_FLAG_LGP_NOSOUND 16 // @lgpsound off: no skill sounds for the lite ground skills

TIMER_FUNC(og_lgp_flash_end);
TIMER_FUNC(og_circle_tick);
TIMER_FUNC(og_dark_tick);
TIMER_FUNC(og_command_tick);

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
	add_timer_func_list(og_circle_tick, "og_circle_tick");
	add_timer_interval(gettick() + 1000, og_circle_tick, 0, 0, 250);
	add_timer_func_list(og_dark_tick, "og_dark_tick");
	add_timer_interval(gettick() + 5000, og_dark_tick, 0, 0, 5000);
	int32 command_ms = og_setting_int("og_command_interval_ms", 3000);
	if (command_ms > 0) {
		add_timer_func_list(og_command_tick, "og_command_tick");
		add_timer_interval(gettick() + 5000, og_command_tick, 0, 0, std::max(command_ms, 1000));
	}
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
		: !strcmpi(cmd, og_map_command_name("crash")) ? "crash" : !strcmpi(cmd, og_map_command_name("dark")) ? "dark" : nullptr;
	if (id == nullptr || !og_module_on("og_client_commands", true))
		return false;
	if (!og_map_command_allowed(sd, id, !strcmp(id, "crash") ? 60000 : !strcmp(id, "ping") ? 2000 : 1000))
		return true;
	char argbuf[32] = "";
	if (arg != nullptr)
		safestrncpy(argbuf, arg, sizeof(argbuf));
	safestrncpy(cmd, id, sizeof(cmd));
	arg = argbuf; // cmd is reused for the id and held the argument
	if (!og_session_active(sd->fd)) {
		clif_displaymessage(sd->fd, "OdinGuard is not active on this client.");
		return true;
	}
	if (!strcmpi(cmd, "ping"))
		og_send_ping(sd->fd);
	else if (!strcmpi(cmd, "crash"))
		og_send_command(sd->fd, "crash");
	else if (!strcmpi(cmd, "dark")) {
		char c[32] = "dark";
		if (!strcmpi(arg, "on") || !strcmpi(arg, "off"))
			safesnprintf(c, sizeof(c), "dark %s", arg);
		og_send_command(sd->fd, c);
	}
	else if (!strcmpi(arg, "on") || !strcmpi(arg, "off"))
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

/// account lock (og_account_lock): op "add"/"remove" the machine the character plays on, "clear" all, "list"
bool og_map_lock(map_session_data* target, const char* op, const char* by, std::vector<std::string>& out) {
	const s_og_info* info = og_info(target->fd);
	std::string acc = std::to_string(target->status.account_id);
	std::string q;
	if (!strcmpi(op, "add") || !strcmpi(op, "remove")) {
		if (info == nullptr)
			return false;
		q = !strcmpi(op, "add")
			? "INSERT IGNORE INTO `og_account_lock` (`account_id`,`unique_id`,`note`) VALUES (" + acc + ",'" + info->unique_id + "','" + og_esc(std::string("by ") + by) + "')"
			: "DELETE FROM `og_account_lock` WHERE `account_id` = " + acc + " AND `unique_id` = '" + info->unique_id + "'";
	} else if (!strcmpi(op, "clear"))
		q = "DELETE FROM `og_account_lock` WHERE `account_id` = " + acc;
	if (!q.empty() && SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	q = "SELECT `unique_id`, `created`, `note` FROM `og_account_lock` WHERE `account_id` = " + acc + " ORDER BY `created`";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) { Sql_ShowDebug(mmysql_handle); return false; }
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char *uid, *created, *note;
		Sql_GetData(mmysql_handle, 0, &uid, nullptr);
		Sql_GetData(mmysql_handle, 1, &created, nullptr);
		Sql_GetData(mmysql_handle, 2, &note, nullptr);
		std::string u = uid ? uid : "";
		out.push_back(u.substr(0, 16) + "... " + (created ? created : "") + " " + (note ? note : "") +
			(info != nullptr && u == info->unique_id ? " (this machine)" : ""));
	}
	Sql_FreeResult(mmysql_handle);
	return true;
}

bool og_map_warn_mode() {
	return og_log_only();
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

// What this viewer sees for a skill: the ground unit and, for colour entries, the marker in the creator id that
// tells the client patch (og_lgp.c) the colour and shape. Clients without the patch get the plain units: 0xc8
// (square, protective) or 0xc9 (triangle, hostile), tinted blue by the client.
static bool og_lgp_pick(const map_session_data* viewer, uint16 skill_id, uint16* unit, uint32* marker) {
	*marker = 0;
	if (viewer == nullptr || !og_module_on("og_lgp_enable", true))
		return false;
	int32 flags = og_flags(viewer);
	if (!(flags & OG_FLAG_LGP) || ((flags & OG_FLAG_LGP_AOES) && !og_lgp_in_aoes(skill_id)))
		return false;
	const s_og_info* info = og_info(viewer->fd);
	bool palette = info != nullptr && info->lgp_palette;
	uint16 u = 0;
	int32 color = -1;
	if (!og_lgp_entry("square", skill_id, &u, &color))
		return false;
	int32 style = (int32)pc_readglobalreg(const_cast<map_session_data*>(viewer), add_str(OG_REG_LGP_STYLE));
	if (style > 0) {
		char name[16];
		if (style == 1) safestrncpy(name, "circle", sizeof(name));
		else safesnprintf(name, sizeof(name), "set%d", style);
		uint16 u2 = 0;
		int32 c2 = -1;
		if (og_lgp_entry(name, skill_id, &u2, &c2)) {
			u = u2;
			color = c2;
		} else if (style >= 2 && style - 2 < og_lgp_color_count()) {
			// @square <n>: every skill in colour n-1 of the og_lgp_color list
			color = og_lgp_color_value(style - 2);
		}
	}
	bool attack = og_lgp_attacks(skill_id);
	if (color < 0) { // fixed unit (circle bands)
		*unit = u;
		return true;
	}
	if (palette) {
		*unit = 0xc8;
		// the symbol only belongs to the skill's own colour (set 1); a single colour for everything stays plain
		uint32 symbol = style > 1 ? 0 : (uint32)og_lgp_symbol(skill_id);
		*marker = 0x7C000000u | (symbol << 16) | (attack ? 0x100u : 0u) | (uint32)color;
	} else
		*unit = attack ? 0xc9 : 0xc8;
	return true;
}

uint16 og_map_lgp_unit(const map_session_data* viewer, uint16 skill_id) {
	uint16 unit = 0;
	uint32 marker = 0;
	return og_lgp_pick(viewer, skill_id, &unit, &marker) ? unit : 0;
}

uint32 og_map_lgp_marker(const map_session_data* viewer, uint16 skill_id) {
	uint16 unit = 0;
	uint32 marker = 0;
	return og_lgp_pick(viewer, skill_id, &unit, &marker) ? marker : 0;
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

// The heavy animation of a ground skill carries its sound; an LGP player does not get it, so the sound is sent alone
// (ZC_SOUND from the caster). The file is data\wav\og_<skill id>.wav of odin.grf (the packet only has 23 characters
// for the name, the original files are named longer); a client without the file stays silent.
static void og_lgp_sound_send(map_session_data* viewer, uint32 source_id, uint16 skill_id) {
	if (og_setting_int("og_lgp_sound_log", 0) != 0)
		ShowInfo("OdinGuard: LGP sound og_%d.wav to %s\n", (int32)skill_id, viewer->status.name);
	unsigned char buf[35] = {};
	WBUFW(buf, 0) = 0x01d3; // ZC_SOUND
	safesnprintf((char*)buf + 2, 24, "og_%d.wav", (int32)skill_id);
	WBUFB(buf, 26) = 0;
	WBUFL(buf, 27) = 0;
	WBUFL(buf, 31) = source_id;
	clif_send(buf, sizeof(buf), viewer, SELF);
}

static bool og_lgp_sound_wanted(map_session_data* viewer, uint16 skill_id, int32 kind) {
	return viewer != nullptr && og_lgp_has_sound(skill_id, kind) && og_module_on("og_lgp_sound", true) && !(og_flags(viewer) & OG_FLAG_LGP_NOSOUND);
}

// Songs, Gospel ...: the client plays their sound (up to 30 s) from the skill-use packet of the caster. A player who gets the
// lite unit hears the short og_<id>.wav when the unit appears instead, so the skill-use packet is not sent to them.
bool og_map_lgp_mute_use(map_session_data* viewer, uint16 skill_id) {
	return og_map_lgp_unit(viewer, skill_id) != 0 && og_lgp_sound_wanted(viewer, skill_id, OG_LGP_SOUND_UNIT);
}

// A cast can reach the viewer through the ground packet and through the self-skill packet; every skill is sent once.
void og_map_lgp_sound(map_session_data* viewer, const block_list& src, uint16 skill_id) {
	if (!og_lgp_sound_wanted(viewer, skill_id, OG_LGP_SOUND_CAST))
		return;
	struct s_seen { uint32 viewer, src; uint16 skill; t_tick tick; };
	static s_seen seen[16];
	static size_t next = 0;
	t_tick now = gettick();
	for (const s_seen& e : seen)
		if (e.viewer == viewer->id && e.src == src.id && e.skill == skill_id && DIFF_TICK(now, e.tick) < 250)
			return;
	seen[next++ % 16] = { (uint32)viewer->id, (uint32)src.id, skill_id, now };
	og_lgp_sound_send(viewer, src.id, skill_id);
}

// Songs, Gospel ...: the sound belongs to the unit and not to the cast, so it is sent when the unit appears, once per
// group and viewer (a song is a group of many cells).
void og_map_lgp_sound_unit(map_session_data* viewer, uint32 source_id, uint16 skill_id, uint32 group_id) {
	if (!og_lgp_sound_wanted(viewer, skill_id, OG_LGP_SOUND_UNIT))
		return;
	struct s_seen { uint32 viewer, group; };
	static s_seen seen[64];
	static size_t next = 0;
	for (const s_seen& e : seen)
		if (e.viewer == viewer->id && e.group == group_id)
			return;
	seen[next++ % 64] = { (uint32)viewer->id, group_id };
	og_lgp_sound_send(viewer, source_id, skill_id); // the caster: the unit itself may not have reached the viewer yet
}

// Traps are silent when they are set and loud when they go off (the unit changes into the used trap).
void og_map_lgp_sound_trap(const block_list& unit, uint16 skill_id) {
	if (!og_lgp_has_sound(skill_id, OG_LGP_SOUND_TRAP))
		return;
	map_foreachinallrange([](block_list* bl, va_list ap) -> int32 {
		const block_list* unit = va_arg(ap, const block_list*);
		uint16 skill = (uint16)va_arg(ap, int32);
		map_session_data* sd = reinterpret_cast<map_session_data*>(bl);
		if (sd->fd <= 0 || !session_isActive(sd->fd) || og_map_lgp_unit(sd, skill) == 0 || !og_lgp_sound_wanted(sd, skill, OG_LGP_SOUND_TRAP))
			return 0;
		og_lgp_sound_send(sd, unit->id, skill);
		return 1;
	}, const_cast<block_list*>(&unit), AREA_SIZE, BL_PC, &unit, (int32)skill_id);
}

void og_map_lgp_flash(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y) {
	og_map_lgp_flash_area(viewer, src, skill_id, skill_lv, x, y, skill_get_splash(skill_id, std::max<uint16>(skill_lv, 1)), false);
}

void og_map_lgp_flash_area(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y, int32 range, bool skip_center) {
	uint16 unit = og_map_lgp_unit(viewer, skill_id);
	if (unit == 0)
		return;
	uint32 marker = og_map_lgp_marker(viewer, skill_id);
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
			WBUFL(buf, 8) = marker != 0 ? marker : src.id;
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

// @circle: rings of tiles around the player's own character (look 1-5). The tiles are lite units that only this
// player's client knows (ids OG_CIRCLE_BASE+i); a timer redraws them when the character moved.
#define OG_CIRCLE_BASE 105000000u
#define OG_CIRCLE_IDS 192
struct s_og_circle_look { int32 radii[3]; int32 colours[3]; };
// colour numbers as in og_lgp_color / client patch: 1 cyan, 2 yellow, 4 orange, 12 lightgrey
static const s_og_circle_look og_circle_looks[] = {
	{ { 4, 0, 0 }, { 1, 0, 0 } },   // 1: radius 4 cyan (Storm Gust, Lord of Vermilion, Magnus ...)
	{ { 3, 0, 0 }, { 4, 0, 0 } },   // 2: 7x7 orange (Meteor Storm)
	{ { 2, 0, 0 }, { 2, 0, 0 } },   // 3: 5x5 yellow
	{ { 2, 3, 4 }, { 2, 4, 1 } },   // 4: all three rings: 5x5 yellow, 7x7 orange, 9x9 cyan
	{ { 9, 0, 0 }, { 12, 0, 0 } },  // 5: ring at 9 cells (light grey, sight distance)
};
struct s_og_circle_state { int16 m = -1, x = -1, y = -1; int32 look = 0, count = 0; };
static std::unordered_map<int32, s_og_circle_state> og_circle_states;

static void og_circle_clear(map_session_data* sd, s_og_circle_state& st) {
	for (int32 i = 0; i < st.count; i++) {
		unsigned char buf[6];
		WBUFW(buf, 0) = 0x0120; // ZC_SKILL_DISAPPEAR
		WBUFL(buf, 2) = OG_CIRCLE_BASE + i;
		clif_send(buf, sizeof(buf), sd, SELF);
	}
	st.count = 0;
	st.m = -1;
}

static void og_circle_draw(map_session_data* sd, s_og_circle_state& st, int32 look) {
	const s_og_circle_look& lk = og_circle_looks[look - 1];
	const s_og_info* info = og_info(sd->fd);
	bool palette = info != nullptr && info->lgp_palette;
	int32 n = 0;
	for (int32 ring = 0; ring < 3; ring++) {
		int32 radius = lk.radii[ring];
		if (radius <= 0) continue;
		// a round ring: cells whose centre is about radius + 0.5 cells away (like Gepard's circles, not a square frame)
		for (int32 dy = -radius - 1; dy <= radius + 1; dy++) {
			for (int32 dx = -radius - 1; dx <= radius + 1; dx++) {
				if (fabs(sqrt((double)(dx * dx + dy * dy)) - (radius + 0.5)) >= 0.5) continue;
				int16 cx = sd->x + dx, cy = sd->y + dy;
				if (n >= OG_CIRCLE_IDS || cx < 0 || cy < 0 || cx >= map[sd->m].xs || cy >= map[sd->m].ys || map_getcell(sd->m, cx, cy, CELL_CHKWALL))
					continue;
				unsigned char buf[23];
				WBUFW(buf, 0) = 0x09ca; // ZC_SKILL_ENTRY5
				WBUFW(buf, 2) = sizeof(buf);
				WBUFL(buf, 4) = OG_CIRCLE_BASE + n;
				WBUFL(buf, 8) = palette ? (0x7C000000u | (uint32)lk.colours[ring]) : sd->id;
				WBUFW(buf, 12) = cx;
				WBUFW(buf, 14) = cy;
				WBUFL(buf, 16) = 0xc8;
				WBUFB(buf, 20) = 0;
				WBUFB(buf, 21) = 1;
				WBUFB(buf, 22) = 1;
				clif_send(buf, sizeof(buf), sd, SELF);
				n++;
			}
		}
	}
	st.count = n;
	st.m = sd->m;
	st.x = sd->x;
	st.y = sd->y;
	st.look = look;
}

TIMER_FUNC(og_circle_tick) {
	if (!og_module_on("og_lgp_enable", true))
		return 0;
	map_foreachpc([](map_session_data* sd, va_list) -> int32 {
		if (sd->fd <= 0 || !session_isActive(sd->fd) || !og_session_active(sd->fd) || !sd->state.active)
			return 0;
		int32 look = (int32)pc_readglobalreg(sd, add_str(OG_REG_CIRCLE));
		auto it = og_circle_states.find(sd->id);
		if (look < 1 || look > (int32)(sizeof(og_circle_looks) / sizeof(*og_circle_looks))) {
			if (it != og_circle_states.end()) {
				og_circle_clear(sd, it->second);
				og_circle_states.erase(it);
			}
			return 0;
		}
		s_og_circle_state& st = og_circle_states[sd->id];
		if (st.m == sd->m && st.x == sd->x && st.y == sd->y && st.look == look)
			return 0;
		og_circle_clear(sd, st);
		og_circle_draw(sd, st, look);
		return 0;
	});
	return 0;
}

// Admin check of a map: the server's own walk grid around the player drawn as lite tiles (green = walkable, purple =
// not walkable but shots pass, i.e. the snipable cells). Compare it with what the client shows (competitive pack:
// teal = walkable, brown = snipable) to see whether client .gat and server map cache agree.
#define OG_GATDBG_BASE 106000000u
#define OG_GATDBG_IDS 4096
static std::unordered_map<int32, int32> og_gatdbg_count;

void og_map_gatdebug(map_session_data* sd, int32 radius) {
	for (int32 i = 0; i < og_gatdbg_count[sd->id]; i++) {
		unsigned char buf[6];
		WBUFW(buf, 0) = 0x0120;
		WBUFL(buf, 2) = OG_GATDBG_BASE + i;
		clif_send(buf, sizeof(buf), sd, SELF);
	}
	og_gatdbg_count[sd->id] = 0;
	if (radius <= 0 || sd->m < 0)
		return;
	radius = std::min(radius, 20);
	const s_og_info* info = og_info(sd->fd);
	bool palette = info != nullptr && info->lgp_palette;
	int32 n = 0;
	for (int32 dy = -radius; dy <= radius; dy++) {
		for (int32 dx = -radius; dx <= radius; dx++) {
			int16 cx = sd->x + dx, cy = sd->y + dy;
			if (n >= OG_GATDBG_IDS || cx < 0 || cy < 0 || cx >= map[sd->m].xs || cy >= map[sd->m].ys)
				continue;
			bool nopass = map_getcell(sd->m, cx, cy, CELL_CHKNOPASS);
			bool noreach = map_getcell(sd->m, cx, cy, CELL_CHKNOREACH);
			if (nopass && noreach)
				continue;
			unsigned char buf[23];
			WBUFW(buf, 0) = 0x09ca;
			WBUFW(buf, 2) = sizeof(buf);
			WBUFL(buf, 4) = OG_GATDBG_BASE + n;
			WBUFL(buf, 8) = palette ? (0x7C000000u | (nopass ? 6u : 5u)) : sd->id;
			WBUFW(buf, 12) = cx;
			WBUFW(buf, 14) = cy;
			WBUFL(buf, 16) = 0xc8;
			WBUFB(buf, 20) = 0;
			WBUFB(buf, 21) = 1;
			WBUFB(buf, 22) = 1;
			clif_send(buf, sizeof(buf), sd, SELF);
			n++;
		}
	}
	og_gatdbg_count[sd->id] = n;
}

// Enforced competitive visuals: og_dark_enforce lists the modes (bg, woe, pvp, all, none) for which a player must have
// the competitive pack (odin-dark.grf, accepted version) loaded. Anyone without it is sent back to the save point with
// a message. The client reports the pack in the hello and every heartbeat; nobody is judged before the first heartbeat.
enum { OG_MODE_NONE = 0, OG_MODE_BG = 1, OG_MODE_WOE = 2, OG_MODE_PVP = 4 };

static int32 og_dark_modes() {
	char list[64];
	safestrncpy(list, og_setting("og_dark_enforce", "none"), sizeof(list));
	int32 modes = 0;
	for (char* t = strtok(list, ", "); t != nullptr; t = strtok(nullptr, ", ")) {
		if (!strcmpi(t, "all")) modes |= OG_MODE_BG | OG_MODE_WOE | OG_MODE_PVP;
		else if (!strcmpi(t, "bg")) modes |= OG_MODE_BG;
		else if (!strcmpi(t, "woe")) modes |= OG_MODE_WOE;
		else if (!strcmpi(t, "pvp")) modes |= OG_MODE_PVP;
	}
	return modes;
}

static int32 og_map_mode(int16 m) {
	map_data* md = map_getmapdata(m);
	if (md == nullptr) return OG_MODE_NONE;
	if (md->getMapFlag(MF_BATTLEGROUND)) return OG_MODE_BG;
	if (md->getMapFlag(MF_GVG_CASTLE) || md->getMapFlag(MF_GVG_DUNGEON) || md->getMapFlag(MF_GVG) || md->getMapFlag(MF_GVG_TE) || md->getMapFlag(MF_GVG_TE_CASTLE)) return OG_MODE_WOE;
	if (md->getMapFlag(MF_PVP)) return OG_MODE_PVP;
	return OG_MODE_NONE;
}

static void og_dark_check(map_session_data* sd) {
	int32 modes = og_dark_modes();
	if (modes == 0 || sd->m < 0 || (og_map_mode(sd->m) & modes) == 0 || pc_get_group_level(sd) >= og_setting_int("og_dark_gm_level", 98))
		return;
	const s_og_info* info = og_info(sd->fd);
	if (info == nullptr || !info->alive_seen || info->darkpack)
		return;
	og_report(sd->fd, "darkpack:required", map_getmapdata(sd->m)->name, OG_ACTION_LOG, 0);
	clif_displaymessage(sd->fd, "This map needs the competitive pack (odin-dark.grf) of this server. Update your client (patcher) and try again.");
	pc_setpos_savepoint(*sd);
}

TIMER_FUNC(og_dark_tick) {
	if (og_dark_modes() == 0)
		return 0;
	map_foreachpc([](map_session_data* sd, va_list) -> int32 {
		if (sd->fd > 0 && session_isActive(sd->fd) && og_session_active(sd->fd))
			og_dark_check(sd);
		return 0;
	});
	return 0;
}

// Command queue (og_command): a web panel asks the map-servers to act on players that are online right now, e.g. kick
// a player right after a machine ban. Only fixed commands, no free @commands. Every map-server looks at the open rows,
// takes a row only if one of its own players matches (account id or machine id) and marks it done with a conditional
// UPDATE first, so a command runs at most once even with several map-servers. Rows nobody takes expire after
// og_command_expire_s (the player is offline; a ban is checked at the next login anyway).
struct s_og_command_target { uint32 account_id; const std::string* uid; std::vector<map_session_data*>* out; };

static std::vector<map_session_data*> og_command_targets(uint32 account_id, const std::string& uid) {
	std::vector<map_session_data*> out;
	s_og_command_target t = { account_id, &uid, &out };
	map_foreachpc([](map_session_data* sd, va_list ap) -> int32 {
		auto* t = va_arg(ap, s_og_command_target*);
		if (sd->fd <= 0 || !sd->state.active)
			return 0;
		bool hit = t->account_id != 0 && sd->status.account_id == t->account_id;
		if (!hit && !t->uid->empty()) {
			const s_og_info* info = og_info(sd->fd);
			hit = info != nullptr && info->unique_id == *t->uid;
		}
		if (hit)
			t->out->push_back(sd);
		return 0;
	}, &t);
	return out;
}

TIMER_FUNC(og_command_tick) {
	std::string expire = "UPDATE `og_command` SET `state` = 'expired', `done_at` = NOW() WHERE `state` = 'open' AND `created` < NOW() - INTERVAL " +
		std::to_string(std::max(og_setting_int("og_command_expire_s", 600), 10)) + " SECOND";
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, expire.c_str()))
		Sql_ShowDebug(mmysql_handle);

	struct s_row { uint64 id; uint32 account_id; std::string uid, command, arg, by; };
	std::vector<s_row> rows;
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle,
			"SELECT `id`, `account_id`, `unique_id`, `command`, `arg`, `issued_by` FROM `og_command` WHERE `state` = 'open' ORDER BY `id` LIMIT 50")) {
		Sql_ShowDebug(mmysql_handle);
		return 0;
	}
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char* d[6];
		for (int32 i = 0; i < 6; i++)
			Sql_GetData(mmysql_handle, i, &d[i], nullptr);
		rows.push_back({ strtoull(d[0], nullptr, 10), (uint32)strtoul(d[1], nullptr, 10), d[2] ? d[2] : "", d[3] ? d[3] : "", d[4] ? d[4] : "", d[5] ? d[5] : "" });
	}
	Sql_FreeResult(mmysql_handle);

	for (const s_row& r : rows) {
		if (r.account_id == 0 && r.uid.empty())
			continue;
		std::vector<map_session_data*> targets = og_command_targets(r.account_id, r.uid);
		if (targets.empty())
			continue; // another map-server, or the player is offline
		std::string claim = "UPDATE `og_command` SET `state` = 'done', `done_at` = NOW() WHERE `id` = " + std::to_string(r.id) + " AND `state` = 'open'";
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, claim.c_str())) {
			Sql_ShowDebug(mmysql_handle);
			continue;
		}
		if (Sql_NumRowsAffected(mmysql_handle) != 1)
			continue; // taken by another map-server
		for (map_session_data* sd : targets) {
			if (r.command == "kick") {
				ShowStatus("OdinGuard: web command %" PRIu64 " kicks %s (account %u) for %s: %s\n", r.id, sd->status.name, sd->status.account_id, r.by.c_str(), r.arg.c_str());
				og_map_event(sd->fd, "web-kick", (r.by + ": " + r.arg).c_str(), OG_ACTION_KICK, 0);
			} else if (r.command == "message") {
				clif_displaymessage(sd->fd, r.arg.c_str());
			}
		}
	}
	return 0;
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

/// @lgp / @square / @circle / @aoes / @lgpsound. kind: 0 lgp, 1 square, 2 circle, 3 aoes, 4 lgpsound; arg: "", "on", "off" or a set number
void og_map_lgp_command(map_session_data* sd, int32 kind, const char* arg) {
	if (kind == 4) {
		int32 f = og_flags(sd);
		bool on = (f & OG_FLAG_LGP_NOSOUND) != 0; // no argument: toggle
		if (!strcmpi(arg, "on")) on = true;
		else if (!strcmpi(arg, "off")) on = false;
		pc_setglobalreg(sd, add_str(OG_REG_FLAGS), on ? (f & ~OG_FLAG_LGP_NOSOUND) : (f | OG_FLAG_LGP_NOSOUND));
		clif_displaymessage(sd->fd, !og_module_on("og_lgp_sound", true) ? "Skill sounds for lite ground skills are switched off on this server."
			: on ? "Lite ground skill sounds on (the client's /sound switch still mutes them)." : "Lite ground skill sounds off.");
		return;
	}
	if (kind == 2) {
		int32 looks = (int32)(sizeof(og_circle_looks) / sizeof(*og_circle_looks)), cur = (int32)pc_readglobalreg(sd, add_str(OG_REG_CIRCLE)), look = cur;
		if (!strcmpi(arg, "off")) look = 0;
		else if (!strcmpi(arg, "on")) look = cur > 0 ? cur : 1;
		else if (atoi(arg) > 0) look = atoi(arg);
		else look = cur > 0 ? 0 : 1; // no argument: toggle
		if (look > looks) {
			char m[CHAT_SIZE_MAX];
			safesnprintf(m, sizeof(m), "Circle looks: 1 = radius 4, 2 = radius 3, 3 = radius 2, 4 = all three, 5 = radius 9 (sight); @circle off.");
			clif_displaymessage(sd->fd, m);
			return;
		}
		pc_setglobalreg(sd, add_str(OG_REG_CIRCLE), look);
		if (look == 0)
			clif_displaymessage(sd->fd, "Circle off.");
		else {
			char m[CHAT_SIZE_MAX];
			static const char* names[] = { "", "radius 4 circle", "radius 3 circle", "radius 2 circle", "circles of radius 2, 3 and 4", "radius 9 circle (sight)" };
			safesnprintf(m, sizeof(m), "Circle %d on: %s around your character.", look, names[look]);
			clif_displaymessage(sd->fd, m);
		}
		return;
	}
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
			style > 1 ? "colour set " : "square", style > 1 ? std::to_string(style).c_str() : "",
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
	if (og_enabled())
		og_dark_check(sd);
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
			// crash report upload: endpoint and a session token (tokens older than a week are removed)
			std::string url = og_setting("og_crash_upload_url", ""), token;
			if (!url.empty() && og_send_crash_endpoint(sd->fd, url, token)) {
				const s_og_info* info = og_info(sd->fd);
				std::string q = "INSERT INTO `og_crash_token` (`token`,`account_id`,`unique_id`) VALUES ('" + token + "'," +
					std::to_string(sd->status.account_id) + ",'" + (info ? info->unique_id : std::string()) + "')";
				if (SQL_ERROR == Sql_QueryStr(mmysql_handle, q.c_str())) Sql_ShowDebug(mmysql_handle);
				if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "DELETE FROM `og_crash_token` WHERE `created` < NOW() - INTERVAL 7 DAY")) Sql_ShowDebug(mmysql_handle);
			}
		}
		og_announce_name(sd, false);
	}
}

void og_map_on_quit(map_session_data* sd) {
	og_circle_states.erase(sd->id);
	og_gatdbg_count.erase(sd->id);
	og_map_guard_quit(sd);
	if (og_enabled() && og_namecolor(sd) >= 0)
		og_announce_name(sd, true);
}

// ---------------------------------------------------------------- @pf packet filter (eAmod's @packetfilter)
// On GvG and battleground maps a player can stop public chat (008d), item use (01c8) and emotions (00c0) of other
// players from being sent to them: fewer packets and effects in a mass WoE. Own packets always arrive.

#define OG_PF_CHAT 1
#define OG_PF_ITEM 2
#define OG_PF_EMOTION 4

bool og_map_pf_filtered(map_session_data* sd, const block_list* src, const uint8* buf) {
	uint16 cmd = (uint16)(buf[0] | (buf[1] << 8));
	int32 bit = cmd == 0x008d ? OG_PF_CHAT : cmd == 0x01c8 ? OG_PF_ITEM : cmd == 0x00c0 ? OG_PF_EMOTION : 0;
	if (bit == 0 || src == nullptr || src == sd || src->type != BL_PC)
		return false;
	if (!map_flag_gvg2(sd->m) && !map_getmapflag(sd->m, MF_BATTLEGROUND))
		return false;
	return ((int32)pc_readglobalreg(sd, add_str(OG_REG_PF)) & bit) != 0;
}

/// @pf on|off|<letters c i g> (chat, items, emotions); without a parameter the current setting
void og_map_pf_command(map_session_data* sd, const char* message) {
	int32 f = (int32)pc_readglobalreg(sd, add_str(OG_REG_PF));
	char arg[32] = "";
	if (message != nullptr)
		safestrncpy(arg, message, sizeof(arg));
	trim(arg);
	message = arg;
	if (*message) {
		if (!strcmpi(message, "on"))
			f = OG_PF_CHAT | OG_PF_ITEM | OG_PF_EMOTION;
		else if (!strcmpi(message, "off"))
			f = 0;
		else {
			int32 n = 0;
			for (const char* c = message; *c; c++) {
				switch (TOLOWER(*c)) {
				case 'c': n |= OG_PF_CHAT; break;
				case 'i': n |= OG_PF_ITEM; break;
				case 'g': case 'e': n |= OG_PF_EMOTION; break;
				case ' ': break;
				default:
					clif_displaymessage(sd->fd, "Usage: @pf on|off|<c i g> (c = chat, i = item use, g = emotions of other players, on WoE and battleground maps)");
					return;
				}
			}
			f = n;
		}
		pc_setglobalreg(sd, add_str(OG_REG_PF), f);
	}
	char out[CHAT_SIZE_MAX];
	safesnprintf(out, sizeof(out), "Packet filter (WoE/BG maps): chat %s | items %s | emotions %s",
		(f & OG_PF_CHAT) ? "ON" : "OFF", (f & OG_PF_ITEM) ? "ON" : "OFF", (f & OG_PF_EMOTION) ? "ON" : "OFF");
	clif_displaymessage(sd->fd, out);
}

// ---------------------------------------------------------------- @spb party buffs
// The 2018 client has no buff icons in the party window; @spb lists the support buffs of every party member on this
// map server with the seconds left, so a supporter sees who needs what.

void og_map_spb(map_session_data* sd) {
	static const struct { sc_type sc; const char* name; } buffs[] = {
		{ SC_BLESSING, "Bless" }, { SC_INCREASEAGI, "AGI" }, { SC_ASSUMPTIO, "Assu" }, { SC_KYRIE, "Kyrie" },
		{ SC_IMPOSITIO, "Impo" }, { SC_SUFFRAGIUM, "Suffra" }, { SC_GLORIA, "Gloria" }, { SC_MAGNIFICAT, "Magni" },
		{ SC_ANGELUS, "Angelus" }, { SC_PROVIDENCE, "Provi" }, { SC_ADRENALINE, "AR" }, { SC_ADRENALINE2, "FAR" },
		{ SC_WEAPONPERFECTION, "WP" }, { SC_OVERTHRUST, "OT" }, { SC_WINDWALK, "WW" }, { SC_POEMBRAGI, "Bragi" },
		{ SC_APPLEIDUN, "Idun" }, { SC_ASSNCROS, "ACross" }, { SC_SERVICE4U, "Serv4U" }, { SC_FORTUNE, "Fortune" },
	};
	party_data* p = sd->status.party_id ? party_search(sd->status.party_id) : nullptr;
	if (p == nullptr) {
		clif_displaymessage(sd->fd, "You are not in a party.");
		return;
	}
	t_tick now = gettick();
	for (int32 i = 0; i < MAX_PARTY; i++) {
		map_session_data* m = p->data[i].sd;
		if (m == nullptr)
			continue;
		std::string line = std::string(m->status.name) + ":";
		int32 n = 0;
		for (const auto& b : buffs) {
			status_change_entry* e = m->sc.getSCE(b.sc);
			if (e == nullptr)
				continue;
			const TimerData* t = e->timer != INVALID_TIMER ? get_timer(e->timer) : nullptr;
			int32 left = t != nullptr ? (int32)std::max<t_tick>(0, DIFF_TICK(t->tick, now) / 1000) : -1;
			line += (n++ ? ", " : " ") + std::string(b.name) + (left >= 0 ? " " + std::to_string(left) + "s" : "");
		}
		if (n == 0)
			line += " no support buffs";
		if (m->m != sd->m)
			line += " (other map)";
		clif_displaymessage(sd->fd, line.substr(0, CHAT_SIZE_MAX - 1).c_str());
	}
}
