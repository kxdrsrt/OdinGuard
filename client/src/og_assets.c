/* grf-rules module.
 * 1. GRF manifest (operator list from the server, OG_KIND_GRF_LIST / GRF_MOD): every GRF in DATA.INI must be a listed
 *    version (size + tail hash, checked on every scan, cached by size and write time; the full-file hash of listed
 *    versions is checked once in a background thread) or an allowed mod (tail hash, any name). Unknown, modified and
 *    missing required GRFs are reported (OG_KIND_GRF_UNKNOWN / GRF_MODIFIED / GRF_MISSING); the server decides.
 *    Several accepted versions per name let the operator publish the new hashes before a patch goes out.
 * 2. Loose files (grf_strict): game files outside the GRFs (no-delay ACTs, transparent walls) are reported unless
 *    they are in an overlay folder (built-in ones plus OG_KIND_OVERLAY from the server). */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_LIST 128
#define MAX_REPORTED 64

typedef struct { char name[64]; unsigned long long size; char tail[65]; char full[65]; int required; } grf_ver;
/* full_state: 0 not queued, 1 queued, 2 hashing, 3 done; full_bad: the whole-file hash differed (re-reported on every
 * connection, the login connection may close before the map connection starts) */
typedef struct { char path[MAX_PATH]; unsigned long long size; FILETIME mtime; char tail[65]; char full[65]; int full_state, full_bad; } grf_cache;

static const char *g_builtin_overlay[] = { "data\\odinguard\\", "data\\texture\\effect\\lgp\\", "data\\texture\\effect\\aura\\" };
static const char *g_protected_ext[] = { ".spr", ".act", ".str", ".gat", ".gnd", ".rsw", ".rsm", ".lub", ".lua",
                                         ".imf", ".pal", ".bmp", ".tga", ".txt", ".xml", ".grf" };

static grf_ver g_list[MAX_LIST];
static int g_nlist;
static char g_mods[MAX_LIST][65];
static int g_nmods;
static char g_overlay[32][128];
static int g_noverlay;
static grf_cache g_cache[32];
static int g_ncache;
static char g_reported[MAX_REPORTED][80];
static int g_nreported;
static CRITICAL_SECTION g_lock;
static volatile LONG g_lock_init;
static HANDLE g_worker;

static void lock_init(void)
{
    if (InterlockedCompareExchange(&g_lock_init, 1, 0) == 0) InitializeCriticalSection(&g_lock);
    while (g_lock_init != 1) Sleep(0);
}

void og_assets_reset(void)
{
    lock_init();
    EnterCriticalSection(&g_lock);
    g_nreported = 0;
    LeaveCriticalSection(&g_lock);
}

static void report_once(uint8_t kind, const char *text)
{
    uint8_t m[300];
    char key[80];
    size_t n = strlen(text);
    int i;
    snprintf(key, sizeof key, "%u:%s", kind, text);
    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_nreported; i++) if (!strcmp(g_reported[i], key)) { LeaveCriticalSection(&g_lock); return; }
    if (g_nreported < MAX_REPORTED) snprintf(g_reported[g_nreported++], sizeof g_reported[0], "%s", key);
    LeaveCriticalSection(&g_lock);
    if (n > 255) n = 255;
    m[0] = OG_C_DETECT;
    m[1] = m[2] = 0;
    m[3] = kind;
    memcpy(m + 4, text, n);
    og_net_broadcast_control(m, n + 4);
    og_log("grf rule kind %d: %s", kind, text);
}

/* "a|b|c" -> fields (in place) */
static int split(char *s, char **f, int max)
{
    int n = 0;
    f[n++] = s;
    while (*s && n < max) { if (*s == '|') { *s = 0; f[n++] = s + 1; } s++; }
    return n;
}

static void load_policy(void)
{
    int i;
    g_nlist = g_nmods = g_noverlay = 0;
    EnterCriticalSection(&og_policy_lock);
    for (i = 0; i < og_policy.nsig; i++) {
        char tmp[256], *f[5];
        const og_signature *s = &og_policy.sig[i];
        snprintf(tmp, sizeof tmp, "%s", s->pattern);
        if (s->kind == OG_KIND_GRF_LIST && g_nlist < MAX_LIST && split(tmp, f, 5) == 5 && strlen(f[2]) == 64) {
            grf_ver *v = &g_list[g_nlist++];
            snprintf(v->name, sizeof v->name, "%s", f[0]);
            v->size = strtoull(f[1], NULL, 10);
            snprintf(v->tail, sizeof v->tail, "%s", f[2]);
            snprintf(v->full, sizeof v->full, "%s", strlen(f[3]) == 64 ? f[3] : "");
            v->required = f[4][0] == 'r';
        } else if (s->kind == OG_KIND_GRF_MOD && g_nmods < MAX_LIST && split(tmp, f, 2) >= 1 && strlen(f[0]) == 64) {
            snprintf(g_mods[g_nmods++], 65, "%s", f[0]);
        } else if (s->kind == OG_KIND_OVERLAY && g_noverlay < 32) {
            snprintf(g_overlay[g_noverlay++], 128, "%s", s->pattern);
        }
    }
    LeaveCriticalSection(&og_policy_lock);
}

/* SHA-256 of the 46-byte header plus everything from the file table to the end */
static int tail_hash(HANDLE f, unsigned long long size, char hex[65])
{
    uint8_t h[46], buf[65536], out[32];
    unsigned long long off, pos;
    DWORD got;
    LARGE_INTEGER li;
    og_sha256_ctx c;
    if (size < 46 || !ReadFile(f, h, 46, &got, NULL) || got != 46) return 0;
    {
        uint32_t version = h[42] | (h[43] << 8) | ((uint32_t)h[44] << 16) | ((uint32_t)h[45] << 24);
        if (version == 0x300 && !h[35] && !h[36] && !h[37]) {
            off = 0;
            for (pos = 0; pos < 8; pos++) off |= (unsigned long long)h[30 + pos] << (8 * pos);
        } else {
            off = h[30] | (h[31] << 8) | ((uint32_t)h[32] << 16) | ((uint32_t)h[33] << 24);
        }
    }
    og_sha256_init(&c);
    og_sha256_update(&c, h, 46);
    pos = 46 + off;
    if (pos < size) {
        li.QuadPart = (LONGLONG)pos;
        if (!SetFilePointerEx(f, li, NULL, FILE_BEGIN)) return 0;
        while (ReadFile(f, buf, sizeof buf, &got, NULL) && got) og_sha256_update(&c, buf, got);
    }
    og_sha256_final(&c, out);
    og_hex(out, 32, hex);
    return 1;
}

static grf_cache *cached(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    unsigned long long size;
    grf_cache *e = NULL;
    HANDLE f;
    int i;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return NULL;
    size = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    for (i = 0; i < g_ncache; i++)
        if (!_stricmp(g_cache[i].path, path)) { e = &g_cache[i]; break; }
    if (e && e->size == size && CompareFileTime(&e->mtime, &a.ftLastWriteTime) == 0) return e;
    if (!e) {
        if (g_ncache >= (int)(sizeof g_cache / sizeof *g_cache)) return NULL;
        e = &g_cache[g_ncache++];
    }
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    e->size = size;
    e->mtime = a.ftLastWriteTime;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { e->path[0] = 0; return NULL; }
    if (!tail_hash(f, size, e->tail)) e->tail[0] = 0;
    CloseHandle(f);
    return e;
}

/* background: full hashes of listed versions (one file at a time, low priority) */
static DWORD WINAPI full_worker(LPVOID arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    for (;;) {
        char path[MAX_PATH], want[65], name[64];
        int i, job = -1;
        EnterCriticalSection(&g_lock);
        for (i = 0; i < g_ncache; i++) if (g_cache[i].full_state == 1) { job = i; break; }
        if (job >= 0) {
            g_cache[job].full_state = 2;
            snprintf(path, sizeof path, "%s", g_cache[job].path);
            snprintf(want, sizeof want, "%s", g_cache[job].full);
        }
        LeaveCriticalSection(&g_lock);
        if (job < 0) { Sleep(2000); continue; }
        {
            char got[65];
            const char *base = strrchr(path, '\\');
            snprintf(name, sizeof name, "%s", base ? base + 1 : path);
            int bad = og_sha256_file(path, got) && _stricmp(got, want);
            EnterCriticalSection(&g_lock);
            if (!_stricmp(g_cache[job].path, path)) { g_cache[job].full_state = 3; g_cache[job].full_bad = bad; }
            LeaveCriticalSection(&g_lock);
            if (bad) {
                char t[160];
                snprintf(t, sizeof t, "%s (content)", name);
                report_once(OG_KIND_GRF_MODIFIED, t);
            }
        }
    }
    return 0;
}

static volatile LONG g_darkpack; /* odin-dark.grf is in DATA.INI and is the accepted version */

int og_darkpack_active(void) { return g_darkpack != 0; }

static void check_manifest(void)
{
    int dark = 0;
    char ini[MAX_PATH], key[8], val[MAX_PATH], full[MAX_PATH];
    int seen_list[MAX_LIST];
    int i, k;
    memset(seen_list, 0, sizeof seen_list);
    snprintf(ini, sizeof ini, "%s\\DATA.INI", og_client_dir);
    for (k = 0; k < 32; k++) {
        grf_cache *e;
        int listed = 0, required = 0, match = -1;
        snprintf(key, sizeof key, "%d", k);
        GetPrivateProfileStringA("Data", key, "", val, sizeof val, ini);
        if (!val[0]) continue;
        if (strchr(val, '\\') || strchr(val, '/') || strstr(val, "..")) { report_once(OG_KIND_GRF_UNKNOWN, val); continue; }
        snprintf(full, sizeof full, "%s\\%s", og_client_dir, val);
        EnterCriticalSection(&g_lock);
        e = cached(full);
        for (i = 0; i < g_nlist; i++) {
            if (_stricmp(g_list[i].name, val)) continue;
            listed = 1;
            required |= g_list[i].required;
            seen_list[i] = 1;
            if (e && e->size == g_list[i].size && !_stricmp(e->tail, g_list[i].tail)) match = i;
        }
        if (e && match >= 0) {
            for (i = 0; i < g_nlist; i++) if (!_stricmp(g_list[i].name, val)) seen_list[i] = 2;
            if (g_list[match].full[0] && e->full_state == 0) {
                snprintf(e->full, sizeof e->full, "%s", g_list[match].full);
                e->full_state = 1;
            }
        }
        LeaveCriticalSection(&g_lock);
        if (!e) {
            if (required) report_once(OG_KIND_GRF_MISSING, val);
            continue;
        }
        if (match >= 0 && !_stricmp(val, "odin-dark.grf")) dark = 1;
        if (match >= 0) {
            if (e->full_state == 3 && e->full_bad) {
                char t[160];
                snprintf(t, sizeof t, "%s (content)", val);
                report_once(OG_KIND_GRF_MODIFIED, t);
            }
            continue;
        }
        for (i = 0; i < g_nmods; i++) if (!_stricmp(g_mods[i], e->tail)) break;
        if (i < g_nmods) continue;
        report_once(listed ? OG_KIND_GRF_MODIFIED : OG_KIND_GRF_UNKNOWN, val);
    }
    for (i = 0; i < g_nlist; i++)
        if (g_list[i].required && !seen_list[i]) report_once(OG_KIND_GRF_MISSING, g_list[i].name);
    InterlockedExchange(&g_darkpack, dark);
    if (!g_worker) g_worker = CreateThread(NULL, 0, full_worker, NULL, 0, NULL);
}

static int ends_with_i(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && _stricmp(s + a - b, suf) == 0;
}

static int is_overlay(const char *rel)
{
    size_t i;
    int k;
    for (i = 0; i < sizeof g_builtin_overlay / sizeof *g_builtin_overlay; i++)
        if (_strnicmp(rel, g_builtin_overlay[i], strlen(g_builtin_overlay[i])) == 0) return 1;
    for (k = 0; k < g_noverlay; k++)
        if (g_overlay[k][0] && _strnicmp(rel, g_overlay[k], strlen(g_overlay[k])) == 0) return 1;
    return 0;
}

static int walk(const char *root, const char *rel, char *detail, size_t cap, int depth)
{
    char mask[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    if (depth > 12) return 0;
    snprintf(mask, sizeof mask, "%s\\%s*", root, rel);
    h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char sub[MAX_PATH];
        size_t i;
        if (fd.cFileName[0] == '.') continue;
        snprintf(sub, sizeof sub, "%s%s", rel, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            strncat(sub, "\\", sizeof sub - strlen(sub) - 1);
            if (!is_overlay(sub)) found = walk(root, sub, detail, cap, depth + 1);
        } else if (!is_overlay(sub)) {
            for (i = 0; i < sizeof g_protected_ext / sizeof *g_protected_ext; i++) {
                if (ends_with_i(fd.cFileName, g_protected_ext[i])) {
                    snprintf(detail, cap, "loose file %s", sub);
                    found = 1;
                    break;
                }
            }
        }
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

int og_assets_check(char *detail, size_t cap)
{
    lock_init();
    load_policy();
    if (g_nlist || g_nmods) check_manifest();
    if (cap) detail[0] = 0;
    return og_policy.grf_strict ? walk(og_client_dir, "data\\", detail, cap, 0) : 0;
}

/* Studio / tests: tail hash of one file ("" on error) */
__declspec(dllexport) int OdinGuard_GrfTailHash(const char *path, char hex[65])
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    int ok;
    hex[0] = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = GetFileSizeEx(f, &sz) && tail_hash(f, (unsigned long long)sz.QuadPart, hex);
    CloseHandle(f);
    return ok;
}
