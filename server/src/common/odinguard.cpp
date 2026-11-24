// OdinGuard Anti Cheat – server side. See odinguard.hpp, og_proto.h and og_wire.h.

#include "odinguard.hpp"

#ifdef _WIN32
#include "winapi.hpp"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <tuple>

#include "../../3rdparty/monocypher/monocypher.h"

#include "og_proto.h"
#include "og_wire.h"
#include "showmsg.hpp"
#include "socket.hpp"
#include "strlib.hpp"
#include "timer.hpp"

#define OG_CONF_FILE "conf/odinguard_athena.conf"
#define OG_CONF_IMPORT "conf/import/odinguard_conf.txt"
#define OG_RDATA_MAX (512 * 1024)
#define OG_OUT_MAX (8 * 1024 * 1024)
#define OG_SIGS_PER_MESSAGE 400
#define OG_CHALLENGE_TIMEOUT 60000
#define OG_CODE_RANGE_MIN 4096
#define OG_CODE_RANGE_MAX 65536

// socket.cpp
int32 socket_raw_recv(int32 fd, void* buf, size_t len);
int32 socket_raw_send(int32 fd, const void* buf, size_t len);
bool socket_would_block();

struct s_og_signature {
	uint16 id;
	uint8 kind;
	e_og_action action;
	int32 score;
	std::string pattern;
	std::string name; // stable id from the signature package (e.g. OG-RCX-001)
};

struct s_og_challenge_cfg {
	bool enable = false;
	int32 interval = 600; // seconds
	e_og_action action = OG_ACTION_FLAG;
};

struct s_og_file {
	uint8 index;
	bool required;
	std::string path;
};

struct s_og_lgp_color { std::string name; int32 index; }; // index: colour number in the client patch (og_lgp.c)
struct s_og_lgp { std::string style; uint16 skill, unit; int32 color; }; // color: position in lgp_colors, -1 = fixed unit

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
	int32 min_version = OG_PROTO_MIN;
	int32 ban_match = 2;
	int32 time_drift = 6;
	e_og_action integrity_action = OG_ACTION_KICK;
	e_og_action tamper_action = OG_ACTION_KICK;
	e_og_action grf_action = OG_ACTION_KICK;
	e_og_action grf_modified_action = OG_ACTION_KICK;
	e_og_action grf_unknown_action = OG_ACTION_LOG;
	e_og_action grf_missing_action = OG_ACTION_LOG;
	std::vector<s_og_signature> grf_manifest; // kinds GRF_LIST, GRF_MOD, OVERLAY (sent after the signatures)
	e_og_action inject_action = OG_ACTION_LOG;
	e_og_action vm_action = OG_ACTION_KICK;
	std::vector<std::string> allowed_exe;
	std::vector<s_og_signature> signatures;
	std::vector<std::pair<int32, uint32>> item_colors, group_colors;
	std::vector<s_og_lgp_color> lgp_colors;
	std::vector<s_og_lgp> lgp;
	std::vector<uint16> lgp_attack; // hostile skills: triangles instead of squares
	std::vector<std::pair<std::string, std::string>> switches; // module switches and other plain values
	s_og_challenge_cfg ch[5]; // index = OG_CH_*
	std::vector<s_og_file> files;
	std::string key_hex, salt_hex, secret_hex, sigpack, sigpack_key, guard_file, exe_reference;
	uint32 sigpack_min_version = 0, sigpack_version = 0;
	uint8 sk[64] = {};
	uint8 pk[32] = {};
	uint8 salt[OG_SALT_LEN] = {};
	uint8 tag[OG_TAG_LEN] = {};
	uint8 secret[32] = {};
	std::string guard_code; // hex BLAKE2b of the canonical guard image
	std::vector<uint8> guard_image, exe_image;
} og_cfg;

struct s_og_pending {
	uint32 id;
	uint8 type, target;
	uint8 nonce[16];
	uint32 a, b;
	t_tick sent;
};

struct s_og_state {
	std::vector<uint8> in, out;
	uint8 k_c2s[32] = {}, k_s2c[32] = {};
	uint64 ctr_c2s = 0, ctr_s2c = 0;
	bool active = false;
	bool timed_out = false;
	bool exe_reported = false;
	bool guard_reported = false;
	t_tick last_alive = 0;
	s_og_info info;
	// challenges
	uint32 next_id = 1;
	t_tick next_at[5] = {};
	std::vector<s_og_pending> pending;
	bool time_base = false;
	bool time_reported = false;
	t_tick base_server = 0;
	uint32 base_tick = 0, base_qpc = 0;
};

static og_event_handler og_on_event = nullptr;
static og_text_handler og_on_text = nullptr;
static og_pong_handler og_on_pong = nullptr;

enum e_og_mode : uint8 { OG_MODE_UNDECIDED = 0, OG_MODE_PLAIN, OG_MODE_OG };

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
#ifdef _WIN32
	return BCryptGenRandom(nullptr, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
	static FILE* f = nullptr;
	if (f == nullptr && (f = fopen("/dev/urandom", "rb")) == nullptr) return false;
	return fread(p, 1, n, f) == n;
#endif
}

static uint32 og_random32() {
	uint8 b[4] = {};
	og_random(b, 4);
	return og_rd32(b);
}

static void og_nonce(uint8 nonce[24], uint64 ctr) {
	memset(nonce, 0, 24);
	for (int32 i = 0; i < 8; i++) nonce[i] = (uint8)(ctr >> (8 * i));
}

// key rotation, see og_proto.h
static void og_rekey(uint8 key[32], uint64 ctr) {
	static const char label[] = "OdinGuard/rekey";
	if (ctr == 0 || ctr % OG_REKEY_RECORDS) return;
	uint8 next[32];
	crypto_blake2b_ctx c;
	crypto_blake2b_init(&c, 32);
	crypto_blake2b_update(&c, key, 32);
	crypto_blake2b_update(&c, (const uint8*)label, sizeof(label) - 1);
	crypto_blake2b_final(&c, next);
	memcpy(key, next, 32);
	crypto_wipe(next, 32);
}

const char* og_action_name(e_og_action a) {
	static const char* names[] = { "log", "kick", "ban", "flag", "flagban" };
	return a <= OG_ACTION_FLAGBAN ? names[a] : "log";
}

static e_og_action og_parse_action(const char* v) {
	if (!strcmpi(v, "ban")) return OG_ACTION_BAN;
	if (!strcmpi(v, "kick")) return OG_ACTION_KICK;
	if (!strcmpi(v, "flag")) return OG_ACTION_FLAG;
	if (!strcmpi(v, "flagban")) return OG_ACTION_FLAGBAN;
	return OG_ACTION_LOG;
}

static uint8 og_parse_kind(const char* v) {
	if (!strcmpi(v, "window")) return OG_KIND_WINDOW;
	if (!strcmpi(v, "module")) return OG_KIND_MODULE;
	if (!strcmpi(v, "window_class")) return OG_KIND_WINDOW_CLASS;
	if (!strcmpi(v, "file_name")) return OG_KIND_FILE_NAME;
	if (!strcmpi(v, "file_sha256")) return OG_KIND_FILE_SHA256;
	if (!strcmpi(v, "driver")) return OG_KIND_DRIVER;
	if (!strcmpi(v, "grf_sha256")) return OG_KIND_GRF_SHA256;
	if (!strcmpi(v, "mutex")) return OG_KIND_MUTEX;
	if (!strcmpi(v, "registry")) return OG_KIND_REGISTRY;
	if (!strcmpi(v, "allow_sha256")) return OG_KIND_ALLOW_SHA256;
	if (!strcmpi(v, "allow_path")) return OG_KIND_ALLOW_PATH;
	return OG_KIND_PROCESS;
}

// default flag score of an action (signatures carry their own score from severity x confidence)
static int32 og_action_score(e_og_action a) {
	switch (a) {
	case OG_ACTION_BAN: return 100;
	case OG_ACTION_FLAGBAN: return 50;
	case OG_ACTION_FLAG: return 25;
	default: return 0;
	}
}

static int32 og_challenge_index(const char* name) {
	if (!strcmpi(name, "nonce")) return OG_CH_NONCE;
	if (!strcmpi(name, "code")) return OG_CH_CODE_RANGE;
	if (!strcmpi(name, "time")) return OG_CH_TIME;
	if (!strcmpi(name, "file")) return OG_CH_FILE_RANGE;
	return 0;
}

// a later line (import file) replaces an earlier one
static void og_switch_set(const char* key, const char* value) {
	og_cfg.switches.erase(std::remove_if(og_cfg.switches.begin(), og_cfg.switches.end(), [key](const auto& e) { return !strcmpi(e.first.c_str(), key); }), og_cfg.switches.end());
	og_cfg.switches.emplace_back(key, value);
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
		else if (!strcmpi(w1, "og_min_version")) og_cfg.min_version = std::max(OG_PROTO_MIN, atoi(w2));
		else if (!strcmpi(w1, "og_ban_match")) og_cfg.ban_match = std::max(1, atoi(w2));
		else if (!strcmpi(w1, "og_integrity_action")) og_cfg.integrity_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_tamper_action")) og_cfg.tamper_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_action")) og_cfg.grf_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_modified_action")) og_cfg.grf_modified_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_unknown_action")) og_cfg.grf_unknown_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_missing_action")) og_cfg.grf_missing_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_grf_file") || !strcmpi(w1, "og_grf_mod") || !strcmpi(w1, "og_grf_overlay")) {
			// og_grf_file: name,size,tail sha256,full sha256 or -,yes|no (required)
			// og_grf_mod: tail sha256,note      og_grf_overlay: data\folder\ (allowed loose files)
			s_og_signature e;
			e.id = 0;
			e.action = OG_ACTION_LOG;
			e.score = 0;
			std::string v = w2;
			if (!strcmpi(w1, "og_grf_overlay")) {
				e.kind = OG_KIND_OVERLAY;
				e.pattern = v;
			} else if (!strcmpi(w1, "og_grf_mod")) {
				e.kind = OG_KIND_GRF_MOD;
				size_t c = v.find(',');
				e.pattern = v.substr(0, c) + "|" + (c == std::string::npos ? "" : v.substr(c + 1));
			} else {
				e.kind = OG_KIND_GRF_LIST;
				std::vector<std::string> f;
				size_t a = 0, b;
				while ((b = v.find(',', a)) != std::string::npos) { f.push_back(v.substr(a, b - a)); a = b + 1; }
				f.push_back(v.substr(a));
				if (f.size() != 5 || f[2].size() != 64) { ShowWarning("odinguard: bad og_grf_file line '%s'\n", w2); continue; }
				e.pattern = f[0] + "|" + f[1] + "|" + f[2] + "|" + f[3] + "|" + (config_switch(f[4].c_str()) ? "r" : "o");
			}
			if (e.pattern.size() > 255) { ShowWarning("odinguard: %s too long\n", w1); continue; }
			og_cfg.grf_manifest.push_back(e);
		}
		else if (!strcmpi(w1, "og_inject_action")) og_cfg.inject_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_vm_action")) og_cfg.vm_action = og_parse_action(w2);
		else if (!strcmpi(w1, "og_key")) og_cfg.key_hex = w2;
		else if (!strcmpi(w1, "og_proto_salt")) og_cfg.salt_hex = w2;
		else if (!strcmpi(w1, "og_build_secret")) og_cfg.secret_hex = w2;
		else if (!strcmpi(w1, "og_sigpack")) og_cfg.sigpack = w2;
		else if (!strcmpi(w1, "og_sigpack_key")) og_cfg.sigpack_key = w2;
		else if (!strcmpi(w1, "og_sigpack_min_version")) og_cfg.sigpack_min_version = (uint32)strtoul(w2, nullptr, 10);
		else if (!strcmpi(w1, "og_guard_file")) og_cfg.guard_file = w2;
		else if (!strcmpi(w1, "og_exe_reference")) og_cfg.exe_reference = w2;
		else if (!strcmpi(w1, "og_ch_time_drift")) og_cfg.time_drift = std::max(2, atoi(w2));
		else if (!strcmpi(w1, "og_allowed_exe")) og_cfg.allowed_exe.push_back(w2);
		else if (!strcmpi(w1, "og_clear_signatures")) { if (config_switch(w2)) og_cfg.signatures.clear(); }
		else if (!strcmpi(w1, "og_signature")) {
			// kind,action,pattern (local additions; the signed package comes with og_sigpack)
			char kind[32], action[32], pattern[256];
			if (sscanf(w2, "%31[^,],%31[^,],%255[^\r\n]", kind, action, pattern) == 3 && og_cfg.signatures.size() < 4000) {
				s_og_signature sig;
				sig.id = (uint16)(30000 + og_cfg.signatures.size());
				sig.kind = og_parse_kind(kind);
				sig.action = og_parse_action(action);
				sig.score = og_action_score(sig.action);
				sig.pattern = pattern;
				sig.name = std::string("local:") + pattern;
				og_cfg.signatures.push_back(sig);
			}
		}
		else if (!strncmpi(w1, "og_ch_", 6)) {
			// og_ch_<name> (switch), og_ch_<name>_interval, og_ch_<name>_action
			char name[32] = "", what[32] = "";
			sscanf(w1 + 6, "%31[^_]_%31s", name, what);
			int32 i = og_challenge_index(name);
			if (i == 0) og_switch_set(w1, w2);
			else if (!*what) og_cfg.ch[i].enable = config_switch(w2) != 0;
			else if (!strcmpi(what, "interval")) og_cfg.ch[i].interval = std::max(20, atoi(w2));
			else if (!strcmpi(what, "action")) og_cfg.ch[i].action = og_parse_action(w2);
		}
		else if (!strcmpi(w1, "og_protected_file")) {
			// index,path,required
			int32 idx;
			char path[512], req[16] = "yes";
			if (sscanf(w2, "%d , %511[^,] , %15s", &idx, path, req) >= 2 && idx >= 0 && idx < 255) {
				trim(path);
				og_cfg.files.push_back({ (uint8)idx, config_switch(req) != 0, path });
			}
		}
		else if (!strcmpi(w1, "og_item_color") || !strcmpi(w1, "og_group_namecolor")) {
			int32 id;
			unsigned int rgb;
			if (sscanf(w2, "%d , %x", &id, &rgb) == 2)
				(w1[3] == 'i' ? og_cfg.item_colors : og_cfg.group_colors).emplace_back(id, rgb & 0xFFFFFF);
		}
		else if (!strcmpi(w1, "og_lgp_color")) {
			// name,colour number (the number is the colour in the client patch, see og_lgp.c)
			char name[16];
			int32 index;
			if (sscanf(w2, "%15[a-z] , %i", name, &index) == 2) {
				auto it = std::find_if(og_cfg.lgp_colors.begin(), og_cfg.lgp_colors.end(), [&](const auto& c) { return !strcmpi(c.name.c_str(), name); });
				if (it != og_cfg.lgp_colors.end()) it->index = index;
				else og_cfg.lgp_colors.push_back({ name, index });
			}
		}
		else if (!strcmpi(w1, "og_lgp_attack")) {
			// skill ids shown as triangles (damage or harm to enemies); every other LGP skill is a square (protective)
			for (char* t = strtok(w2, ", \t\r\n"); t != nullptr; t = strtok(nullptr, ", \t\r\n"))
				if (uint16 id = (uint16)strtoul(t, nullptr, 0); id != 0 && std::find(og_cfg.lgp_attack.begin(), og_cfg.lgp_attack.end(), id) == og_cfg.lgp_attack.end())
					og_cfg.lgp_attack.push_back(id);
		}
		else if (!strcmpi(w1, "og_lgp")) {
			// [style,]skill,<colour name | unit id>  (style: square = default, circle, set2 .. set10); unit 0 removes the entry
			char a[16] = "", b[16] = "", c[16] = "";
			int32 n = sscanf(w2, "%15[^, ] , %15[^, ] , %15[^, ]", a, b, c);
			const char* style = n == 3 ? a : "square";
			const char* skill_s = n == 3 ? b : a;
			const char* value = n == 3 ? c : b;
			if (n >= 2 && isdigit((unsigned char)skill_s[0])) {
				uint16 skill = (uint16)strtoul(skill_s, nullptr, 0);
				s_og_lgp e = { style, skill, 0, -1 };
				if (isdigit((unsigned char)value[0]))
					e.unit = (uint16)strtoul(value, nullptr, 0);
				else {
					auto it = std::find_if(og_cfg.lgp_colors.begin(), og_cfg.lgp_colors.end(), [&](const auto& col) { return !strcmpi(col.name.c_str(), value); });
					if (it == og_cfg.lgp_colors.end()) { ShowWarning("OdinGuard: og_lgp %s: unknown colour '%s' (og_lgp_color first)\n", w2, value); continue; }
					e.color = (int32)(it - og_cfg.lgp_colors.begin());
					e.unit = 0xc8; // the plain unit; the colour travels in the marker
				}
				og_cfg.lgp.erase(std::remove_if(og_cfg.lgp.begin(), og_cfg.lgp.end(), [&](const auto& x) { return x.skill == skill && !strcmpi(x.style.c_str(), style); }), og_cfg.lgp.end());
				if (e.unit != 0) og_cfg.lgp.push_back(e);
			}
		}
		else if (!strcmpi(w1, "import")) og_config_read(w2);
		else if (!strncmpi(w1, "og_", 3))
			og_switch_set(w1, w2); // read by the server modules (og_map: og_rate_*, og_bh_*, og_banwave_*, commands ...)
	}
	fclose(fp);
	return true;
}

const char* og_setting(const char* key, const char* def) {
	for (const auto& e : og_cfg.switches)
		if (!strcmpi(e.first.c_str(), key)) return e.second.c_str();
	return def;
}

int32 og_setting_int(const char* key, int32 def) {
	const char* v = og_setting(key, nullptr);
	return v != nullptr ? (int32)strtol(v, nullptr, 0) : def;
}

bool og_module_on(const char* key, bool def) {
	const char* v = og_setting(key, nullptr);
	return og_cfg.enable && (v != nullptr ? config_switch(v) != 0 : def);
}

e_og_action og_setting_action(const char* key, e_og_action def) {
	const char* v = og_setting(key, nullptr);
	return v != nullptr ? og_parse_action(v) : def;
}

// the import file is created at first start, so the server owner finds the switches without reading the docs
static void og_config_bootstrap() {
	FILE* fp = fopen(OG_CONF_IMPORT, "r");
	if (fp != nullptr) { fclose(fp); return; }
	fp = fopen(OG_CONF_IMPORT, "w");
	if (fp == nullptr) return;
	fputs("// OdinGuard Anti Cheat local settings (created automatically). See conf/odinguard_athena.conf.\n"
		"// The Studio writes the profile values (key, proto salt, build secret, signature package) here.\n"
		"//og_enable: yes\n"
		"//og_key: <64 hex characters, or the ODINGUARD_KEY environment variable>\n", fp);
	fclose(fp);
	ShowInfo("OdinGuard: created '" CL_WHITE OG_CONF_IMPORT CL_RESET "'.\n");
}

static bool og_read_file(const std::string& path, std::vector<uint8>& out) {
	FILE* f = fopen(path.c_str(), "rb");
	if (f == nullptr) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? (size_t)n : 0);
	bool ok = n <= 0 || fread(out.data(), 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	return ok;
}

// signed signature package written by the Studio (og-core sigpack.rs):
// "OGSP" u8 format(1) u32 version u32 created u16 n, n x { u16 id, u8 kind, u8 action, u8 score, u8 nlen, name, u8 plen, pattern }, u8 sig[64]
static bool og_load_sigpack() {
	std::vector<uint8> d;
	uint8 key[32];
	if (!og_read_file(og_cfg.sigpack, d)) { ShowError("OdinGuard: signature package '%s' not found.\n", og_cfg.sigpack.c_str()); return false; }
	if (!from_hex(og_cfg.sigpack_key, key, 32)) { ShowError("OdinGuard: og_sigpack_key missing, the signature package is not loaded.\n"); return false; }
	if (d.size() < 15 + 64 || memcmp(d.data(), "OGSP", 4) != 0 || d[4] != 1) { ShowError("OdinGuard: signature package has a wrong format.\n"); return false; }
	size_t body = d.size() - 64;
	if (crypto_eddsa_check(d.data() + body, key, d.data(), body) != 0) { ShowError("OdinGuard: signature package signature INVALID, not loaded.\n"); return false; }
	uint32 version = og_rd32(&d[5]);
	if (version < og_cfg.sigpack_min_version) { ShowError("OdinGuard: signature package version %u is older than %u (rollback refused).\n", version, og_cfg.sigpack_min_version); return false; }
	uint16 n = og_rd16(&d[13]);
	size_t pos = 15;
	std::vector<s_og_signature> sigs;
	for (uint16 i = 0; i < n; i++) {
		if (pos + 6 > body) return false;
		s_og_signature s;
		s.id = og_rd16(&d[pos]);
		s.kind = d[pos + 2];
		s.action = d[pos + 3] <= OG_ACTION_FLAGBAN ? (e_og_action)d[pos + 3] : OG_ACTION_LOG;
		s.score = d[pos + 4];
		uint8 nl = d[pos + 5];
		if (pos + 6 + nl + 1 > body) return false;
		s.name.assign((const char*)&d[pos + 6], nl);
		uint8 pl = d[pos + 6 + nl];
		if (pos + 7 + nl + pl > body || s.id >= 30000) return false;
		s.pattern.assign((const char*)&d[pos + 7 + nl], pl);
		pos += 7 + nl + pl;
		sigs.push_back(s);
	}
	og_cfg.signatures.insert(og_cfg.signatures.begin(), sigs.begin(), sigs.end());
	og_cfg.sigpack_version = version;
	ShowStatus("OdinGuard: signature package v%u loaded (%d entries, signature ok).\n", version, (int32)sigs.size());
	return true;
}

bool og_enabled() { return og_cfg.enable; }
bool og_required() { return og_cfg.enable && og_cfg.required; }
bool og_log_only() { return og_cfg.log_only; }
int32 og_ban_match() { return og_cfg.ban_match; }
int32 og_max_per_machine() { return og_cfg.max_per_machine; }
uint32 og_sigpack_version() { return og_cfg.sigpack_version; }

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

void og_report(int32 fd, const char* type, const char* detail, e_og_action action, int32 score) {
	// warn mode: nothing acts; flags are still stored (they only act through the ban wave, which warn mode skips)
	if (og_cfg.log_only && (action == OG_ACTION_KICK || action == OG_ACTION_BAN)) action = OG_ACTION_LOG;
	uint32 ip = session_isValid(fd) ? session[fd]->client_addr : 0;
	ShowWarning("OdinGuard: %s (%s) fd %d ip %u.%u.%u.%u -> %s\n", type, detail, fd, CONVIP(ip), og_action_name(action));
	if (og_on_event != nullptr)
		og_on_event(fd, type, detail, action, score);
	else if (action == OG_ACTION_KICK || action == OG_ACTION_BAN)
		set_eof(fd);
}

static void og_event(int32 fd, const char* type, const char* detail, e_og_action action) {
	og_report(fd, type, detail, action, og_action_score(action));
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
		og_wr16(r, (uint16)plain.size());
		uint8 nonce[24];
		og_rekey(o->k_s2c, o->ctr_s2c);
		og_nonce(nonce, o->ctr_s2c++);
		crypto_aead_lock(r + 18, r + 2, o->k_s2c, nonce, r, 2, plain.data(), plain.size());
		p += chunk;
		n -= chunk;
	} while (n > 0);
	crypto_wipe(plain.data(), plain.size());
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
	og_wr16(&m[3], (uint16)og_cfg.alive_interval);
	og_control(fd, o, m);
	// signatures and the GRF manifest in chunks; the first replaces the client's list
	std::vector<s_og_signature> all = og_cfg.signatures;
	all.insert(all.end(), og_cfg.grf_manifest.begin(), og_cfg.grf_manifest.end());
	size_t i = 0;
	do {
		size_t end = std::min(all.size(), i + OG_SIGS_PER_MESSAGE);
		std::vector<uint8> s = { OG_S_SIGS, (uint8)(i == 0 ? OG_SIGS_REPLACE : OG_SIGS_APPEND), 0, 0 };
		og_wr16(&s[2], (uint16)(end - i));
		for (size_t k = i; k < end; k++) {
			const s_og_signature& sig = all[k];
			size_t len = std::min(sig.pattern.size(), (size_t)255);
			size_t pos = s.size();
			s.resize(pos + 4 + len);
			og_wr16(&s[pos], sig.id);
			s[pos + 2] = sig.kind;
			s[pos + 3] = (uint8)len;
			memcpy(&s[pos + 4], sig.pattern.data(), len);
		}
		og_control(fd, o, s);
		i = end;
	} while (i < all.size());
}

bool og_send_control(int32 fd, const std::vector<uint8>& msg) {
	s_og_state* o = og_state(fd);
	if (o == nullptr || !o->active || msg.empty()) return false;
	og_control(fd, o, msg);
	return true;
}

const std::vector<std::pair<int32, uint32>>& og_item_colors() { return og_cfg.item_colors; }
const std::vector<std::pair<int32, uint32>>& og_group_namecolors() { return og_cfg.group_colors; }
uint16 og_lgp_unit(uint16 skill_id) {
	if (!og_cfg.enable) return 0;
	for (const auto& e : og_cfg.lgp)
		if (e.skill == skill_id && !strcmpi(e.style.c_str(), "square")) return e.unit;
	return 0;
}

bool og_lgp_entry(const char* style, uint16 skill_id, uint16* unit, int32* color_index) {
	if (!og_cfg.enable) return false;
	for (const auto& e : og_cfg.lgp) {
		if (e.skill != skill_id || strcmpi(e.style.c_str(), style)) continue;
		*unit = e.unit;
		*color_index = e.color < 0 ? -1 : og_cfg.lgp_colors[e.color].index;
		return true;
	}
	return false;
}

bool og_lgp_attacks(uint16 skill_id) {
	return std::find(og_cfg.lgp_attack.begin(), og_cfg.lgp_attack.end(), skill_id) != og_cfg.lgp_attack.end();
}

int32 og_lgp_color_count() { return (int32)og_cfg.lgp_colors.size(); }

int32 og_lgp_color_value(int32 pos) {
	return pos >= 0 && pos < (int32)og_cfg.lgp_colors.size() ? og_cfg.lgp_colors[pos].index : -1;
}

bool og_send_ping(int32 fd) {
	s_og_state* o = og_state(fd);
	if (o == nullptr || !o->active) return false;
	std::vector<uint8> m(5);
	m[0] = OG_S_PING;
	og_wr32(&m[1], (uint32)gettick());
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

// ---------------------------------------------------------------- challenges

static const char* og_challenge_name(uint8 type) {
	switch (type) {
	case OG_CH_NONCE: return "nonce";
	case OG_CH_CODE_RANGE: return "code";
	case OG_CH_TIME: return "time";
	case OG_CH_FILE_RANGE: return "file";
	default: return "unknown";
	}
}

static void og_keyed(const char* label, const uint8 nonce[16], uint32 a, uint32 b, uint8 out[32]) {
	uint8 ab[8];
	og_wr32(ab, a);
	og_wr32(ab + 4, b);
	crypto_blake2b_ctx c;
	crypto_blake2b_keyed_init(&c, 32, og_cfg.secret, 32);
	crypto_blake2b_update(&c, (const uint8*)label, strlen(label));
	crypto_blake2b_update(&c, nonce, 16);
	crypto_blake2b_update(&c, ab, 8);
	crypto_blake2b_final(&c, out);
}

static bool og_range_digest(const std::vector<uint8>& img, uint32 off, uint32 len, const uint8 nonce[16], uint8 out[32]) {
	if (off > img.size() || len > img.size() - off || len == 0) return false;
	crypto_blake2b_ctx c;
	crypto_blake2b_init(&c, 32);
	crypto_blake2b_update(&c, nonce, 16);
	crypto_blake2b_update(&c, img.data() + off, len);
	crypto_blake2b_final(&c, out);
	return true;
}

static const s_og_file* og_file_by_index(uint8 index) {
	for (const s_og_file& f : og_cfg.files)
		if (f.index == index) return &f;
	return nullptr;
}

static bool og_file_digest(const s_og_file& f, uint32 off, uint32 len, const uint8 nonce[16], uint8 out[32], bool* range_ok) {
	FILE* fp = fopen(f.path.c_str(), "rb");
	*range_ok = false;
	if (fp == nullptr) return false;
	std::vector<uint8> buf(len);
	bool ok = fseek(fp, (long)off, SEEK_SET) == 0 && fread(buf.data(), 1, len, fp) == len;
	fclose(fp);
	if (!ok) return false;
	*range_ok = true;
	crypto_blake2b_ctx c;
	crypto_blake2b_init(&c, 32);
	crypto_blake2b_update(&c, nonce, 16);
	crypto_blake2b_update(&c, buf.data(), buf.size());
	crypto_blake2b_final(&c, out);
	return true;
}

static long og_file_size(const std::string& path) {
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == nullptr) return -1;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fclose(fp);
	return n;
}

static bool og_challenge_possible(uint8 type) {
	switch (type) {
	case OG_CH_NONCE: case OG_CH_TIME: return true;
	case OG_CH_CODE_RANGE: return og_cfg.exe_image.size() >= OG_CODE_RANGE_MIN || og_cfg.guard_image.size() >= OG_CODE_RANGE_MIN;
	case OG_CH_FILE_RANGE: return !og_cfg.files.empty();
	}
	return false;
}

static void og_send_challenge(int32 fd, s_og_state* o, uint8 type) {
	s_og_pending p = {};
	p.id = o->next_id++;
	p.type = type;
	p.sent = gettick();
	og_random(p.nonce, 16);
	if (type == OG_CH_CODE_RANGE) {
		bool guard = og_cfg.exe_image.size() < OG_CODE_RANGE_MIN || (!og_cfg.guard_image.empty() && (og_random32() & 1));
		const std::vector<uint8>& img = guard ? og_cfg.guard_image : og_cfg.exe_image;
		p.target = guard ? 1 : 0;
		p.b = OG_CODE_RANGE_MIN + og_random32() % (OG_CODE_RANGE_MAX - OG_CODE_RANGE_MIN);
		if (p.b > img.size()) p.b = (uint32)img.size();
		p.a = (uint32)(og_random32() % (img.size() - p.b + 1));
	} else if (type == OG_CH_FILE_RANGE) {
		const s_og_file& f = og_cfg.files[og_random32() % og_cfg.files.size()];
		long size = og_file_size(f.path);
		if (size < 1) return;
		p.target = f.index;
		p.b = std::min<uint32>((uint32)size, 4096 + og_random32() % (256 * 1024));
		p.a = (uint32)(og_random32() % ((uint32)size - p.b + 1));
	}
	og_challenge_msg m = { p.id, p.type, p.nonce, p.target, p.a, p.b };
	std::vector<uint8> out(OG_CHALLENGE_LEN);
	og_wire_write_challenge(out.data(), &m);
	o->pending.push_back(p);
	og_control(fd, o, out);
}

static void og_challenge_failed(int32 fd, uint8 type, const char* detail) {
	char t[32];
	safesnprintf(t, sizeof(t), "challenge:%s", og_challenge_name(type));
	og_event(fd, t, detail, og_cfg.ch[type].action);
}

static void og_handle_response(int32 fd, s_og_state* o, const og_response_msg& r) {
	auto it = std::find_if(o->pending.begin(), o->pending.end(), [&r](const s_og_pending& p) { return p.id == r.id; });
	if (it == o->pending.end()) {
		og_event(fd, "challenge:replay", "answer to an unknown or already answered challenge", og_cfg.tamper_action);
		return;
	}
	s_og_pending p = *it;
	o->pending.erase(it);
	if (r.type != p.type) { og_challenge_failed(fd, p.type, "answer of a different type"); return; }
	if (r.status == OG_CH_ST_UNKNOWN) { og_event(fd, "challenge:unsupported", og_challenge_name(p.type), OG_ACTION_LOG); return; }
	uint8 want[32];
	char detail[96];
	switch (p.type) {
	case OG_CH_NONCE:
		og_keyed("OG/nonce", p.nonce, 0, 0, want);
		if (crypto_verify32(want, r.digest) != 0) og_challenge_failed(fd, p.type, "wrong build secret (foreign or rebuilt guard)");
		break;
	case OG_CH_CODE_RANGE: {
		const std::vector<uint8>& img = p.target ? og_cfg.guard_image : og_cfg.exe_image;
		if (r.status != OG_CH_ST_OK || r.a != p.a || r.b != p.b) { og_challenge_failed(fd, p.type, "range not answered"); break; }
		if (og_range_digest(img, p.a, p.b, p.nonce, want) && crypto_verify32(want, r.digest) != 0) {
			safesnprintf(detail, sizeof(detail), "%s code modified in range %u+%u", p.target ? "guard" : "exe", p.a, p.b);
			og_challenge_failed(fd, p.type, detail);
		}
		break;
	}
	case OG_CH_TIME: {
		og_keyed("OG/time", p.nonce, r.a, r.b, want);
		if (crypto_verify32(want, r.digest) != 0) { og_challenge_failed(fd, p.type, "time answer not authentic"); break; }
		t_tick now = gettick();
		if (!o->time_base) { o->time_base = true; o->base_server = now; o->base_tick = r.a; o->base_qpc = r.b; break; }
		int64 ds = DIFF_TICK(now, o->base_server);
		if (ds < 120000 || o->time_reported) break;
		int64 dt = (int64)(uint32)(r.a - o->base_tick), dq = (int64)(uint32)(r.b - o->base_qpc);
		int64 drift_t = (dt - ds) * 100 / ds, drift_q = (dq - ds) * 100 / ds;
		if (std::abs(drift_t) > og_cfg.time_drift || std::abs(drift_q) > og_cfg.time_drift) {
			o->time_reported = true;
			safesnprintf(detail, sizeof(detail), "client clock %+d%% (tick) %+d%% (qpc) over %d s", (int32)drift_t, (int32)drift_q, (int32)(ds / 1000));
			og_challenge_failed(fd, p.type, detail);
		}
		break;
	}
	case OG_CH_FILE_RANGE: {
		const s_og_file* f = og_file_by_index(p.target);
		if (f == nullptr) break;
		if (r.status == OG_CH_ST_MISSING) {
			if (f->required) og_challenge_failed(fd, p.type, "protected file missing");
			break;
		}
		bool range_ok = false;
		if (r.status != OG_CH_ST_OK || r.a != p.a || r.b != p.b) { og_challenge_failed(fd, p.type, "file range not answered"); break; }
		if (og_file_digest(*f, p.a, p.b, p.nonce, want, &range_ok) && crypto_verify32(want, r.digest) != 0) {
			safesnprintf(detail, sizeof(detail), "file %u differs from the official one", p.target);
			og_challenge_failed(fd, p.type, detail);
		}
		break;
	}
	}
}

static void og_challenge_tick(int32 fd, s_og_state* o, t_tick now) {
	for (auto it = o->pending.begin(); it != o->pending.end();) {
		if (DIFF_TICK(now, it->sent) > OG_CHALLENGE_TIMEOUT) {
			uint8 type = it->type;
			it = o->pending.erase(it);
			og_challenge_failed(fd, type, "no answer");
		} else
			++it;
	}
	for (uint8 t = OG_CH_NONCE; t <= OG_CH_FILE_RANGE; t++) {
		if (!og_cfg.ch[t].enable || !og_challenge_possible(t)) continue;
		int32 iv = og_cfg.ch[t].interval * 1000;
		if (o->next_at[t] == 0) { o->next_at[t] = now + 5000 + og_random32() % (uint32)iv; continue; }
		if (DIFF_TICK(now, o->next_at[t]) < 0) continue;
		// random window: next one between 0.5 and 1.5 intervals, so a tool cannot predict it
		o->next_at[t] = now + iv / 2 + og_random32() % (uint32)iv;
		og_send_challenge(fd, o, t);
	}
}

// ---------------------------------------------------------------- control messages

static void og_handle_control(int32 fd, s_og_state* o, const uint8* p, size_t n) {
	if (n < 1) return;
	switch (p[0]) {
	case OG_C_ALIVE: {
		og_alive_msg a;
		if (og_wire_alive(p, n, &a) < 0) { og_event(fd, "malformed", "heartbeat", og_cfg.tamper_action); return; }
		o->last_alive = gettick();
		o->timed_out = false;
		o->info.exe_code = to_hex(a.exe_code, 32);
		o->info.guard_code = to_hex(a.guard_code, 32);
		if (!og_cfg.allowed_exe.empty() && !o->exe_reported &&
			std::find(og_cfg.allowed_exe.begin(), og_cfg.allowed_exe.end(), o->info.exe_code) == og_cfg.allowed_exe.end()) {
			o->exe_reported = true;
			og_event(fd, "exe-code", o->info.exe_code.c_str(), og_cfg.integrity_action);
		}
		if (!og_cfg.guard_code.empty() && !o->guard_reported && o->info.guard_code != og_cfg.guard_code) {
			o->guard_reported = true;
			og_event(fd, "guard-code", o->info.guard_code.c_str(), og_cfg.integrity_action);
		}
		if (a.flags & OG_ALIVE_HOOKS_TAMPERED) og_event(fd, "hooks-tampered", "network hooks replaced", og_cfg.tamper_action);
		if (a.flags & OG_ALIVE_DEBUGGER) og_event(fd, "debugger", "debugger attached", og_cfg.tamper_action);
		if (a.flags & OG_ALIVE_HW_BREAKPOINT) og_event(fd, "hw-breakpoint", "debug registers set", og_cfg.tamper_action);
		if (a.flags & OG_ALIVE_API_HOOKED) og_event(fd, "api-hook", "timing/input API patched", og_cfg.tamper_action);
		if (a.flags & OG_ALIVE_GRF_VIOLATION) og_event(fd, "grf", "protected resource overridden", og_cfg.grf_action);
		o->info.lgp_palette = (a.flags & OG_ALIVE_LGP_PALETTE) != 0 || o->info.lgp_palette;
		break;
	}
	case OG_C_DETECT: {
		if (n < 4) return;
		uint16 id = og_rd16(p + 1);
		uint8 kind = p[3];
		std::string text((const char*)p + 4, n - 4);
		const s_og_signature* sig = og_find_signature(id);
		if (sig != nullptr) {
			std::string type = "sig:" + sig->name;
			og_report(fd, type.c_str(), text.c_str(), sig->action, sig->score);
		} else if (kind == OG_KIND_GRF) og_event(fd, "detect:grf", text.c_str(), og_cfg.grf_action);
		else if (kind == OG_KIND_GRF_MODIFIED) og_event(fd, "grf:modified", text.c_str(), og_cfg.grf_modified_action);
		else if (kind == OG_KIND_GRF_UNKNOWN) og_event(fd, "grf:unknown", text.c_str(), og_cfg.grf_unknown_action);
		else if (kind == OG_KIND_GRF_MISSING) og_event(fd, "grf:missing", text.c_str(), og_cfg.grf_missing_action);
		else if (kind == OG_KIND_MUTEX || kind == OG_KIND_REGISTRY) og_event(fd, "detect:unknown-id", text.c_str(), OG_ACTION_LOG);
		else if (kind == OG_KIND_INJECT) og_event(fd, "detect:module", text.c_str(), og_cfg.inject_action);
		else if (kind == OG_KIND_API_HOOK) og_event(fd, "detect:api-hook", text.c_str(), OG_ACTION_LOG);
		else if (kind == OG_KIND_INPUT) og_event(fd, "detect:input", text.c_str(), OG_ACTION_LOG);
		else og_event(fd, "detect:unknown-id", text.c_str(), OG_ACTION_LOG);
		break;
	}
	case OG_C_RESPONSE: {
		og_response_msg r;
		if (og_wire_response(p, n, &r) < 0) { og_event(fd, "malformed", "challenge answer", og_cfg.tamper_action); return; }
		og_handle_response(fd, o, r);
		break;
	}
	case OG_C_PONG:
		if (n >= 5 && og_on_pong != nullptr)
			og_on_pong(fd, (int64)((uint32)gettick() - og_rd32(p + 1)));
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
	og_client_hello h;
	int32 r = og_wire_client_hello(o->in.data(), o->in.size(), &h);
	if (r == 0) return false;
	if (r < 0) { set_eof(fd); return false; }

	o->info.version = h.version;
	o->info.flags = h.flags;
	o->info.lgp_palette = (h.flags & OG_FLAG_LGP_PALETTE) != 0;
	o->info.unique_id = to_hex(h.unique_id, 32);
	o->info.exe_code = to_hex(h.exe_code, 32);
	o->info.build.assign((const char*)h.build, strnlen((const char*)h.build, 16));
	for (int32 i = 0; i < h.ncomp; i++) {
		const uint8* c = h.comp + i * (OG_COMPONENT_NAME + 32);
		std::string name((const char*)c, strnlen((const char*)c, OG_COMPONENT_NAME));
		o->info.components.emplace_back(name, to_hex(c + OG_COMPONENT_NAME, 32));
	}

	uint8 status = OG_STATUS_OK;
	const char* message = "";
	if (h.version < og_cfg.min_version) { status = OG_STATUS_OUTDATED; message = "Please update your client (the anti cheat is outdated)."; }
	else if (crypto_verify16(h.tag, og_cfg.tag) != 0) { status = OG_STATUS_PROFILE; message = "This client was built for another server. Please use this server's client."; }

	uint8 e_sec[32], e_pub[32], nonce_s[32], shared[32];
	if (!og_random(e_sec, sizeof(e_sec)) || !og_random(nonce_s, sizeof(nonce_s))) {
		ShowError("OdinGuard: no random source\n");
		set_eof(fd);
		return false;
	}
	crypto_x25519_public_key(e_pub, e_sec);
	crypto_x25519(shared, e_sec, h.eph_pub);
	crypto_wipe(e_sec, sizeof(e_sec));
	static const uint8 zero[32] = {};
	if (crypto_verify32(shared, zero) == 0) { set_eof(fd); return false; }

	std::vector<uint8> sh(OG_SERVER_HELLO_FIXED + strlen(message));
	og_wr16(&sh[0], OG_HELLO_ID);
	og_wr16(&sh[2], (uint16)sh.size());
	sh[4] = OG_PROTO_VERSION;
	sh[5] = status;
	memcpy(&sh[6], e_pub, 32);
	memcpy(&sh[38], nonce_s, 32);
	memcpy(&sh[OG_SERVER_HELLO_FIXED], message, strlen(message));

	uint8 transcript[64];
	crypto_blake2b_ctx bc;
	crypto_blake2b_init(&bc, 64);
	crypto_blake2b_update(&bc, o->in.data(), h.len);
	crypto_blake2b_update(&bc, sh.data(), 70);
	crypto_blake2b_final(&bc, transcript);
	crypto_eddsa_sign(&sh[70], og_cfg.sk, transcript, sizeof(transcript));

	uint8 keys[64];
	static const char label[] = "OdinGuard/v2";
	crypto_blake2b_init(&bc, 64);
	crypto_blake2b_update(&bc, (const uint8*)label, sizeof(label) - 1);
	crypto_blake2b_update(&bc, og_cfg.salt, OG_SALT_LEN);
	crypto_blake2b_update(&bc, shared, 32);
	crypto_blake2b_update(&bc, h.eph_pub, 32);
	crypto_blake2b_update(&bc, e_pub, 32);
	crypto_blake2b_update(&bc, h.nonce, 32);
	crypto_blake2b_update(&bc, nonce_s, 32);
	crypto_blake2b_final(&bc, keys);
	memcpy(o->k_c2s, keys, 32);
	memcpy(o->k_s2c, keys + 32, 32);
	crypto_wipe(keys, sizeof(keys));
	crypto_wipe(shared, sizeof(shared));

	o->in.erase(o->in.begin(), o->in.begin() + h.len);
	o->out.insert(o->out.end(), sh.begin(), sh.end());
#ifdef SEND_SHORTLIST
	send_shortlist_add_fd(fd);
#endif
	if (status != OG_STATUS_OK) {
		ShowInfo("OdinGuard: handshake refused (%s), fd %d\n", message, fd);
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
	while (!session[fd]->flag.eof) {
		uint16 blen = 0;
		int32 st = og_wire_record(o->in.data() + pos, o->in.size() - pos, &blen);
		if (st == 0) break;
		if (st < 0) { set_eof(fd); break; }
		const uint8* r = o->in.data() + pos;
		plain.resize(blen);
		uint8 nonce[24];
		og_rekey(o->k_c2s, o->ctr_c2s);
		og_nonce(nonce, o->ctr_c2s);
		if (crypto_aead_unlock(plain.data(), r + 2, o->k_c2s, nonce, r, 2, r + 18, blen) != 0) {
			og_event(fd, "record-mac", "tampered, injected or replayed packet", OG_ACTION_KICK);
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
		if (og_rd16(o->in.data()) != OG_HELLO_ID) {
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
		if (o == nullptr || !o->active || session[fd]->flag.eof) continue;
		og_challenge_tick(fd, o, now);
		if (!o->timed_out && DIFF_TICK(now, o->last_alive) > og_cfg.alive_timeout * 1000) {
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
		"CREATE TABLE IF NOT EXISTS `og_flag` ("
		" `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, `time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		" `unique_id` CHAR(64) NOT NULL DEFAULT '', `account_id` INT UNSIGNED NOT NULL DEFAULT 0, `char_name` VARCHAR(24) NOT NULL DEFAULT '',"
		" `rule` VARCHAR(64) NOT NULL, `score` INT NOT NULL DEFAULT 0, `ban` TINYINT(1) NOT NULL DEFAULT 0, `detail` VARCHAR(255) NOT NULL DEFAULT '',"
		" `state` ENUM('open','banned','cleared') NOT NULL DEFAULT 'open',"
		" PRIMARY KEY (`id`), KEY `account_id` (`account_id`, `state`), KEY `unique_id` (`unique_id`)) ENGINE=InnoDB",
	};
	return ddl;
}

static void og_load_references() {
	if (!og_cfg.guard_file.empty()) {
		std::vector<uint8> f;
		uint8* img = nullptr;
		size_t n = og_read_file(og_cfg.guard_file, f) ? og_pe_canonical(f.data(), f.size(), OG_CANON_CODE | OG_CANON_RDATA, &img) : 0;
		if (n) {
			og_cfg.guard_image.assign(img, img + n);
			uint8 h[32];
			crypto_blake2b(h, 32, img, n);
			og_cfg.guard_code = to_hex(h, 32);
			ShowStatus("OdinGuard: guard reference %s (%u bytes, code %.16s).\n", og_cfg.guard_file.c_str(), (uint32)n, og_cfg.guard_code.c_str());
		} else
			ShowError("OdinGuard: og_guard_file '%s' is not a usable guard DLL.\n", og_cfg.guard_file.c_str());
		free(img);
	}
	if (!og_cfg.exe_reference.empty() && !og_read_file(og_cfg.exe_reference, og_cfg.exe_image))
		ShowError("OdinGuard: og_exe_reference '%s' not found.\n", og_cfg.exe_reference.c_str());
	for (const s_og_file& f : og_cfg.files)
		if (og_file_size(f.path) < 0) ShowError("OdinGuard: protected file %d '%s' not found on the server.\n", f.index, f.path.c_str());
}

void og_init() {
	og_config_bootstrap();
	if (!og_config_read(OG_CONF_FILE))
		return;
	const char* env = getenv("ODINGUARD_KEY");
	if (env != nullptr && *env) og_cfg.key_hex = env;
	if ((env = getenv("ODINGUARD_PROTO_SALT")) != nullptr && *env) og_cfg.salt_hex = env;
	if ((env = getenv("ODINGUARD_BUILD_SECRET")) != nullptr && *env) og_cfg.secret_hex = env;
	if (!og_cfg.enable) return;

	uint8 seed[32];
	if (!from_hex(og_cfg.key_hex, seed, sizeof(seed))) {
		ShowError("OdinGuard: og_key / ODINGUARD_KEY missing or invalid (64 hex characters). OdinGuard disabled.\n");
		og_cfg.enable = false;
		return;
	}
	if (!og_cfg.salt_hex.empty() && !from_hex(og_cfg.salt_hex, og_cfg.salt, OG_SALT_LEN)) {
		ShowError("OdinGuard: og_proto_salt must be 32 hex characters. OdinGuard disabled.\n");
		og_cfg.enable = false;
		return;
	}
	if (!og_cfg.secret_hex.empty() && !from_hex(og_cfg.secret_hex, og_cfg.secret, 32))
		ShowError("OdinGuard: og_build_secret must be 64 hex characters; nonce and time challenges will fail.\n");
	{
		static const char label[] = "OdinGuard/tag";
		crypto_blake2b_ctx c;
		crypto_blake2b_init(&c, OG_TAG_LEN);
		crypto_blake2b_update(&c, (const uint8*)label, sizeof(label) - 1);
		crypto_blake2b_update(&c, og_cfg.salt, OG_SALT_LEN);
		crypto_blake2b_final(&c, og_cfg.tag);
	}
	crypto_eddsa_key_pair(og_cfg.sk, og_cfg.pk, seed);
	crypto_wipe(seed, sizeof(seed));
	if (!og_cfg.sigpack.empty() && og_module_on("og_signatures", true))
		og_load_sigpack();
	og_load_references();
	add_timer_func_list(og_alive_check, "og_alive_check");
	add_timer_interval(gettick() + 5000, og_alive_check, 0, 0, 5000);
	ShowStatus("OdinGuard Anti Cheat: enabled (required: %s, log only: %s, %d signatures, challenges%s%s%s%s), server key %s, profile tag %s\n",
		og_cfg.required ? "yes" : "no", og_cfg.log_only ? "yes" : "no", (int32)og_cfg.signatures.size(),
		og_cfg.ch[OG_CH_NONCE].enable ? " nonce" : "", og_cfg.ch[OG_CH_CODE_RANGE].enable ? " code" : "",
		og_cfg.ch[OG_CH_TIME].enable ? " time" : "", og_cfg.ch[OG_CH_FILE_RANGE].enable ? " file" : "",
		to_hex(og_cfg.pk, 32).c_str(), to_hex(og_cfg.tag, OG_TAG_LEN).c_str());
}

void og_final() {
	crypto_wipe(og_cfg.sk, sizeof(og_cfg.sk));
	crypto_wipe(og_cfg.secret, sizeof(og_cfg.secret));
}
