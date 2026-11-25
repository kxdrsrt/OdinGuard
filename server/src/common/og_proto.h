/* OdinGuard Anti Cheat – wire protocol v2. Shared by the client DLL (C) and the server module (C++).
 * All integers little-endian. The parsers for every message live in og_wire.c (pure C, fuzzed natively).
 *
 * 1. Client hello (plaintext, first bytes on every game TCP connection):
 *      u16 id = OG_HELLO_ID ("OG"), u16 len, u8 version, u8 flags,
 *      u8 eph_pub[32], u8 nonce[32], u8 unique_id[32], u8 exe_code[32], char build[16], u8 profile_tag[16],
 *      u8 n, n x { char name[16]; u8 hash[32]; }
 *    profile_tag = BLAKE2b-128("OdinGuard/tag" || proto_salt): a client built for another server is refused
 *    with a clear message instead of failing on the first record.
 * 2. Server hello (plaintext):
 *      u16 id = OG_HELLO_ID, u16 len, u8 version, u8 status,
 *      u8 eph_pub[32], u8 nonce[32], u8 sig[64], char message[]
 *    sig = EdDSA(server static key, BLAKE2b-512(client hello || server hello without sig and message)).
 *    The signed transcript contains both version bytes: a downgrade is detected by both sides.
 * 3. Keys: BLAKE2b-512("OdinGuard/v2" || proto_salt || X25519(eph) || client_pub || server_pub || nonce_c || nonce_s)
 *      first 32 bytes = client->server key, last 32 = server->client key
 * 4. Records (both directions after the hellos):
 *      u16 body_len, u8 mac[16], u8 body[body_len]
 *      body = AEAD(XChaCha20-Poly1305, key, nonce = u64 counter || 16 zero bytes, ad = the 2 length bytes)
 *      plaintext body = u8 type, payload. type 0 = game stream bytes, type 1 = control message.
 *    Key rotation: before the record with counter c (c > 0, c % OG_REKEY_RECORDS == 0) the direction key becomes
 *    BLAKE2b-256(key || "OdinGuard/rekey"); the old key is wiped (forward secrecy inside a session).
 *    Replay: the counter is implicit, a replayed, dropped or reordered record fails authentication.
 */
#ifndef OG_PROTO_H
#define OG_PROTO_H

#define OG_HELLO_ID        0x474F /* bytes "OG" */
#define OG_PROTO_VERSION   2
#define OG_PROTO_MIN       2      /* lowest version a v2 peer accepts (downgrade protection) */
#define OG_HELLO_MAX       2048
#define OG_COMPONENT_NAME  16
#define OG_MAX_COMPONENTS  24
#define OG_RECORD_MAX      60000  /* plaintext payload per record */
#define OG_RECORD_OVERHEAD (2 + 16 + 1)
#define OG_REKEY_RECORDS   4096
#define OG_TAG_LEN         16
#define OG_SALT_LEN        16

#define OG_CLIENT_HELLO_FIXED (2 + 2 + 1 + 1 + 32 + 32 + 32 + 32 + 16 + OG_TAG_LEN + 1)
#define OG_SERVER_HELLO_FIXED (2 + 2 + 1 + 1 + 32 + 32 + 64)

/* client hello flags */
#define OG_FLAG_VM        0x01
#define OG_FLAG_DEBUGGER  0x02
#define OG_FLAG_LGP_PALETTE 0x04 /* the client draws LGP colours and shapes (og_lgp.c) */
#define OG_FLAG_DARKPACK  0x08 /* odin-dark.grf (competitive pack) is loaded and is the accepted version (og_assets.c) */

/* server hello status */
#define OG_STATUS_OK       0
#define OG_STATUS_OUTDATED 1
#define OG_STATUS_REFUSED  2
#define OG_STATUS_PROFILE  3 /* client built for another server (profile tag) */

/* record types */
#define OG_REC_GAME    0
#define OG_REC_CONTROL 1

/* control messages, client -> server */
#define OG_C_ALIVE    0x01 /* u32 seq, u8 exe_code[32], u16 unknown_modules, u8 flags (OG_ALIVE_*), u8 guard_code[32] */
#define OG_C_DETECT   0x02 /* u16 signature_id, u8 kind, char text[] */
#define OG_C_PONG     0x03 /* u32 server_tick */
#define OG_C_REPORT   0x04 /* char text[]: answer to a command, shown to the player */
#define OG_C_RESPONSE 0x05 /* u32 challenge_id, u8 type, u8 status (OG_CH_ST_*), u8 digest[32], u32 a, u32 b */

#define OG_ALIVE_LEN            (1 + 4 + 32 + 2 + 1 + 32)
#define OG_ALIVE_HOOKS_TAMPERED 0x01
#define OG_ALIVE_DEBUGGER       0x02
#define OG_ALIVE_GRF_VIOLATION  0x04
#define OG_ALIVE_API_HOOKED     0x08 /* timing/input APIs patched by a foreign module (speed hacks, macro tools) */
#define OG_ALIVE_HW_BREAKPOINT  0x10 /* debug registers set on the game thread */
#define OG_ALIVE_LGP_PALETTE    0x20 /* capability, not a violation: the client draws the LGP colour units (og_lgp.c) */
#define OG_ALIVE_DARKPACK       0x40 /* capability, not a violation: odin-dark.grf is loaded and accepted (og_assets.c) */

/* control messages, server -> client */
#define OG_S_POLICY    0x81 /* u8 block_vm, u8 max_windows, u16 alive_interval_s, u8 input_filter, u8 grf_strict,
                               u16 n (always 0 in v2, signatures come with OG_S_SIGS) */
#define OG_S_PING      0x82 /* u32 server_tick */
#define OG_S_COMMAND   0x83 /* char command[] ("vsync", "crash") */
#define OG_S_COLORS    0x84 /* u8 kind (OG_COLOR_*), u8 op (OG_COLOR_OP_*), u16 n,
                               n x { u8 r, u8 g, u8 b, u8 len, char text[len] } – names/items drawn in that colour */
#define OG_S_CHALLENGE 0x85 /* u32 challenge_id, u8 type (OG_CH_*), u8 nonce[16], u8 target, u32 a, u32 b */
#define OG_S_SIGS      0x86 /* u8 op (OG_SIGS_*), u16 n, n x { u16 id, u8 kind, u8 len, char pattern[len] } */

#define OG_POLICY_LEN    9
#define OG_CHALLENGE_LEN (1 + 4 + 1 + 16 + 1 + 4 + 4)
#define OG_RESPONSE_LEN  (1 + 4 + 1 + 1 + 32 + 4 + 4)

#define OG_SIGS_REPLACE 0
#define OG_SIGS_APPEND  1

#define OG_COLOR_NAME 1
#define OG_COLOR_ITEM 2
#define OG_COLOR_OP_REPLACE 0 /* the list replaces all entries of the kind */
#define OG_COLOR_OP_SET     1 /* add or change entries */
#define OG_COLOR_OP_REMOVE  2 /* remove entries (colour ignored) */

/* challenge types (OG_S_CHALLENGE) */
#define OG_CH_NONCE      1 /* digest = BLAKE2b-256(key = build secret, "OG/nonce" || nonce) */
#define OG_CH_CODE_RANGE 2 /* target 0 = game exe, 1 = guard dll; a = offset, b = length in the canonical code image;
                              digest = BLAKE2b-256(nonce || bytes) */
#define OG_CH_TIME       3 /* a = client tick (ms), b = client performance counter (ms); digest as OG_CH_NONCE */
#define OG_CH_FILE_RANGE 4 /* target = index into the protected file list of the profile; a = offset, b = length;
                              digest = BLAKE2b-256(nonce || bytes) */

#define OG_CH_ST_OK        0
#define OG_CH_ST_RANGE     1 /* offset/length outside the image or file */
#define OG_CH_ST_MISSING   2 /* file or module not present */
#define OG_CH_ST_UNKNOWN   3 /* challenge type not built into this client */

/* detection / signature kinds (OG_C_DETECT, OG_S_SIGS) */
#define OG_KIND_PROCESS      1
#define OG_KIND_WINDOW       2  /* window title contains */
#define OG_KIND_MODULE       3  /* loaded module name contains */
#define OG_KIND_INPUT        4  /* synthetic input blocked (report only) */
#define OG_KIND_GRF          5  /* resource override (report only) */
#define OG_KIND_INJECT       6  /* unknown module loaded (report only) */
#define OG_KIND_WINDOW_CLASS 7  /* exact window class name */
#define OG_KIND_FILE_NAME    8  /* file name in the client folder (case-insensitive, exact) */
#define OG_KIND_FILE_SHA256  9  /* hex sha256 of a file in the client folder or of a loaded module file */
#define OG_KIND_DRIVER       10 /* kernel driver / service name */
#define OG_KIND_GRF_SHA256   11 /* hex sha256 of a GRF listed in DATA.INI */
#define OG_KIND_API_HOOK     12 /* report only: API patched, text = function and owning module */
#define OG_KIND_GRF_MODIFIED 13 /* report only: GRF of the operator list differs (text = name) */
#define OG_KIND_GRF_UNKNOWN  14 /* report only: GRF in DATA.INI that is neither listed nor an allowed mod */
#define OG_KIND_GRF_MISSING  15 /* report only: required GRF of the operator list missing */
#define OG_KIND_MUTEX        16 /* named mutex / event / section exists (exact name, Local or Global) */
#define OG_KIND_REGISTRY     17 /* registry key exists: "HKCU\\path" or "HKLM\\path" */
/* GRF manifest of the operator (sent with OG_S_SIGS, id 0, never reported themselves):
 *   GRF_LIST "name|size|tail-sha256|full-sha256 or -|r or o"  accepted version of a GRF in DATA.INI (r = required);
 *            several entries with the same name = several accepted versions (patch rollouts)
 *   GRF_MOD  "tail-sha256|note"   allowed optional GRF under any name (skins, darkworld variants)
 *   OVERLAY  "data\\folder\\"    allowed loose-file folder (in addition to the built-in overlays)
 * tail-sha256 = SHA-256 of the 46-byte header followed by the bytes from the file table to the end of the file
 * (fast: every repack or entry change rewrites the table); full-sha256 = SHA-256 of the whole file, checked in
 * the background. */
#define OG_KIND_GRF_LIST     18
#define OG_KIND_GRF_MOD      19
#define OG_KIND_OVERLAY      20
#define OG_KIND_ALLOW_SHA256 64 /* allowlist: never report a file or module with this hash */
#define OG_KIND_ALLOW_PATH   65 /* allowlist: never report a module whose path contains this text */

#endif
