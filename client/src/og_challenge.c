/* Challenge answers (OG_S_CHALLENGE -> OG_C_RESPONSE). The nonce challenge is part of core; the code range, time
 * and file range challenges are compiled in only when the profile selects them (OG_MODCH_CODE/TIME/FILE). The server
 * verifies every answer itself (build secret, release guard file, reference dump of the exe, official files). */
#include "og.h"
#include "../../third_party/monocypher.h"
#include <stdio.h>
#include <string.h>

#define FILE_RANGE_MAX (1024u * 1024)

static void keyed(const char *label, const uint8_t nonce[16], uint32_t a, uint32_t b, uint8_t out[32])
{
    uint8_t key[32], ab[8];
    crypto_blake2b_ctx c;
    og_profile_build_secret(key);
    og_wr32(ab, a);
    og_wr32(ab + 4, b);
    crypto_blake2b_keyed_init(&c, 32, key, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, strlen(label));
    crypto_blake2b_update(&c, nonce, 16);
    crypto_blake2b_update(&c, ab, 8);
    crypto_blake2b_final(&c, out);
    crypto_wipe(key, sizeof key);
}

#if OG_MODCH_FILE
static int file_range(uint8_t index, uint32_t off, uint32_t len, const uint8_t nonce[16], uint8_t out[32])
{
    static const char *const files[] = OG_PROFILE_FILES;
    char path[MAX_PATH];
    HANDLE h;
    LARGE_INTEGER size, pos;
    uint8_t *buf;
    DWORD got = 0;
    crypto_blake2b_ctx c;
    if (index >= OG_PROFILE_NFILES) return OG_CH_ST_RANGE;
    snprintf(path, sizeof path, "%s\\%s", og_client_dir, files[index]);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return OG_CH_ST_MISSING;
    if (!GetFileSizeEx(h, &size) || len == 0 || len > FILE_RANGE_MAX || (ULONGLONG)off + len > (ULONGLONG)size.QuadPart) {
        CloseHandle(h);
        return OG_CH_ST_RANGE;
    }
    buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, len);
    pos.QuadPart = off;
    if (!buf || !SetFilePointerEx(h, pos, NULL, FILE_BEGIN) || !ReadFile(h, buf, len, &got, NULL) || got != len) {
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(h);
        return OG_CH_ST_RANGE;
    }
    CloseHandle(h);
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, nonce, 16);
    crypto_blake2b_update(&c, buf, len);
    crypto_blake2b_final(&c, out);
    HeapFree(GetProcessHeap(), 0, buf);
    return OG_CH_ST_OK;
}
#endif

void og_challenge_handle(const og_challenge_msg *c, uint8_t out[OG_RESPONSE_LEN])
{
    og_response_msg r;
    uint8_t digest[32];
    memset(&r, 0, sizeof r);
    memset(digest, 0, sizeof digest);
    r.id = c->id;
    r.type = c->type;
    r.status = OG_CH_ST_OK;
    r.digest = digest;
    switch (c->type) {
    case OG_CH_NONCE:
        keyed("OG/nonce", c->nonce, 0, 0, digest);
        break;
#if OG_MODCH_CODE && OG_MOD_INTEGRITY
    case OG_CH_CODE_RANGE:
        r.a = c->a;
        r.b = c->b;
        r.status = (uint8_t)og_module_range(c->target ? (HMODULE)og_instance : GetModuleHandleA(NULL),
                                            c->target ? OG_CANON_CODE | OG_CANON_RDATA : OG_CANON_CODE, c->a, c->b, c->nonce, digest);
        break;
#endif
#if OG_MODCH_TIME
    case OG_CH_TIME: {
        LARGE_INTEGER f, q;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&q);
        r.a = GetTickCount();
        r.b = f.QuadPart ? (uint32_t)(q.QuadPart / (f.QuadPart / 1000)) : 0;
        keyed("OG/time", c->nonce, r.a, r.b, digest);
        break;
    }
#endif
#if OG_MODCH_FILE
    case OG_CH_FILE_RANGE:
        r.a = c->a;
        r.b = c->b;
        r.status = (uint8_t)file_range(c->target, c->a, c->b, c->nonce, digest);
        break;
#endif
    default:
        r.status = OG_CH_ST_UNKNOWN;
        break;
    }
    og_wire_write_response(out, &r);
}
