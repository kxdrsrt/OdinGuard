/* Native fuzz test of the wire parsers and the PE canonicaliser (proto/og_wire.c), built with ASan + UBSan:
 *   cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o fuzz_wire tests/fuzz_wire.c ../proto/og_wire.c
 *   ./fuzz_wire [iterations] [seed]
 * Mutates valid seed messages (bit flips, truncation, length fields, random bytes) and checks invariants:
 * no out-of-bounds access (sanitizers), parsers never report more bytes than given, every delivered entry lies
 * inside the buffer, writer output parses back to the same values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../proto/og_wire.h"

static uint64_t g_rng = 88172645463325252ull;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (uint32_t)g_rng; }

static const uint8_t *g_buf;
static size_t g_len;
static unsigned long g_entries;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "invariant failed: %s (line %d)\n", #c, __LINE__); abort(); } } while (0)

static void on_sig(void *ctx, uint16_t id, uint8_t kind, const char *p, size_t n)
{
	(void)ctx; (void)id; (void)kind;
	CHECK((const uint8_t *)p >= g_buf && (const uint8_t *)p + n <= g_buf + g_len);
	if (n) { volatile char c = p[n - 1]; (void)c; }
	g_entries++;
}

static void on_color(void *ctx, uint32_t rgb, const char *p, size_t n)
{
	(void)ctx;
	CHECK(rgb <= 0xFFFFFF);
	on_sig(NULL, 0, 0, p, n);
}

static size_t seed_client_hello(uint8_t *b)
{
	size_t n = OG_CLIENT_HELLO_FIXED + 3 * (OG_COMPONENT_NAME + 32), i;
	for (i = 0; i < n; i++) b[i] = (uint8_t)rnd();
	og_wr16(b, OG_HELLO_ID);
	og_wr16(b + 2, (uint16_t)n);
	b[OG_CLIENT_HELLO_FIXED - 1] = 3;
	return n;
}

static size_t seed_sigs(uint8_t *b, uint8_t type)
{
	size_t pos = type == OG_S_SIGS ? 4 : 5, i;
	int k;
	b[0] = type;
	b[1] = 1; b[2] = 1;
	for (k = 0; k < 5; k++) {
		uint8_t len = (uint8_t)(rnd() % 40);
		b[pos] = (uint8_t)k; b[pos + 1] = 0; b[pos + 2] = 3; b[pos + 3] = len;
		for (i = 0; i < len; i++) b[pos + 4 + i] = (uint8_t)('a' + rnd() % 26);
		pos += 4 + len;
	}
	if (type == OG_S_SIGS) og_wr16(b + 2, 5); else og_wr16(b + 3, 5);
	return pos;
}

/* a tiny but valid PE32 with one code section and a relocation block */
static size_t seed_pe(uint8_t *b)
{
	size_t n = 0x600;
	uint32_t lf = 0x80, opt = lf + 24;
	memset(b, 0, n);
	b[0] = 'M'; b[1] = 'Z';
	og_wr32(b + 0x3c, lf);
	memcpy(b + lf, "PE\0\0", 4);
	og_wr16(b + lf + 6, 2);
	og_wr16(b + lf + 20, 224);
	og_wr16(b + opt, 0x10b);
	og_wr32(b + opt + 96 + 5 * 8, 0x2000);   /* reloc rva */
	og_wr32(b + opt + 96 + 5 * 8 + 4, 12);
	/* .text */
	memcpy(b + opt + 224, ".text", 5);
	og_wr32(b + opt + 224 + 8, 0x100); og_wr32(b + opt + 224 + 12, 0x1000);
	og_wr32(b + opt + 224 + 16, 0x200); og_wr32(b + opt + 224 + 20, 0x200);
	og_wr32(b + opt + 224 + 36, 0x60000020);
	/* .reloc */
	memcpy(b + opt + 264, ".reloc", 6);
	og_wr32(b + opt + 264 + 8, 0x20); og_wr32(b + opt + 264 + 12, 0x2000);
	og_wr32(b + opt + 264 + 16, 0x200); og_wr32(b + opt + 264 + 20, 0x400);
	og_wr32(b + opt + 264 + 36, 0x42000040);
	memset(b + 0x200, 0xAB, 0x100);
	og_wr32(b + 0x400, 0x1000); og_wr32(b + 0x404, 12); og_wr16(b + 0x408, 0x3010); og_wr16(b + 0x40a, 0);
	return n;
}

static void mutate(uint8_t *b, size_t *n, size_t cap)
{
	int k, rounds = 1 + rnd() % 4;
	for (k = 0; k < rounds; k++) {
		switch (rnd() % 6) {
		case 0: if (*n) b[rnd() % *n] ^= (uint8_t)(1 << (rnd() % 8)); break;
		case 1: if (*n) *n = rnd() % *n; break;
		case 2: if (*n >= 4) og_wr16(b + 2 + (rnd() % 2), (uint16_t)rnd()); break;
		case 3: if (*n) b[rnd() % *n] = (uint8_t)rnd(); break;
		case 4: { size_t add = rnd() % 64; while (add-- && *n < cap) b[(*n)++] = (uint8_t)rnd(); break; }
		case 5: if (*n > 8) og_wr32(b + rnd() % (*n - 4), rnd()); break;
		}
	}
}

int main(int argc, char **argv)
{
	unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 200000, i;
	enum { CAP = 4096 };
	uint8_t *b = (uint8_t *)malloc(CAP);
	if (argc > 2) g_rng = strtoull(argv[2], NULL, 10) | 1;
	for (i = 0; i < iters; i++) {
		size_t n = 0;
		int which = (int)(rnd() % 9), r;
		switch (which) {
		case 0: n = seed_client_hello(b); break;
		case 1: n = OG_SERVER_HELLO_FIXED + 20; memset(b, 0x41, n); og_wr16(b, OG_HELLO_ID); og_wr16(b + 2, (uint16_t)n); break;
		case 2: n = seed_sigs(b, OG_S_SIGS); break;
		case 3: n = seed_sigs(b, OG_S_COLORS); break;
		case 4: n = OG_CHALLENGE_LEN; memset(b, 7, n); b[0] = OG_S_CHALLENGE; break;
		case 5: n = OG_RESPONSE_LEN; memset(b, 9, n); b[0] = OG_C_RESPONSE; break;
		case 6: n = OG_ALIVE_LEN; memset(b, 3, n); b[0] = OG_C_ALIVE; break;
		case 7: n = 18 + 100; memset(b, 0, n); og_wr16(b, 100); break;
		case 8: n = seed_pe(b); break;
		}
		if (rnd() % 8) mutate(b, &n, CAP);
		/* exact-size heap copy so ASan sees every overread */
		{
			uint8_t *m = (uint8_t *)malloc(n ? n : 1);
			memcpy(m, b, n);
			g_buf = m; g_len = n;
			switch (which) {
			case 0: { og_client_hello h; r = og_wire_client_hello(m, n, &h); CHECK(r <= (int)n); if (r > 0) CHECK(h.comp + h.ncomp * 48 <= m + n); break; }
			case 1: { og_server_hello h; r = og_wire_server_hello(m, n, &h); CHECK(r <= (int)n); if (r > 0) CHECK(h.message + h.message_len <= m + n); break; }
			case 2: { uint8_t op; og_wire_sigs(m, n, &op, on_sig, NULL); break; }
			case 3: { uint8_t k, op; og_wire_colors(m, n, &k, &op, on_color, NULL); break; }
			case 4: { og_challenge_msg c; uint8_t out[OG_CHALLENGE_LEN]; og_challenge_msg c2;
				if (og_wire_challenge(m, n, &c) > 0) { og_wire_write_challenge(out, &c); CHECK(og_wire_challenge(out, sizeof out, &c2) > 0 && c2.id == c.id && c2.a == c.a && c2.b == c.b && c2.target == c.target); } break; }
			case 5: { og_response_msg x; uint8_t out[OG_RESPONSE_LEN]; og_response_msg y;
				if (og_wire_response(m, n, &x) > 0) { og_wire_write_response(out, &x); CHECK(og_wire_response(out, sizeof out, &y) > 0 && y.id == x.id && y.status == x.status && !memcmp(y.digest, x.digest, 32)); } break; }
			case 6: { og_alive_msg a; og_wire_alive(m, n, &a); break; }
			case 7: { uint16_t bl = 0; r = og_wire_record(m, n, &bl); if (r > 0) CHECK((size_t)18 + bl <= n); break; }
			case 8: { uint8_t *img = NULL; size_t sz = og_pe_canonical(m, n, OG_CANON_CODE | OG_CANON_RDATA, &img);
				if (sz) { volatile uint8_t x = img[sz - 1]; (void)x; } free(img); break; }
			}
			free(m);
		}
	}
	/* the unmutated PE seed must give the code section with the relocated slot zeroed */
	{
		uint8_t *img = NULL;
		size_t n = seed_pe(b), sz = og_pe_canonical(b, n, OG_CANON_CODE, &img);
		CHECK(sz == 0x100 && img[0x0f] == 0xAB && img[0x10] == 0 && img[0x13] == 0 && img[0x14] == 0xAB);
		free(img);
	}
	printf("fuzz_wire: %lu iterations ok, %lu list entries checked\n", iters, g_entries);
	free(b);
	return 0;
}
