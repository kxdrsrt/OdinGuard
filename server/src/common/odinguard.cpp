// OdinGuard server side. See odinguard.hpp and og_proto.h.

#include "odinguard.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "../../3rdparty/monocypher/monocypher.h"

#include "og_proto.h"
#include "showmsg.hpp"
#include "socket.hpp"
#include "strlib.hpp"
#include "timer.hpp"

#define OG_CONF_FILE "conf/odinguard_athena.conf"
#define OG_CONF_IMPORT "conf/import/odinguard_conf.txt"
#define OG_RDATA_MAX (512 * 1024)
#define OG_OUT_MAX (8 * 1024 * 1024)

// socket.cpp
int32 socket_raw_recv(int32 fd, void* buf, size_t len);
int32 socket_raw_send(int32 fd, const void* buf, size_t len);
bool socket_would_block();

struct s_og_signature {
	uint16 id;
	uint8 kind;
	e_og_action action;
	std::string pattern;
};

static struct {
	bool enable = false;
	bool required = false;
	bool log_only = true;
	bool block_vm = false;
	bool input_filter = false;
	bool grf_strict = false;
	int32 max_windows = 0;
	int32 max_per_machine = 0;
	int32 alive_interval = 30;
	int32 alive_timeout = 120;
	int32 min_version = 1;
	int32 ban_match = 2;
	e_og_action integrity_action = OG_ACTION_KICK;
	e_og_action tamper_action = OG_ACTION_KICK;
	e_og_action grf_action = OG_ACTION_KICK;
	e_og_action inject_action = OG_ACTION_LOG;
	e_og_action vm_action = OG_ACTION_KICK;
	std::vector<std::string> allowed_exe;
	std::vector<s_og_signature> signatures;
	std::vector<std::pair<int32, uint32>> item_colors, group_colors;
	std::vector<std::pair<uint16, uint16>> lgp;
	std::vector<std::pair<std::string, int32>> auras;
	std::string key_hex;
	uint8 sk[64] = {};
	uint8 pk[32] = {};
} og_cfg;

struct s_og_state {
	std::vector<uint8> in, out;
	uint8 k_c2s[32] = {}, k_s2c[32] = {};
	uint64 ctr_c2s = 0, ctr_s2c = 0;
	bool active = false;
	bool timed_out = false;
	bool exe_reported = false;
	t_tick last_alive = 0;
	s_og_info info;
};

static og_event_handler og_on_event = nullptr;
static og_text_handler og_on_text = nullptr;
static og_pong_handler og_on_pong = nullptr;

enum e_og_mode : uint8 { OG_MODE_UNDECIDED = 0, OG_MODE_PLAIN, OG_MODE_OG };

static inline uint16 rd16(const uint8* p) { return (uint16)(p[0] | (p[1] << 8)); }
static inline uint32 rd32(const uint8* p) { return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24); }
static inline void wr16(uint8* p, uint16 v) { p[0] = (uint8)v; p[1] = (uint8)(v >> 8); }
static inline void wr32(uint8* p, uint32 v) { for (int32 i = 0; i < 4; i++) p[i] = (uint8)(v >> (8 * i)); }

static std::string to_hex(const uint8* p, size_t n) {
	static const char* x = "0123456789abcdef";
	std::string s(n * 2, '0');
	for (size_t i = 0; i < n; i++) { s[i * 2] = x[p[i] >> 4]; s[i * 2 + 1] = x[p[i] & 15]; }
	return s;
}

static bool from_hex(const std::string& s, uint8* out, size_t n) {
	if (s.size() != n * 2) return false;
	for (size_t i = 0; i < n; i++) {
		unsigned int v;
		if (sscanf(s.c_str() + i * 2, "%2x", &v) != 1) return false;
		out[i] = (uint8)v;
	}
	return true;
}

static bool og_random(uint8* p, size_t n) {
	FILE* f = fopen("/dev/urandom", "rb");
	if (f == nullptr) return false;
	bool ok = fread(p, 1, n, f) == n;
	fclose(f);
	return ok;
}

static void og_nonce(uint8 nonce[24], uint64 ctr) {
	memset(nonce, 0, 24);
	for (int32 i = 0; i < 8; i++) nonce[i] = (uint8)(ctr >> (8 * i));
}

static e_og_action og_parse_action(const char* v) {
	if (!strcmpi(v, "ban")) return OG_ACTION_BAN;
	if (!strcmpi(v, "kick")) return OG_ACTION_KICK;
	return OG_ACTION_LOG;
}

static uint8 og_parse_kind(const char* v) {
	if (!strcmpi(v, "window")) return OG_KIND_WINDOW;
	if (!strcmpi(v, "module")) return OG_KIND_MODULE;
	return OG_KIND_PROCESS;
}

static bool og_config_read(const char* file) {
	FILE* fp = fopen(file, "r");
	if (fp == nullptr) return false;
	char line[1024], w1[1024], w2[1024];
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '/' && line[1] == '/') continue;
		if (sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) != 2) continue;
		trim(w1);
		trim(w2);
		if (!strcmpi(w1, "og_enable")) og_cfg.enable = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_required")) og_cfg.required = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_log_only")) og_cfg.log_only = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_block_vm")) og_cfg.block_vm = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_input_filter")) og_cfg.input_filter = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_grf_strict")) og_cfg.grf_strict = config_switch(w2) != 0;
		else if (!strcmpi(w1, "og_max_windows")) og_cfg.max_windows = atoi(w2);
		else if (!strcmpi(w1, "og_max_per_machine")) og_cfg.max_per_machine = atoi(w2);
		else if (!strcmpi(w1, "og_alive_interval")) og_cfg.alive_interval = std::max(5, atoi(w2));
		else if (!strcmpi(w1, "og_alive_timeout")) og_cfg.alive_timeout = std::max(15, atoi(w2));
		else if (!strcmpi(w1, "og_min_version")) og_cfg.min_version = atoi(w2);
		else if (!strcmpi(w1, "og_ban_match")) og_cfg.ban_match = std::max(1, atoi(w2));
		else if (!strcmpi(w1, "og_integrity_action")) og_cfg.integrity_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_tamper_action")) og_cfg.tamper_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_action")) og_cfg.grf_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_inject_action")) og_cfg.inject_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_vm_action")) og_cfg.vm_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_key")) og_cfg.key_hex = w2;
		else if (!strcmpi(w1, "og_allowed_exe")) og_cfg.allowed_exe.push_back(w2);
		else if (!strcmpi(w1, "og_clear_signatures")) { if (config_switch(w2)) og_cfg.signatures.clear(); }
		else if (!strcmpi(w1, "og_signature")) {
			// kind,action,pattern
			char kind[32], action[32], pattern[256];
			if (sscanf(w2, "%31[^,],%31[^,],%255[^\r\n]", kind, action, pattern) == 3 && og_cfg.signatures.size() < 1000) {
				s_og_signature sig;
				sig.id = (uint16)(og_cfg.signatures.size() + 1);
				sig.kind = og_parse_kind(kind);
				sig.action = og_parse_action(action);
				sig.pattern = pattern;
				og_cfg.signatures.push_back(sig);
			}
		}
		else if (!strcmpi(w1, "og_item_color") || !strcmpi(w1, "og_group_namecolor")) {
			int32 id;
			unsigned int rgb;
			if (sscanf(w2, "%d , %x", &id, &rgb) == 2)
				(w1[3] == 'i' ? og_cfg.item_colors : og_cfg.group_colors).emplace_back(id, rgb & 0xFFFFFF);
		}
		else if (!strcmpi(w1, "og_aura")) {
			char name[32];
			int32 id;
			if (sscanf(w2, "%31[^,], %d", name, &id) == 2) {
				trim(name);
				og_cfg.auras.erase(std::remove_if(og_cfg.auras.begin(), og_cfg.auras.end(), [&name](const auto& e) { return !strcmpi(e.first.c_str(), name); }), og_cfg.auras.end());
				if (id > 0) og_cfg.auras.emplace_back(name, id);
			}
		}
		else if (!strcmpi(w1, "og_lgp")) {
			unsigned int skill, unit;
			if (sscanf(w2, "%i , %i", &skill, &unit) == 2) {
				og_cfg.lgp.erase(std::remove_if(og_cfg.lgp.begin(), og_cfg.lgp.end(), [skill](const auto& e) { return e.first == skill; }), og_cfg.lgp.end());
				if (unit != 0) og_cfg.lgp.emplace_back((uint16)skill, (uint16)unit);
			}
		}
		else if (!strcmpi(w1, "import")) og_config_read(w2);
	}
	fclose(fp);
	return true;
}

// the import file is created at first start, so the server owner finds the switches without reading the docs
static void og_config_bootstrap() {
	FILE* fp = fopen(OG_CONF_IMPORT, "r");
	if (fp != nullptr) { fclose(fp); return; }
	fp = fopen(OG_CONF_IMPORT, "w");
	if (fp == nullptr) return;
	fputs("// OdinGuard local settings (created automatically). See conf/odinguard_athena.conf.\n"
		"//og_enable: yes\n"
		"//og_key: <64 hex characters, or set the ODINGUARD_KEY environment variable>\n", fp);
	fclose(fp);
	ShowInfo("OdinGuard: created '" CL_WHITE OG_CONF_IMPORT CL_RESET "'.\n");
}

bool og_enabled() { return og_cfg.enable; }
bool og_required() { return og_cfg.enable && og_cfg.required; }
bool og_log_only() { return og_cfg.log_only; }
int32 og_ban_match() { return og_cfg.ban_match; }
int32 og_max_per_machine() { return og_cfg.max_per_machine; }

static s_og_state* og_state(int32 fd) {
	return session_isValid(fd) ? static_cast<s_og_state*>(session[fd]->og) : nullptr;
}

bool og_session_active(int32 fd) {
	s_og_state* o = og_state(fd);
	return o != nullptr && o->active;
}

const s_og_info* og_info(int32 fd) {
	s_og_state* o = og_state(fd);
	return (o != nullptr && o->active) ? &o->info : nullptr;
}

void og_set_handlers(og_event_handler ev, og_text_handler text, og_pong_handler pong) {
	og_on_event = ev;
	og_on_text = text;
	og_on_pong = pong;
}

static void og_event(int32 fd, const char* type, const char* detail, e_og_action action) {
	if (og_cfg.log_only && action != OG_ACTION_LOG) action = OG_ACTION_LOG;
	uint32 ip = session_isValid(fd) ? session[fd]->client_addr : 0;
	ShowWarning("OdinGuard: %s (%s) fd %d ip %u.%u.%u.%u%s\n", type, detail, fd, CONVIP(ip),
		action == OG_ACTION_BAN ? " -> ban" : action == OG_ACTION_KICK ? " -> kick" : "");
	if (og_on_event != nullptr)
		og_on_event(fd, type, detail, action);
	else if (action != OG_ACTION_LOG)
		set_eof(fd);
}

static void og_push_plain(int32 fd, const uint8* p, size_t n) {
	socket_data* s = session[fd];
	if (n == 0) return;
	if (RFIFOSPACE(fd) < n) {
		size_t want = s->rdata_size + n;
		if (want > OG_RDATA_MAX) { set_eof(fd); return; }
		size_t size = s->max_rdata;
		while (size < want) size *= 2;
		realloc_fifo(fd, (uint32)size, (uint32)s->max_wdata);
		if (RFIFOSPACE(fd) < n) { set_eof(fd); return; }
	}
	memcpy(s->rdata + s->rdata_size, p, n);
	s->rdata_size += n;
}

// appends one or more encrypted records to the output buffer
static void og_seal(int32 fd, s_og_state* o, uint8 type, const uint8* p, size_t n) {
	std::vector<uint8> plain;
	do {
		size_t chunk = std::min(n, (size_t)OG_RECORD_MAX);
		plain.resize(chunk + 1);
		plain[0] = type;
		if (chunk) memcpy(&plain[1], p, chunk);
		size_t pos = o->out.size();
		o->out.resize(pos + 2 + 16 + plain.size());
		uint8* r = &o->out[pos];
		wr16(r, (uint16)plain.size());
		uint8 nonce[24];
		og_nonce(nonce, o->ctr_s2c++);
		crypto_aead_lock(r + 18, r + 2, o->k_s2c, nonce, r, 2, plain.data(), plain.size());
		p += chunk;
		n -= chunk;
	} while (n > 0);
	if (o->out.size() > OG_OUT_MAX) { o->out.clear(); set_eof(fd); }
#ifdef SEND_SHORTLIST
	send_shortlist_add_fd(fd);
#endif
}

static void og_control(int32 fd, s_og_state* o, const std::vector<uint8>& msg) {
	og_seal(fd, o, OG_REC_CONTROL, msg.data(), msg.size());
}

static void og_send_policy(int32 fd, s_og_state* o) {
	std::vector<uint8> m = { OG_S_POLICY, (uint8)og_cfg.block_vm, (uint8)og_cfg.max_windows, 0, 0, (uint8)og_cfg.input_filter, (uint8)og_cfg.grf_strict, 0, 0 };
	wr16(&m[3], (uint16)og_cfg.alive_interval);
	wr16(&m[7], (uint16)og_cfg.signatures.size());
	for (const s_og_signature& sig : og_cfg.signatures) {
		size_t len = std::min(sig.pattern.size(), (size_t)255);
		size_t pos = m.size();
		m.resize(pos + 4 + len);
		wr16(&m[pos], sig.id);
		m[pos + 2] = sig.kind;
		m[pos + 3] = (uint8)len;
		memcpy(&m[pos + 4], sig.pattern.data(), len);
	}
	og_control(fd, o, m);
}

bool og_send_control(int32 fd, const std::vector<uint8>& msg) {
	s_og_state* o = og_state(fd);
	if (o == nullptr || !o->active || msg.empty()) return false;
	og_control(fd, o, msg);
	return true;
}

const std::vector<std::pair<int32, uint32>>& og_item_colors() { return og_cfg.item_colors; }
const std::vector<std::pair<int32, uint32>>& og_group_namecolors() { return og_cfg.group_colors; }

const std::vector<std::pair<std::string, int32>>& og_auras() { return og_cfg.auras; }

uint16 og_lgp_unit(uint16 skill_id) {
	if (!og_cfg.enable) return 0;
	for (const auto& e : og_cfg.lgp)
		if (e.first == skill_id) return e.second;
	return 0;
}

bool og_send_ping(int32 fd) {
	s_og_state* o = og_state(fd);
	if (o == nullptr || !o->active) return false;
	std::vector<uint8> m(5);
	m[0] = OG_S_PING;
	wr32(&m[1], (uint32)gettick());
	og_control(fd, o, m);
	return true;
}

bool og_send_command(int32 fd, const char* command) {
	s_og_state* o = og_state(fd);
	if (o == nullptr || !o->active) return false;
	std::vector<uint8> m(1 + strlen(command));
	m[0] = OG_S_COMMAND;
	memcpy(&m[1], command, m.size() - 1);
	og_control(fd, o, m);
	return true;
}

static const s_og_signature* og_find_signature(uint16 id) {
	for (const s_og_signature& sig : og_cfg.signatures)
		if (sig.id == id) return &sig;
	return nullptr;
}

static void og_handle_control(int32 fd, s_og_state* o, const uint8* p, size_t n) {
	if (n < 1) return;
	switch (p[0]) {
	case OG_C_ALIVE: {
		if (n < 1 + 4 + 32 + 2 + 1) return;
		o->last_alive = gettick();
		o->timed_out = false;
		o->info.exe_code = to_hex(p + 5, 32);
		uint8 flags = p[39];
		if (!og_cfg.allowed_exe.empty() && !o->exe_reported) {
			bool ok = false;
			for (const std::string& h : og_cfg.allowed_exe)
				if (h == o->info.exe_code) { ok = true; break; }
			if (!ok) {
				o->exe_reported = true;
				og_event(fd, "exe-code", o->info.exe_code.c_str(), og_cfg.integrity_action);
			}
		}
		if (flags & OG_ALIVE_HOOKS_TAMPERED) og_event(fd, "hooks-tampered", "network hooks replaced", og_cfg.tamper_action);
		if (flags & OG_ALIVE_DEBUGGER) og_event(fd, "debugger", "debugger attached", og_cfg.tamper_action);
		if (flags & OG_ALIVE_GRF_VIOLATION) og_event(fd, "grf", "protected resource overridden", og_cfg.grf_action);
		break;
	}
	case OG_C_DETECT: {
		if (n < 4) return;
		uint16 id = rd16(p + 1);
		uint8 kind = p[3];
		std::string text((const char*)p + 4, n - 4);
		const s_og_signature* sig = og_find_signature(id);
		e_og_action action = OG_ACTION_LOG;
		std::string type = "detect";
		if (sig != nullptr) { action = sig->action; type += ":" + sig->pattern; }
		else if (kind == OG_KIND_GRF) { action = og_cfg.grf_action; type += ":grf"; }
		else if (kind == OG_KIND_INJECT) { action = og_cfg.inject_action; type += ":module"; }
		else if (kind == OG_KIND_INPUT) { type += ":input"; }
		og_event(fd, type.c_str(), text.c_str(), action);
		break;
	}
	case OG_C_PONG:
		if (n >= 5 && og_on_pong != nullptr)
			og_on_pong(fd, (int64)((uint32)gettick() - rd32(p + 1)));
		break;
	case OG_C_REPORT:
		if (og_on_text != nullptr) {
			std::string text((const char*)p + 1, std::min(n - 1, (size_t)255));
			og_on_text(fd, text.c_str());
		}
		break;
	}
}

// returns true when the handshake is complete
static bool og_handle_hello(int32 fd, s_og_state* o) {
	if (o->in.size() < 4) return false;
	const uint8* p = o->in.data();
	uint16 len = rd16(p + 2);
	if (len < OG_CLIENT_HELLO_FIXED || len > OG_HELLO_MAX) { set_eof(fd); return false; }
	if (o->in.size() < len) return false;
	uint8 n = p[OG_CLIENT_HELLO_FIXED - 1];
	if (n > OG_MAX_COMPONENTS || len != OG_CLIENT_HELLO_FIXED + n * (OG_COMPONENT_NAME + 32)) { set_eof(fd); return false; }

	o->info.version = p[4];
	o->info.flags = p[5];
	const uint8* c_pub = p + 6;
	const uint8* nonce_c = p + 38;
	o->info.unique_id = to_hex(p + 70, 32);
	o->info.exe_code = to_hex(p + 102, 32);
	o->info.build.assign((const char*)p + 134, strnlen((const char*)p + 134, 16));
	for (int32 i = 0; i < n; i++) {
		const uint8* c = p + OG_CLIENT_HELLO_FIXED + i * (OG_COMPONENT_NAME + 32);
		std::string name((const char*)c, strnlen((const char*)c, OG_COMPONENT_NAME));
		o->info.components.emplace_back(name, to_hex(c + OG_COMPONENT_NAME, 32));
	}

	uint8 status = o->info.version < og_cfg.min_version ? OG_STATUS_OUTDATED : OG_STATUS_OK;
	uint8 e_sec[32], e_pub[32], nonce_s[32], shared[32];
	if (!og_random(e_sec, sizeof(e_sec)) || !og_random(nonce_s, sizeof(nonce_s))) {
		ShowError("OdinGuard: no random source\n");
		set_eof(fd);
		return false;
	}
	crypto_x25519_public_key(e_pub, e_sec);
	crypto_x25519(shared, e_sec, c_pub);
	crypto_wipe(e_sec, sizeof(e_sec));
	static const uint8 zero[32] = {};
	if (crypto_verify32(shared, zero) == 0) { set_eof(fd); return false; }

	const char* message = status == OG_STATUS_OUTDATED ? "Please update your client (OdinGuard is outdated)." : "";
	std::vector<uint8> sh(OG_SERVER_HELLO_FIXED + strlen(message));
	wr16(&sh[0], OG_HELLO_ID);
	wr16(&sh[2], (uint16)sh.size());
	sh[4] = OG_PROTO_VERSION;
	sh[5] = status;
	memcpy(&sh[6], e_pub, 32);
	memcpy(&sh[38], nonce_s, 32);
	memcpy(&sh[OG_SERVER_HELLO_FIXED], message, strlen(message));

	uint8 transcript[64];
	crypto_blake2b_ctx bc;
	crypto_blake2b_init(&bc, 64);
	crypto_blake2b_update(&bc, p, len);
	crypto_blake2b_update(&bc, sh.data(), 70);
	crypto_blake2b_final(&bc, transcript);
	crypto_eddsa_sign(&sh[70], og_cfg.sk, transcript, sizeof(transcript));

	uint8 keys[64];
	static const char label[] = "OdinGuard/v1";
	crypto_blake2b_init(&bc, 64);
	crypto_blake2b_update(&bc, (const uint8*)label, sizeof(label) - 1);
	crypto_blake2b_update(&bc, shared, 32);
	crypto_blake2b_update(&bc, c_pub, 32);
	crypto_blake2b_update(&bc, e_pub, 32);
	crypto_blake2b_update(&bc, nonce_c, 32);
	crypto_blake2b_update(&bc, nonce_s, 32);
	crypto_blake2b_final(&bc, keys);
	memcpy(o->k_c2s, keys, 32);
	memcpy(o->k_s2c, keys + 32, 32);
	crypto_wipe(keys, sizeof(keys));
	crypto_wipe(shared, sizeof(shared));

	o->out.insert(o->out.end(), sh.begin(), sh.end());
	o->in.erase(o->in.begin(), o->in.begin() + len);
#ifdef SEND_SHORTLIST
	send_shortlist_add_fd(fd);
#endif
	if (status != OG_STATUS_OK) {
		set_eof(fd);
		return false;
	}
	o->active = true;
	o->last_alive = gettick();
	og_send_policy(fd, o);
	if ((o->info.flags & OG_FLAG_VM) && og_cfg.block_vm)
		og_event(fd, "vm", "virtual machine", og_cfg.vm_action);
	return true;
}

static void og_process_records(int32 fd, s_og_state* o) {
	size_t pos = 0;
	std::vector<uint8> plain;
	while (o->in.size() - pos >= 2 && !session[fd]->flag.eof) {
		const uint8* r = o->in.data() + pos;
		uint16 blen = rd16(r);
		if (blen == 0) { set_eof(fd); break; }
		if (o->in.size() - pos < (size_t)18 + blen) break;
		plain.resize(blen);
		uint8 nonce[24];
		og_nonce(nonce, o->ctr_c2s);
		if (crypto_aead_unlock(plain.data(), r + 2, o->k_c2s, nonce, r, 2, r + 18, blen) != 0) {
			og_event(fd, "record-mac", "tampered or injected packet", OG_ACTION_KICK);
			set_eof(fd);
			break;
		}
		o->ctr_c2s++;
		pos += 18 + blen;
		if (plain[0] == OG_REC_GAME) og_push_plain(fd, &plain[1], blen - 1);
		else if (plain[0] == OG_REC_CONTROL) og_handle_control(fd, o, &plain[1], blen - 1);
		else { set_eof(fd); break; }
	}
	if (pos) o->in.erase(o->in.begin(), o->in.begin() + pos);
}

bool og_handles_recv(int32 fd) {
	if (!og_cfg.enable || !session_isValid(fd)) return false;
	socket_data* s = session[fd];
	return s->og_mode != OG_MODE_PLAIN && !s->flag.server;
}

void og_mark_plain(int32 fd) {
	if (session_isValid(fd)) session[fd]->og_mode = OG_MODE_PLAIN;
}

int32 og_recv(int32 fd) {
	static uint8 buf[65536];
	socket_data* s = session[fd];
	size_t want = sizeof(buf);
	if (s->og_mode == OG_MODE_UNDECIDED) want = std::min(want, (size_t)RFIFOSPACE(fd));

	int32 len = socket_raw_recv(fd, buf, want);
	if (len < 0) {
		if (!socket_would_block()) set_eof(fd);
		return 0;
	}
	if (len == 0) {
		set_eof(fd);
		return 0;
	}
	s->rdata_tick = last_tick;

	if (s->og == nullptr) s->og = new s_og_state();
	s_og_state* o = static_cast<s_og_state*>(s->og);
	o->in.insert(o->in.end(), buf, buf + len);

	if (s->og_mode == OG_MODE_UNDECIDED) {
		if (o->in.size() < 2) return 0;
		if (rd16(o->in.data()) != OG_HELLO_ID) {
			s->og_mode = OG_MODE_PLAIN;
			og_push_plain(fd, o->in.data(), o->in.size());
			og_session_free(fd);
			return 0;
		}
		s->og_mode = OG_MODE_OG;
	}
	if (!o->active && !og_handle_hello(fd, o)) return 0;
	og_process_records(fd, o);
	return 0;
}

bool og_handles_send(int32 fd) {
	return session_isValid(fd) && session[fd]->og_mode == OG_MODE_OG && session[fd]->og != nullptr;
}

int32 og_send(int32 fd) {
	socket_data* s = session[fd];
	s_og_state* o = static_cast<s_og_state*>(s->og);
	if (o->active && s->wdata_size > 0) {
		og_seal(fd, o, OG_REC_GAME, s->wdata, s->wdata_size);
		s->wdata_size = 0;
	}
	if (o->out.empty()) return 0;
	int32 len = socket_raw_send(fd, o->out.data(), o->out.size());
	if (len < 0) {
		if (!socket_would_block()) {
			o->out.clear();
			s->wdata_size = 0;
			set_eof(fd);
		}
		return 0;
	}
	if (len > 0) {
		s->wdata_tick = last_tick;
		o->out.erase(o->out.begin(), o->out.begin() + len);
	}
	return 0;
}

bool og_has_pending(int32 fd) {
	s_og_state* o = og_state(fd);
	return o != nullptr && !o->out.empty();
}

void og_session_free(int32 fd) {
	if (session[fd] == nullptr || session[fd]->og == nullptr) return;
	s_og_state* o = static_cast<s_og_state*>(session[fd]->og);
	crypto_wipe(o->k_c2s, sizeof(o->k_c2s));
	crypto_wipe(o->k_s2c, sizeof(o->k_s2c));
	delete o;
	session[fd]->og = nullptr;
}

static TIMER_FUNC(og_alive_check) {
	t_tick now = gettick();
	for (int32 fd = 1; fd < fd_max; fd++) {
		s_og_state* o = og_state(fd);
		if (o == nullptr || !o->active || o->timed_out || session[fd]->flag.eof) continue;
		if (DIFF_TICK(now, o->last_alive) > og_cfg.alive_timeout * 1000) {
			o->timed_out = true;
			og_event(fd, "alive-timeout", "no heartbeat from the client guard", OG_ACTION_KICK);
		}
	}
	return 0;
}

const std::vector<const char*>& og_sql_ddl() {
	static const std::vector<const char*> ddl = {
		"CREATE TABLE IF NOT EXISTS `og_client` ("
		" `unique_id` CHAR(64) NOT NULL, `account_id` INT UNSIGNED NOT NULL DEFAULT 0,"
		" `first_seen` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, `last_seen` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		" `last_ip` VARCHAR(45) NOT NULL DEFAULT '', `build` VARCHAR(32) NOT NULL DEFAULT '', `exe_code` CHAR(64) NOT NULL DEFAULT '',"
		" PRIMARY KEY (`unique_id`, `account_id`), KEY `account_id` (`account_id`)) ENGINE=InnoDB",
		"CREATE TABLE IF NOT EXISTS `og_component` ("
		" `unique_id` CHAR(64) NOT NULL, `name` VARCHAR(16) NOT NULL, `hash` CHAR(64) NOT NULL,"
		" PRIMARY KEY (`unique_id`, `name`), KEY `hash` (`hash`)) ENGINE=InnoDB",
		"CREATE TABLE IF NOT EXISTS `og_ban` ("
		" `id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `unique_id` CHAR(64) NOT NULL,"
		" `reason` VARCHAR(255) NOT NULL DEFAULT '', `banned_by` VARCHAR(24) NOT NULL DEFAULT '',"
		" `created` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, `expires` DATETIME NULL,"
		" PRIMARY KEY (`id`), KEY `unique_id` (`unique_id`)) ENGINE=InnoDB",
		"CREATE TABLE IF NOT EXISTS `og_event` ("
		" `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, `time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		" `unique_id` CHAR(64) NOT NULL DEFAULT '', `account_id` INT UNSIGNED NOT NULL DEFAULT 0, `char_name` VARCHAR(24) NOT NULL DEFAULT '',"
		" `type` VARCHAR(64) NOT NULL, `detail` VARCHAR(255) NOT NULL DEFAULT '', `action` VARCHAR(8) NOT NULL DEFAULT '',"
		" PRIMARY KEY (`id`), KEY `unique_id` (`unique_id`), KEY `account_id` (`account_id`)) ENGINE=InnoDB",
	};
	return ddl;
}

void og_init() {
	og_config_bootstrap();
	if (!og_config_read(OG_CONF_FILE))
		return;
	const char* env = getenv("ODINGUARD_KEY");
	if (env != nullptr && *env) og_cfg.key_hex = env;
	if (!og_cfg.enable) return;

	uint8 seed[32];
	if (!from_hex(og_cfg.key_hex, seed, sizeof(seed))) {
		ShowError("OdinGuard: og_key / ODINGUARD_KEY missing or invalid (64 hex characters). OdinGuard disabled.\n");
		og_cfg.enable = false;
		return;
	}
	crypto_eddsa_key_pair(og_cfg.sk, og_cfg.pk, seed);
	add_timer_func_list(og_alive_check, "og_alive_check");
	add_timer_interval(gettick() + 10000, og_alive_check, 0, 0, 10000);
	ShowStatus("OdinGuard: enabled (required: %s, log only: %s, %d signatures), server key %s\n",
		og_cfg.required ? "yes" : "no", og_cfg.log_only ? "yes" : "no", (int32)og_cfg.signatures.size(), to_hex(og_cfg.pk, 32).c_str());
}

void og_final() {
	crypto_wipe(og_cfg.sk, sizeof(og_cfg.sk));
}
