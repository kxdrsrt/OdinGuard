// OdinGuard server side: handshake, encrypted record layer and policy for client connections.
// Wire format: og_proto.h (shared with the OdinGuard client DLL).

#ifndef ODINGUARD_HPP
#define ODINGUARD_HPP

#include <string>
#include <utility>
#include <vector>

#include "cbasetypes.hpp"

enum e_og_action : uint8 {
	OG_ACTION_LOG = 0,
	OG_ACTION_KICK,
	OG_ACTION_BAN,
};

struct s_og_info {
	std::string unique_id; // hex
	std::string exe_code;  // hex, latest reported
	std::string build;
	uint8 flags = 0;
	uint8 version = 0;
	std::vector<std::pair<std::string, std::string>> components; // name, hex hash
};

void og_init();
void og_final();
bool og_enabled();
bool og_required();
bool og_log_only();
int32 og_ban_match();
int32 og_max_per_machine();

// state of a client connection
bool og_session_active(int32 fd);
const s_og_info* og_info(int32 fd);

// socket layer
bool og_handles_recv(int32 fd);
int32 og_recv(int32 fd);
bool og_handles_send(int32 fd);
int32 og_send(int32 fd);
bool og_has_pending(int32 fd);
void og_session_free(int32 fd);
void og_mark_plain(int32 fd);

// server -> client
bool og_send_ping(int32 fd);
bool og_send_control(int32 fd, const std::vector<uint8>& msg);
bool og_send_command(int32 fd, const char* command);

// callbacks set by the servers
typedef void (*og_event_handler)(int32 fd, const char* type, const char* detail, e_og_action action);
typedef void (*og_text_handler)(int32 fd, const char* text);
typedef void (*og_pong_handler)(int32 fd, int64 rtt_ms);
void og_set_handlers(og_event_handler ev, og_text_handler text, og_pong_handler pong);

// display features (map-server)
const std::vector<std::pair<int32, uint32>>& og_item_colors();       // item id, 0xRRGGBB
const std::vector<std::pair<int32, uint32>>& og_group_namecolors();  // group id, 0xRRGGBB
uint16 og_lgp_unit(uint16 skill_id);                                  // 0 = no lite display for the skill
const std::vector<std::pair<std::string, int32>>& og_auras();         // name, hat effect id (only these are allowed)

// CREATE TABLE IF NOT EXISTS statements (login and map server run them at start)
const std::vector<const char*>& og_sql_ddl();

#endif /* ODINGUARD_HPP */
