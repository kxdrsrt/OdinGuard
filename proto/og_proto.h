/* OdinGuard wire protocol v1. Shared by the client DLL (C) and the xAthena server (C++).
 * All integers little-endian.
 *
 * 1. Client hello (plaintext, first bytes on every game TCP connection):
 *      u16 id = OG_HELLO_ID ("OG"), u16 len, u8 version, u8 flags,
 *      u8 eph_pub[32], u8 nonce[32], u8 unique_id[32], u8 exe_code[32], char build[16],
 *      u8 n, n x { char name[16]; u8 hash[32]; }
 * 2. Server hello (plaintext):
 *      u16 id = OG_HELLO_ID, u16 len, u8 version, u8 status,
 *      u8 eph_pub[32], u8 nonce[32], u8 sig[64], char message[]
 *    sig = EdDSA(server static key, BLAKE2b-512(client hello || server hello without sig and message))
 * 3. Keys: BLAKE2b-512("OdinGuard/v1" || X25519(eph) || client_pub || server_pub || nonce_c || nonce_s)
 *      first 32 bytes = client->server key, last 32 = server->client key
 * 4. Records (both directions after the hellos):
 *      u16 body_len, u8 mac[16], u8 body[body_len]
 *      body = AEAD(XChaCha20-Poly1305, key, nonce = u64 counter || 16 zero bytes, ad = the 2 length bytes)
 *      plaintext body = u8 type, payload. type 0 = game stream bytes, type 1 = control message.
 */
#ifndef OG_PROTO_H
#define OG_PROTO_H

#define OG_HELLO_ID        0x474F /* bytes "OG" */
#define OG_PROTO_VERSION   1
#define OG_HELLO_MAX       2048
#define OG_COMPONENT_NAME  16
#define OG_MAX_COMPONENTS  24
#define OG_RECORD_MAX      60000  /* plaintext payload per record */
#define OG_RECORD_OVERHEAD (2 + 16 + 1)

#define OG_CLIENT_HELLO_FIXED (2 + 2 + 1 + 1 + 32 + 32 + 32 + 32 + 16 + 1)
#define OG_SERVER_HELLO_FIXED (2 + 2 + 1 + 1 + 32 + 32 + 64)

/* client hello flags */
#define OG_FLAG_VM        0x01
#define OG_FLAG_DEBUGGER  0x02

/* server hello status */
#define OG_STATUS_OK       0
#define OG_STATUS_OUTDATED 1
#define OG_STATUS_REFUSED  2

/* record types */
#define OG_REC_GAME    0
#define OG_REC_CONTROL 1

/* control messages, client -> server */
#define OG_C_ALIVE   0x01 /* u32 seq, u8 exe_code[32], u16 unknown_modules, u8 flags (OG_ALIVE_*) */
#define OG_C_DETECT  0x02 /* u16 signature_id, u8 kind, char text[] */
#define OG_C_PONG    0x03 /* u32 server_tick */
#define OG_C_REPORT  0x04 /* char text[]: answer to a command, shown to the player */

#define OG_ALIVE_HOOKS_TAMPERED 0x01
#define OG_ALIVE_DEBUGGER       0x02
#define OG_ALIVE_GRF_VIOLATION  0x04

/* control messages, server -> client */
#define OG_S_POLICY  0x81 /* u8 block_vm, u8 max_windows, u16 alive_interval_s, u8 input_filter, u8 grf_strict,
                             u16 n, n x { u16 id, u8 kind, u8 len, char pattern[len] } */
#define OG_S_PING    0x82 /* u32 server_tick */
#define OG_S_COMMAND 0x83 /* char command[] ("vsync", "crash") */

/* detection kinds */
#define OG_KIND_PROCESS 1
#define OG_KIND_WINDOW  2
#define OG_KIND_MODULE  3
#define OG_KIND_INPUT   4 /* synthetic input blocked */
#define OG_KIND_GRF     5 /* resource override */
#define OG_KIND_INJECT  6 /* unknown module loaded */

#endif
