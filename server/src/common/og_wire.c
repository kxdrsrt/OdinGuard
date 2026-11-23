/* OdinGuard Anti Cheat – see og_wire.h. Valid C99 and C++ (the server compiles it through og_wire.cpp). */
#include "og_wire.h"

#include <stdlib.h>
#include <string.h>

int og_wire_client_hello(const uint8_t *p, size_t n, og_client_hello *h)
{
	uint16_t len;
	uint8_t nc;
	if (n < 4) return 0;
	if (og_rd16(p) != OG_HELLO_ID) return -1;
	len = og_rd16(p + 2);
	if (len < OG_CLIENT_HELLO_FIXED || len > OG_HELLO_MAX) return -1;
	if (n < len) return 0;
	nc = p[OG_CLIENT_HELLO_FIXED - 1];
	if (nc > OG_MAX_COMPONENTS || len != OG_CLIENT_HELLO_FIXED + (size_t)nc * (OG_COMPONENT_NAME + 32)) return -1;
	h->len = len;
	h->version = p[4];
	h->flags = p[5];
	h->eph_pub = p + 6;
	h->nonce = p + 38;
	h->unique_id = p + 70;
	h->exe_code = p + 102;
	h->build = p + 134;
	h->tag = p + 150;
	h->ncomp = nc;
	h->comp = p + OG_CLIENT_HELLO_FIXED;
	return len;
}

int og_wire_server_hello(const uint8_t *p, size_t n, og_server_hello *h)
{
	uint16_t len;
	if (n < 4) return 0;
	if (og_rd16(p) != OG_HELLO_ID) return -1;
	len = og_rd16(p + 2);
	if (len < OG_SERVER_HELLO_FIXED || len > OG_HELLO_MAX) return -1;
	if (n < len) return 0;
	h->len = len;
	h->version = p[4];
	h->status = p[5];
	h->eph_pub = p + 6;
	h->nonce = p + 38;
	h->sig = p + 70;
	h->message = p + OG_SERVER_HELLO_FIXED;
	h->message_len = (size_t)len - OG_SERVER_HELLO_FIXED;
	return len;
}

int og_wire_record(const uint8_t *p, size_t n, uint16_t *body_len)
{
	uint16_t b;
	if (n < 2) return 0;
	b = og_rd16(p);
	if (b == 0 || b > OG_RECORD_MAX + 1) return -1;
	if (n < (size_t)18 + b) return 0;
	*body_len = b;
	return 1;
}

int og_wire_policy(const uint8_t *p, size_t n, og_policy_msg *m)
{
	if (n < OG_POLICY_LEN || p[0] != OG_S_POLICY) return -1;
	m->block_vm = p[1];
	m->max_windows = p[2];
	m->alive_interval = og_rd16(p + 3);
	m->input_filter = p[5];
	m->grf_strict = p[6];
	return OG_POLICY_LEN;
}

int og_wire_alive(const uint8_t *p, size_t n, og_alive_msg *m)
{
	if (n < OG_ALIVE_LEN || p[0] != OG_C_ALIVE) return -1;
	m->seq = og_rd32(p + 1);
	m->exe_code = p + 5;
	m->unknown_modules = og_rd16(p + 37);
	m->flags = p[39];
	m->guard_code = p + 40;
	return OG_ALIVE_LEN;
}

int og_wire_challenge(const uint8_t *p, size_t n, og_challenge_msg *m)
{
	if (n < OG_CHALLENGE_LEN || p[0] != OG_S_CHALLENGE) return -1;
	m->id = og_rd32(p + 1);
	m->type = p[5];
	m->nonce = p + 6;
	m->target = p[22];
	m->a = og_rd32(p + 23);
	m->b = og_rd32(p + 27);
	return OG_CHALLENGE_LEN;
}

int og_wire_response(const uint8_t *p, size_t n, og_response_msg *m)
{
	if (n < OG_RESPONSE_LEN || p[0] != OG_C_RESPONSE) return -1;
	m->id = og_rd32(p + 1);
	m->type = p[5];
	m->status = p[6];
	m->digest = p + 7;
	m->a = og_rd32(p + 39);
	m->b = og_rd32(p + 43);
	return OG_RESPONSE_LEN;
}

int og_wire_sigs(const uint8_t *p, size_t n, uint8_t *op, og_sig_fn fn, void *ctx)
{
	size_t pos = 4;
	int cnt, i;
	if (n < 4 || p[0] != OG_S_SIGS) return -1;
	*op = p[1];
	cnt = og_rd16(p + 2);
	for (i = 0; i < cnt; i++) {
		uint8_t len;
		if (pos + 4 > n) return -1;
		len = p[pos + 3];
		if (pos + 4 + len > n) return -1;
		if (fn) fn(ctx, og_rd16(p + pos), p[pos + 2], (const char *)p + pos + 4, len);
		pos += 4 + (size_t)len;
	}
	return cnt;
}

int og_wire_colors(const uint8_t *p, size_t n, uint8_t *kind, uint8_t *op, og_color_fn fn, void *ctx)
{
	size_t pos = 5;
	int cnt, i;
	if (n < 5 || p[0] != OG_S_COLORS) return -1;
	*kind = p[1];
	*op = p[2];
	cnt = og_rd16(p + 3);
	for (i = 0; i < cnt; i++) {
		uint8_t len;
		if (pos + 4 > n) return -1;
		len = p[pos + 3];
		if (pos + 4 + len > n) return -1;
		if (fn) fn(ctx, ((uint32_t)p[pos] << 16) | ((uint32_t)p[pos + 1] << 8) | p[pos + 2], (const char *)p + pos + 4, len);
		pos += 4 + (size_t)len;
	}
	return cnt;
}

size_t og_wire_write_challenge(uint8_t out[OG_CHALLENGE_LEN], const og_challenge_msg *m)
{
	out[0] = OG_S_CHALLENGE;
	og_wr32(out + 1, m->id);
	out[5] = m->type;
	memcpy(out + 6, m->nonce, 16);
	out[22] = m->target;
	og_wr32(out + 23, m->a);
	og_wr32(out + 27, m->b);
	return OG_CHALLENGE_LEN;
}

size_t og_wire_write_response(uint8_t out[OG_RESPONSE_LEN], const og_response_msg *m)
{
	out[0] = OG_C_RESPONSE;
	og_wr32(out + 1, m->id);
	out[5] = m->type;
	out[6] = m->status;
	if (m->digest) memcpy(out + 7, m->digest, 32);
	else memset(out + 7, 0, 32);
	og_wr32(out + 39, m->a);
	og_wr32(out + 43, m->b);
	return OG_RESPONSE_LEN;
}

/* ---------------- PE canonical image ---------------- */

#define PE_SCN_CNT_INITIALIZED_DATA 0x00000040u
#define PE_SCN_MEM_DISCARDABLE      0x02000000u
#define PE_SCN_MEM_EXECUTE          0x20000000u
#define PE_SCN_MEM_WRITE            0x80000000u

int og_pe_section_included(uint32_t ch, int which)
{
	if (ch & (PE_SCN_MEM_WRITE | PE_SCN_MEM_DISCARDABLE)) return 0;
	if (ch & PE_SCN_MEM_EXECUTE) return (which & OG_CANON_CODE) != 0;
	return (which & OG_CANON_RDATA) && (ch & PE_SCN_CNT_INITIALIZED_DATA);
}

typedef struct { uint32_t va, vsize, raw, rawsize, ch; } pe_sec;

static uint32_t rva_to_off(const pe_sec *s, int ns, uint32_t rva, size_t n)
{
	int i;
	for (i = 0; i < ns; i++)
		if (rva >= s[i].va && rva < s[i].va + s[i].rawsize && (size_t)s[i].raw + (rva - s[i].va) < n)
			return s[i].raw + (rva - s[i].va);
	return 0;
}

size_t og_pe_canonical(const uint8_t *f, size_t n, int which, uint8_t **out)
{
	uint32_t lfanew, opt, nsec, i, rel_rva, rel_size, iat_rva, iat_size, total = 0, pos = 0;
	uint16_t optsize;
	pe_sec s[96];
	uint8_t *img;
	*out = NULL;
	if (n < 0x40 || f[0] != 'M' || f[1] != 'Z') return 0;
	lfanew = og_rd32(f + 0x3c);
	if ((size_t)lfanew + 24 > n || memcmp(f + lfanew, "PE\0\0", 4) != 0) return 0;
	nsec = og_rd16(f + lfanew + 6);
	optsize = og_rd16(f + lfanew + 20);
	opt = lfanew + 24;
	if (nsec == 0 || nsec > 96 || (size_t)opt + optsize + nsec * 40u > n || optsize < 96 + 6 * 8) return 0;
	if (og_rd16(f + opt) != 0x10b) return 0; /* PE32 only (the 2018 client and the guard are 32-bit) */
	rel_rva = og_rd32(f + opt + 96 + 5 * 8);
	rel_size = og_rd32(f + opt + 96 + 5 * 8 + 4);
	/* the import address table is written by the loader even when its section is read-only */
	iat_rva = optsize >= 96 + 13 * 8 ? og_rd32(f + opt + 96 + 12 * 8) : 0;
	iat_size = optsize >= 96 + 13 * 8 ? og_rd32(f + opt + 96 + 12 * 8 + 4) : 0;
	for (i = 0; i < nsec; i++) {
		const uint8_t *h = f + opt + optsize + i * 40;
		s[i].vsize = og_rd32(h + 8);
		s[i].va = og_rd32(h + 12);
		s[i].rawsize = og_rd32(h + 16);
		s[i].raw = og_rd32(h + 20);
		s[i].ch = og_rd32(h + 36);
		if (!s[i].vsize) s[i].vsize = s[i].rawsize;
		if ((size_t)s[i].raw + s[i].rawsize > n) s[i].rawsize = s[i].raw < n ? (uint32_t)(n - s[i].raw) : 0;
		if (s[i].vsize > 64u * 1024 * 1024) return 0;
		if (og_pe_section_included(s[i].ch, which)) total += s[i].vsize;
	}
	if (!total || total > 256u * 1024 * 1024) return 0;
	img = (uint8_t *)calloc(1, total);
	if (!img) return 0;
	for (i = 0; i < nsec; i++) {
		uint32_t copy, start;
		if (!og_pe_section_included(s[i].ch, which)) continue;
		start = pos;
		copy = s[i].rawsize < s[i].vsize ? s[i].rawsize : s[i].vsize;
		if (copy) memcpy(img + pos, f + s[i].raw, copy);
		pos += s[i].vsize;
		if (iat_size && iat_rva < s[i].va + s[i].vsize && iat_rva + iat_size > s[i].va) {
			uint32_t a = iat_rva > s[i].va ? iat_rva : s[i].va;
			uint32_t b = iat_rva + iat_size < s[i].va + s[i].vsize ? iat_rva + iat_size : s[i].va + s[i].vsize;
			memset(img + start + (a - s[i].va), 0, b - a);
		}
		/* zero every HIGHLOW slot inside this section */
		if (rel_rva && rel_size) {
			uint32_t r = rva_to_off(s, (int)nsec, rel_rva, n), end;
			if (r) {
				end = r + rel_size;
				if (end > n) end = (uint32_t)n;
				while (r + 8 <= end) {
					uint32_t page = og_rd32(f + r), bsize = og_rd32(f + r + 4), k;
					if (bsize < 8 || r + bsize > end) break;
					for (k = 8; k + 2 <= bsize; k += 2) {
						uint16_t e = og_rd16(f + r + k);
						uint32_t rva = page + (e & 0xFFF);
						if ((e >> 12) == 3 && rva >= s[i].va && rva + 4 <= s[i].va + s[i].vsize)
							memset(img + start + (rva - s[i].va), 0, 4);
					}
					r += bsize;
				}
			}
		}
	}
	*out = img;
	return total;
}
