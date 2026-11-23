/* OdinGuard Anti Cheat – pure parsers and writers for the wire protocol (og_proto.h) and a portable PE
 * canonicaliser. No OS calls, no allocation except og_pe_canonical. Used by the client DLL, the server module
 * (compiled as C++ through og_wire.cpp) and the native fuzz test (tests/fuzz_wire.c).
 *
 * Return convention of the parsers: > 0 = number of bytes the message uses, 0 = incomplete (stream framing
 * only), -1 = malformed (drop the connection or the message). */
#ifndef OG_WIRE_H
#define OG_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "og_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t og_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t og_rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void og_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void og_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

typedef struct {
	uint16_t len;
	uint8_t version, flags, ncomp;
	const uint8_t *eph_pub, *nonce, *unique_id, *exe_code, *build, *tag, *comp; /* comp: ncomp x (16 + 32) */
} og_client_hello;

typedef struct {
	uint16_t len;
	uint8_t version, status;
	const uint8_t *eph_pub, *nonce, *sig, *message;
	size_t message_len;
} og_server_hello;

typedef struct { uint8_t block_vm, max_windows, input_filter, grf_strict; uint16_t alive_interval; } og_policy_msg;
typedef struct { uint32_t seq; const uint8_t *exe_code; uint16_t unknown_modules; uint8_t flags; const uint8_t *guard_code; } og_alive_msg;
typedef struct { uint32_t id; uint8_t type; const uint8_t *nonce; uint8_t target; uint32_t a, b; } og_challenge_msg;
typedef struct { uint32_t id; uint8_t type, status; const uint8_t *digest; uint32_t a, b; } og_response_msg;

typedef void (*og_sig_fn)(void *ctx, uint16_t id, uint8_t kind, const char *pattern, size_t len);
typedef void (*og_color_fn)(void *ctx, uint32_t rgb, const char *text, size_t len);

int og_wire_client_hello(const uint8_t *p, size_t n, og_client_hello *h);
int og_wire_server_hello(const uint8_t *p, size_t n, og_server_hello *h);
/* record framing: 1 = a complete record of 18 + *body_len bytes is at p */
int og_wire_record(const uint8_t *p, size_t n, uint16_t *body_len);

/* control messages (p[0] is the message type) */
int og_wire_policy(const uint8_t *p, size_t n, og_policy_msg *m);
int og_wire_alive(const uint8_t *p, size_t n, og_alive_msg *m);
int og_wire_challenge(const uint8_t *p, size_t n, og_challenge_msg *m);
int og_wire_response(const uint8_t *p, size_t n, og_response_msg *m);
/* returns the number of entries delivered, -1 if malformed (entries before the error were delivered) */
int og_wire_sigs(const uint8_t *p, size_t n, uint8_t *op, og_sig_fn fn, void *ctx);
int og_wire_colors(const uint8_t *p, size_t n, uint8_t *kind, uint8_t *op, og_color_fn fn, void *ctx);

size_t og_wire_write_challenge(uint8_t out[OG_CHALLENGE_LEN], const og_challenge_msg *m);
size_t og_wire_write_response(uint8_t out[OG_RESPONSE_LEN], const og_response_msg *m);

/* Canonical image of a PE file (32-bit): the sections a guard hashes, in section order, each as large as in
 * memory (zero padded), with every HIGHLOW relocation slot and the import address table zeroed, so it does not depend on the load address.
 * OG_CANON_CODE: executable, non-writable sections. OG_CANON_RDATA: also read-only initialized data
 * (not discardable). Returns the size and sets *out (malloc'd, caller frees), 0 if the file is not a usable PE. */
#define OG_CANON_CODE  1
#define OG_CANON_RDATA 2
size_t og_pe_canonical(const uint8_t *file, size_t n, int which, uint8_t **out);
/* section filter shared by the file and the in-memory variant */
int og_pe_section_included(uint32_t characteristics, int which);

#ifdef __cplusplus
}
#endif

#endif
