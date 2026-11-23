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
/// viewer does not want to see the auras (hat effects) of others
bool og_map_hides_auras(const map_session_data* viewer, const block_list& owner);
/// lite ground skill unit id for this viewer, 0 = normal display
uint16 og_map_lgp_unit(const map_session_data* viewer, uint16 skill_id);
/// sends buf to every player in the area of src for whom keep(viewer) is true
void og_map_send_area(const block_list& src, const void* buf, int32 len, bool (*keep)(map_session_data* viewer, const block_list& src, void* ctx), void* ctx);
/// @showauras, @lgp (player commands)
void og_map_toggle_auras(map_session_data* sd);
void og_map_toggle_lgp(map_session_data* sd);
/// @aura: sets the permanent aura of a character (0 = none), @return false for an unknown aura
bool og_map_set_aura(map_session_data* sd, int32 aura);
int32 og_map_aura_by_name(const char* name);
/// @namecolor: permanent name colour of a character (-1 = none)
void og_map_set_namecolor(map_session_data* sd, int32 rgb);

#endif /* OG_MAP_HPP */
