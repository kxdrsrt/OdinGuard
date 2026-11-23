/* cheat-signatures module: matches the signed signature package the server sends (OG_S_SIGS) against running
 * processes, loaded modules, windows, files in the client folder, kernel drivers and the GRFs of DATA.INI, and
 * reports modules loaded from outside the client and Windows folders. Every hit is reported once per connection
 * (OG_C_DETECT); the server decides what happens (signature confidence, severity, action, warn mode).
 * Allowlist entries (OG_KIND_ALLOW_SHA256 / OG_KIND_ALLOW_PATH) are never reported. */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

#define HASH_CACHE 256
#define GRF_HASH_MAX (256u * 1024 * 1024) /* larger archives (data.grf) are not hashed */

static uint8_t g_reported[65536 / 8];
static char g_reported_mods[64][MAX_PATH];
static int g_nreported_mods;
static og_signature g_sig[OG_MAX_SIGS];
static int g_nsig;

typedef struct { char path[MAX_PATH]; DWORD size_lo, size_hi; FILETIME mtime; char hex[65]; } hash_entry;
static hash_entry g_cache[HASH_CACHE];
static int g_ncache;

void og_detect_reset(void)
{
    memset(g_reported, 0, sizeof g_reported);
    g_nreported_mods = 0;
}

static int seen_peek(uint16_t id) { return (g_reported[id >> 3] >> (id & 7)) & 1; }
static int seen(uint16_t id) { int r = (g_reported[id >> 3] >> (id & 7)) & 1; g_reported[id >> 3] |= (uint8_t)(1 << (id & 7)); return r; }

static void detect(uint16_t id, uint8_t kind, const char *text)
{
    uint8_t m[300];
    size_t n = strlen(text);
    if (n > 255) n = 255;
    m[0] = OG_C_DETECT;
    m[1] = (uint8_t)id; m[2] = (uint8_t)(id >> 8);
    m[3] = kind;
    memcpy(m + 4, text, n);
    og_net_broadcast_control(m, n + 4);
    og_log("detect kind %d id %d: %s", kind, id, text);
}

/* sha256 of a file, cached by path, size and modification time */
static const char *file_hash(const char *path, DWORD max_size)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    int i;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return NULL;
    if (a.nFileSizeHigh || a.nFileSizeLow > max_size) return NULL;
    for (i = 0; i < g_ncache; i++) {
        hash_entry *e = &g_cache[i];
        if (!_stricmp(e->path, path) && e->size_lo == a.nFileSizeLow && e->size_hi == a.nFileSizeHigh &&
            CompareFileTime(&e->mtime, &a.ftLastWriteTime) == 0)
            return e->hex;
    }
    i = g_ncache < HASH_CACHE ? g_ncache++ : (int)(GetTickCount() % HASH_CACHE);
    if (!og_sha256_file(path, g_cache[i].hex)) { g_cache[i].path[0] = 0; return NULL; }
    strncpy(g_cache[i].path, path, MAX_PATH - 1);
    g_cache[i].size_lo = a.nFileSizeLow;
    g_cache[i].size_hi = a.nFileSizeHigh;
    g_cache[i].mtime = a.ftLastWriteTime;
    return g_cache[i].hex;
}

static int allowed_hash(const char *hex)
{
    int i;
    if (!hex) return 0;
    for (i = 0; i < g_nsig; i++)
        if (g_sig[i].kind == OG_KIND_ALLOW_SHA256 && !_stricmp(g_sig[i].pattern, hex)) return 1;
    return 0;
}

static void match_hash(const char *hex, const char *where)
{
    int i;
    if (!hex || allowed_hash(hex)) return;
    for (i = 0; i < g_nsig; i++)
        if (g_sig[i].kind == OG_KIND_FILE_SHA256 && !_stricmp(g_sig[i].pattern, hex) && !seen(g_sig[i].id))
            detect(g_sig[i].id, OG_KIND_FILE_SHA256, where);
}

static int has_kind(uint8_t kind)
{
    int i;
    for (i = 0; i < g_nsig; i++) if (g_sig[i].kind == kind) return 1;
    return 0;
}

static BOOL CALLBACK enum_win(HWND w, LPARAM lp)
{
    char title[256], cls[256];
    int i;
    (void)lp;
    title[0] = cls[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    if (IsWindowVisible(w)) GetWindowTextA(w, title, sizeof title);
    for (i = 0; i < g_nsig; i++) {
        og_signature *s = &g_sig[i];
        if (s->kind == OG_KIND_WINDOW && title[0] && og_contains_i(title, s->pattern) && !seen(s->id)) detect(s->id, OG_KIND_WINDOW, title);
        else if (s->kind == OG_KIND_WINDOW_CLASS && cls[0] && !_stricmp(cls, s->pattern) && !seen(s->id)) detect(s->id, OG_KIND_WINDOW_CLASS, cls);
    }
    return TRUE;
}

static void scan_processes(void)
{
    PROCESSENTRY32 pe;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int i;
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof pe;
    if (Process32First(snap, &pe)) do {
        for (i = 0; i < g_nsig; i++)
            if (g_sig[i].kind == OG_KIND_PROCESS && og_contains_i(pe.szExeFile, g_sig[i].pattern) && !seen(g_sig[i].id))
                detect(g_sig[i].id, OG_KIND_PROCESS, pe.szExeFile);
    } while (Process32Next(snap, &pe));
    CloseHandle(snap);
}

/* loaded modules: name signatures, file hashes, and modules from outside the client and Windows folders */
static int scan_modules(void)
{
    MODULEENTRY32 me;
    char win[MAX_PATH];
    int i, unknown = 0, want_hash = has_kind(OG_KIND_FILE_SHA256);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    GetWindowsDirectoryA(win, sizeof win);
    me.dwSize = sizeof me;
    if (Module32First(snap, &me)) do {
        const char *hex = NULL;
        for (i = 0; i < g_nsig; i++)
            if (g_sig[i].kind == OG_KIND_MODULE && og_contains_i(me.szModule, g_sig[i].pattern) && !seen(g_sig[i].id))
                detect(g_sig[i].id, OG_KIND_MODULE, me.szExePath);
        if (want_hash || g_nsig) hex = file_hash(me.szExePath, 64u * 1024 * 1024);
        if (want_hash) match_hash(hex, me.szExePath);
        if (_strnicmp(me.szExePath, og_client_dir, strlen(og_client_dir)) && _strnicmp(me.szExePath, win, strlen(win)) &&
            !og_path_tolerated(me.szExePath) && !allowed_hash(hex)) {
            int k, dup = 0;
            unknown++;
            for (k = 0; k < g_nreported_mods; k++) if (!_stricmp(g_reported_mods[k], me.szExePath)) { dup = 1; break; }
            if (!dup && g_nreported_mods < 64) {
                snprintf(g_reported_mods[g_nreported_mods++], MAX_PATH, "%s", me.szExePath);
                detect(0, OG_KIND_INJECT, me.szExePath);
            }
        }
    } while (Module32Next(snap, &me));
    CloseHandle(snap);
    return unknown;
}

/* files in the client folder (top level): names and hashes */
static void scan_files(void)
{
    char mask[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int i, want_name = has_kind(OG_KIND_FILE_NAME), want_hash = has_kind(OG_KIND_FILE_SHA256);
    if (!want_name && !want_hash) return;
    snprintf(mask, sizeof mask, "%s\\*", og_client_dir);
    h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        snprintf(full, sizeof full, "%s\\%s", og_client_dir, fd.cFileName);
        if (want_name)
            for (i = 0; i < g_nsig; i++)
                if (g_sig[i].kind == OG_KIND_FILE_NAME && !_stricmp(fd.cFileName, g_sig[i].pattern) &&
                    !allowed_hash(file_hash(full, 64u * 1024 * 1024)) && !seen(g_sig[i].id))
                    detect(g_sig[i].id, OG_KIND_FILE_NAME, fd.cFileName);
        if (want_hash && fd.nFileSizeHigh == 0 && fd.nFileSizeLow <= 64u * 1024 * 1024) match_hash(file_hash(full, 64u * 1024 * 1024), fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void scan_grfs(void)
{
    char ini[MAX_PATH], key[8], val[MAX_PATH], full[MAX_PATH];
    int i, k;
    if (!has_kind(OG_KIND_GRF_SHA256)) return;
    snprintf(ini, sizeof ini, "%s\\DATA.INI", og_client_dir);
    for (k = 0; k < 32; k++) {
        const char *hex;
        snprintf(key, sizeof key, "%d", k);
        GetPrivateProfileStringA("Data", key, "", val, sizeof val, ini);
        if (!val[0] || strchr(val, '\\') || strchr(val, '/')) continue;
        snprintf(full, sizeof full, "%s\\%s", og_client_dir, val);
        if (!(hex = file_hash(full, GRF_HASH_MAX))) continue;
        for (i = 0; i < g_nsig; i++)
            if (g_sig[i].kind == OG_KIND_GRF_SHA256 && !_stricmp(g_sig[i].pattern, hex) && !seen(g_sig[i].id))
                detect(g_sig[i].id, OG_KIND_GRF_SHA256, val);
    }
}

static void scan_drivers(void)
{
    SC_HANDLE scm;
    ENUM_SERVICE_STATUSA *buf;
    DWORD need = 0, count = 0, resume = 0, i;
    int k;
    if (!has_kind(OG_KIND_DRIVER)) return;
    if (!(scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE))) return;
    EnumServicesStatusA(scm, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE, NULL, 0, &need, &count, &resume);
    if (need && (buf = (ENUM_SERVICE_STATUSA *)HeapAlloc(GetProcessHeap(), 0, need))) {
        resume = 0;
        if (EnumServicesStatusA(scm, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE, buf, need, &need, &count, &resume))
            for (i = 0; i < count; i++)
                for (k = 0; k < g_nsig; k++)
                    if (g_sig[k].kind == OG_KIND_DRIVER && !_stricmp(buf[i].lpServiceName, g_sig[k].pattern) && !seen(g_sig[k].id))
                        detect(g_sig[k].id, OG_KIND_DRIVER, buf[i].lpServiceName);
        HeapFree(GetProcessHeap(), 0, buf);
    }
    CloseServiceHandle(scm);
}

/* named kernel objects a tool creates (single-instance mutexes, events, shared memory) */
static int object_exists(const char *name)
{
    HANDLE h;
    if ((h = OpenMutexA(SYNCHRONIZE, FALSE, name))) { CloseHandle(h); return 1; }
    if ((h = OpenEventA(SYNCHRONIZE, FALSE, name))) { CloseHandle(h); return 1; }
    if ((h = OpenFileMappingA(FILE_MAP_READ, FALSE, name))) { CloseHandle(h); return 1; }
    return 0;
}

static void scan_objects(void)
{
    char g[300];
    int i;
    for (i = 0; i < g_nsig; i++) {
        if (g_sig[i].kind != OG_KIND_MUTEX || seen_peek(g_sig[i].id)) continue;
        snprintf(g, sizeof g, "Global\\%s", g_sig[i].pattern);
        if ((object_exists(g_sig[i].pattern) || object_exists(g)) && !seen(g_sig[i].id)) detect(g_sig[i].id, OG_KIND_MUTEX, g_sig[i].pattern);
    }
}

/* registry keys a tool leaves behind: "HKCU\\path" or "HKLM\\path" (32- and 64-bit views) */
static void scan_registry(void)
{
    int i;
    for (i = 0; i < g_nsig; i++) {
        const char *p = g_sig[i].pattern;
        HKEY root, k;
        if (g_sig[i].kind != OG_KIND_REGISTRY || seen_peek(g_sig[i].id)) continue;
        if (!_strnicmp(p, "HKCU\\", 5)) root = HKEY_CURRENT_USER;
        else if (!_strnicmp(p, "HKLM\\", 5)) root = HKEY_LOCAL_MACHINE;
        else continue;
        if (RegOpenKeyExA(root, p + 5, 0, KEY_READ, &k) == ERROR_SUCCESS ||
            RegOpenKeyExA(root, p + 5, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
            RegCloseKey(k);
            if (!seen(g_sig[i].id)) detect(g_sig[i].id, OG_KIND_REGISTRY, p);
        }
    }
}

int og_detect_scan(void)
{
    int unknown;
    EnterCriticalSection(&og_policy_lock);
    g_nsig = og_policy.nsig;
    memcpy(g_sig, og_policy.sig, sizeof(og_signature) * g_nsig);
    LeaveCriticalSection(&og_policy_lock);
    unknown = scan_modules();
    if (!g_nsig) return unknown;
    scan_processes();
    EnumWindows(enum_win, 0);
    scan_files();
    scan_grfs();
    scan_drivers();
    scan_objects();
    scan_registry();
    return unknown;
}
