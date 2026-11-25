// OdinGuard on the map-server: events, kicks and bans, chat commands (!ping, !vsync, !crash), machine limit.
#ifndef OG_MAP_HPP
#define OG_MAP_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

void og_map_init();
/// @return true when the message was an OdinGuard command and must not be sent as chat
bool og_map_chat_command(map_session_data* sd, const char* message);
/// @return false when the machine already has the maximum number of characters online
bool og_map_machine_limit(map_session_data* sd);
/// bans the machine of an online character, @return false when the character has no OdinGuard session
bool og_map_ban(map_session_data* target, const char* by, const char* reason);
/// @return number of lifted bans of the machines the account used
int32 og_map_unban_account(uint32 account_id);
/// writes a short status of the character's guard into out
void og_map_info(map_session_data* target, char* out, size_t size);

// display features
struct block_list;
/// called by clif_parse_LoadEndAck before the character is spawned
void og_map_on_load(map_session_data* sd, bool first);
void og_map_on_quit(map_session_data* sd);
/// lite ground skill unit id for this viewer, 0 = normal display
uint16 og_map_lgp_unit(const map_session_data* viewer, uint16 skill_id);
/// marker for the creator id field of the unit packet (colour and shape for the client patch), 0 = none
uint32 og_map_lgp_marker(const map_session_data* viewer, uint16 skill_id);
/// sends buf to every player in the area of src for whom keep(viewer) is true
void og_map_send_area(const block_list& src, const void* buf, int32 len, bool (*keep)(map_session_data* viewer, const block_list& src, void* ctx), void* ctx);
/// lite cells for a ground skill without a unit (position effect only), sent to this viewer and removed again
void og_map_lgp_sound(map_session_data* viewer, const block_list& src, uint16 skill_id);
void og_map_lgp_flash(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y);
/// the same for a square of the given range around x,y (single-cell units with an area: Meteor Storm impacts ...)
void og_map_lgp_flash_area(map_session_data* viewer, const block_list& src, uint16 skill_id, uint16 skill_lv, int16 x, int16 y, int32 range, bool skip_center);
/// @lgp (kind 0), @square (1), @circle (2), @aoes (3); arg "", "on", "off" or a colour set (1 + og_lgp_color lines)
void og_map_gatdebug(map_session_data* sd, int32 radius);
void og_map_lgp_command(map_session_data* sd, int32 kind, const char* arg);
/// @namecolor: permanent name colour of a character (-1 = none)
void og_map_set_namecolor(map_session_data* sd, int32 rgb);

// server-side protection modules (og_map_guard.cpp)
#include <string>
#include <vector>
void og_map_guard_init();
void og_map_guard_quit(map_session_data* sd);
void og_map_flag(map_session_data* sd, const char* uid, const char* rule, int32 score, bool ban, const char* detail);
int32 og_map_banwave(bool dry, std::vector<std::string>* report);
void og_map_flags_text(uint32 account_id, std::vector<std::string>& out);
void og_map_packet(int32 fd, map_session_data* sd, uint16 cmd);
void og_map_skill_refused(map_session_data* sd);
void og_map_skill_used(map_session_data* sd);
void og_map_item_used(map_session_data* sd, int32 index);
void og_map_hp_update(map_session_data* sd);
void og_map_honeypot_spawn(map_session_data* sd);
bool og_map_honeypot_hit(map_session_data* sd, int32 target_id);
/// command registry: enabled, group, rate limit (og_cmd_*, og_cmdgroup_*, og_cmdrate_*)
bool og_map_command_allowed(map_session_data* sd, const char* id, int32 def_rate_ms);
const char* og_map_command_name(const char* id);


#endif /* OG_MAP_HPP */
