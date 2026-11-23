/* Guard thread: heartbeat, tool signatures, injected modules, debugger, hook integrity, resource rules,
 * window limit from the server policy. */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

og_policy_t og_policy;
CRITICAL_SECTION og_policy_lock;

static HANDLE g_wake;
static uint32_t g_seq;
static uint8_t g_reported[256];        /* per signature id: already reported */
static char g_reported_mods[64][MAX_PATH];
static int g_nreported_mods;
static int g_window_slot_done;
static int g_grf_reported;
static volatile LONG g_generation, g_seen_generation;

/* a new server connection gets every current detection again */
void og_guard_new_connection(void) { InterlockedIncrement(&g_generation); og_guard_scan_now(); }

void og_guard_scan_now(void) { if (g_wake) SetEvent(g_wake); }

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

static int contains_i(const char *hay, const char *needle)
{
    size_t n = strlen(needle), h = strlen(hay), i;
    if (!n || n > h) return 0;
    for (i = 0; i + n <= h; i++)
        if (_strnicmp(hay + i, needle, n) == 0) return 1;
    return 0;
}

typedef struct { og_signature *sig; int nsig; } win_ctx;

static BOOL CALLBACK enum_win(HWND w, LPARAM lp)
{
    win_ctx *c = (win_ctx *)lp;
    char title[256];
    int i;
    if (!IsWindowVisible(w) || !GetWindowTextA(w, title, sizeof title)) return TRUE;
    for (i = 0; i < c->nsig; i++) {
        og_signature *s = &c->sig[i];
        if (s->kind != OG_KIND_WINDOW || g_reported[s->id & 255]) continue;
        if (contains_i(title, s->pattern)) { g_reported[s->id & 255] = 1; detect(s->id, OG_KIND_WINDOW, title); }
    }
    return TRUE;
}

static void scan_signatures(void)
{
    static og_signature sig[256];
    int nsig, i;
    HANDLE snap;
    win_ctx wc;

    EnterCriticalSection(&og_policy_lock);
    nsig = og_policy.nsig;
    memcpy(sig, og_policy.sig, sizeof(og_signature) * nsig);
    LeaveCriticalSection(&og_policy_lock);
    if (!nsig) return;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof pe;
        if (Process32First(snap, &pe)) do {
            for (i = 0; i < nsig; i++) {
                if (sig[i].kind != OG_KIND_PROCESS || g_reported[sig[i].id & 255]) continue;
                if (contains_i(pe.szExeFile, sig[i].pattern)) { g_reported[sig[i].id & 255] = 1; detect(sig[i].id, OG_KIND_PROCESS, pe.szExeFile); }
            }
        } while (Process32Next(snap, &pe));
        CloseHandle(snap);
    }

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me;
        me.dwSize = sizeof me;
        if (Module32First(snap, &me)) do {
            for (i = 0; i < nsig; i++) {
                if (sig[i].kind != OG_KIND_MODULE || g_reported[sig[i].id & 255]) continue;
                if (contains_i(me.szModule, sig[i].pattern)) { g_reported[sig[i].id & 255] = 1; detect(sig[i].id, OG_KIND_MODULE, me.szExePath); }
            }
        } while (Module32Next(snap, &me));
        CloseHandle(snap);
    }

    wc.sig = sig;
    wc.nsig = nsig;
    EnumWindows(enum_win, (LPARAM)&wc);
}

/* known third-party modules that hook every process and must not be treated as injection
   (Warsaw / GAS Tecnologia banking protection in Brazil, overlay and input tools) */
static int module_tolerated(const char *path)
{
    static const char *ok[] = { "\\Warsaw\\", "\\GAS Tecnologia\\", "\\Diebold\\", "\\Topaz OFD\\",
                                "\\NVIDIA Corporation\\", "\\AMD\\", "\\Steam\\", "\\Discord\\", "\\Overwolf\\",
                                "\\Parallels\\", "\\Microsoft\\", "\\Windows Defender\\", "\\RivaTuner", "\\MSI Afterburner\\" };
    size_t i;
    for (i = 0; i < sizeof ok / sizeof *ok; i++) if (contains_i(path, ok[i])) return 1;
    return 0;
}

static int scan_injected(void)
{
    char *list = (char *)HeapAlloc(GetProcessHeap(), 0, 32768), *line, *ctx = NULL;
    int unknown = 0;
    if (!list) return 0;
    og_scan_modules(list, 32768, &unknown);
    for (line = strtok_r(list, "\n", &ctx); line; line = strtok_r(NULL, "\n", &ctx)) {
        int i, seen = 0;
        if (line[0] != '?') continue;
        line += 2;
        if (module_tolerated(line)) { unknown--; continue; }
        for (i = 0; i < g_nreported_mods; i++) if (!_stricmp(g_reported_mods[i], line)) { seen = 1; break; }
        if (seen || g_nreported_mods >= 64) continue;
        strncpy(g_reported_mods[g_nreported_mods++], line, MAX_PATH - 1);
        detect(0, OG_KIND_INJECT, line);
    }
    HeapFree(GetProcessHeap(), 0, list);
    return unknown < 0 ? 0 : unknown;
}

static int debugger_present(void)
{
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return IsDebuggerPresent() || remote;
}

static void apply_window_limit(void)
{
    int max;
    if (g_window_slot_done || !og_policy.received) return;
    max = og_policy.max_windows ? og_policy.max_windows : og_cfg.max_windows;
    g_window_slot_done = 1;
    if (max > 0 && !og_window_slot_acquire(max)) {
        og_log("window limit %d reached", max);
        MessageBoxA(NULL, "The maximum number of game windows on this computer is reached.", "OdinGuard", MB_OK | MB_ICONSTOP | MB_TOPMOST);
        TerminateProcess(GetCurrentProcess(), 1);
    }
}

static void heartbeat(void)
{
    uint8_t m[40], exe[32];
    char hex[65], detail[MAX_PATH + 32];
    int unknown, i;
    uint8_t flags = 0;
    if (!og_net_active_connections()) return;
    if (g_seen_generation != g_generation) {
        g_seen_generation = g_generation;
        memset(g_reported, 0, sizeof g_reported);
        g_nreported_mods = 0;
        g_grf_reported = 0;
    }

    unknown = scan_injected();
    scan_signatures();
    if (!og_net_hooks_intact()) flags |= OG_ALIVE_HOOKS_TAMPERED;
    if (debugger_present()) flags |= OG_ALIVE_DEBUGGER;
    if (og_policy.grf_strict && og_assets_check(detail, sizeof detail)) {
        flags |= OG_ALIVE_GRF_VIOLATION;
        if (!g_grf_reported) { g_grf_reported = 1; detect(0, OG_KIND_GRF, detail); }
    }

    memset(exe, 0, sizeof exe);
    if (og_hash_module_code(GetModuleHandleA(NULL), hex))
        for (i = 0; i < 32; i++) { unsigned v; sscanf(hex + i * 2, "%2x", &v); exe[i] = (uint8_t)v; }
    m[0] = OG_C_ALIVE;
    g_seq++;
    memcpy(m + 1, &g_seq, 4);
    memcpy(m + 5, exe, 32);
    m[37] = (uint8_t)(unknown & 0xFF); m[38] = (uint8_t)(unknown >> 8);
    m[39] = flags;
    og_net_broadcast_control(m, sizeof m);
}

static DWORD WINAPI guard_thread(LPVOID p)
{
    (void)p;
    for (;;) {
        DWORD interval = og_policy.alive_interval > 0 ? (DWORD)og_policy.alive_interval * 1000 : 30000;
        WaitForSingleObject(g_wake, og_net_active_connections() ? interval : 2000);
        apply_window_limit();
        heartbeat();
    }
    return 0;
}

void og_guard_start(void)
{
    g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, guard_thread, NULL, 0, NULL));
}
