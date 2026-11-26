/* heartbeat module: guard thread with the window limit, the detection scan (cheat-signatures module), integrity
 * flags (integrity module) and the heartbeat (OG_C_ALIVE) with the exe code hash and the guard's own code +
 * read-only data hash (self integrity incl. the embedded profile). Flags that can come from harmless software
 * (hardware breakpoints, API hooks) are only sent when two scans in a row see them. */
#include "og.h"
#include <stdio.h>
#include <string.h>

static HANDLE g_wake;
static uint32_t g_seq;
static int g_window_slot_done;
static int g_grf_reported, g_api_reported;
static uint8_t g_prev_flags;
static volatile LONG g_generation, g_seen_generation;

/* a new server connection gets every current detection again */
void og_guard_new_connection(void) { InterlockedIncrement(&g_generation); og_guard_scan_now(); }

void og_guard_scan_now(void) { if (g_wake) SetEvent(g_wake); }

static void report(uint8_t kind, const char *text)
{
    uint8_t m[300];
    size_t n = strlen(text);
    if (n > 255) n = 255;
    m[0] = OG_C_DETECT;
    m[1] = 0; m[2] = 0;
    m[3] = kind;
    memcpy(m + 4, text, n);
    og_net_broadcast_control(m, n + 4);
    og_log("report kind %d: %s", kind, text);
}

static void apply_window_limit(void)
{
    int max;
    if (g_window_slot_done || !og_policy.received) return;
    max = og_policy.max_windows ? og_policy.max_windows : og_cfg.max_windows;
    g_window_slot_done = 1;
    if (max > 0 && !og_window_slot_acquire(max)) {
        og_log("window limit %d reached", max);
        og_message(og_tr(OG_T_MAX_WINDOWS), MB_OK | MB_ICONSTOP);
        TerminateProcess(GetCurrentProcess(), 1);
    }
}

static void heartbeat(void)
{
    uint8_t m[OG_ALIVE_LEN];
    char detail[MAX_PATH + 32];
    int unknown;
    uint8_t flags = 0, raw;
    if (!og_net_active_connections()) return;
    if (g_seen_generation != g_generation) {
        g_seen_generation = g_generation;
        og_detect_reset();
        og_assets_reset();
        g_grf_reported = g_api_reported = 0;
    }

    unknown = og_detect_scan();
    if (!og_net_hooks_intact()) flags |= OG_ALIVE_HOOKS_TAMPERED;
    raw = og_integrity_flags();
    flags |= raw & g_prev_flags;               /* confirmed by two scans */
    g_prev_flags = raw;
#if OG_MOD_INTEGRITY
    if ((flags & OG_ALIVE_API_HOOKED) && !g_api_reported) { g_api_reported = 1; report(OG_KIND_API_HOOK, og_integrity_detail); }
#endif
    if (og_assets_check(detail, sizeof detail)) {
        flags |= OG_ALIVE_GRF_VIOLATION;
        if (!g_grf_reported) { g_grf_reported = 1; report(OG_KIND_GRF, detail); }
    }

    if (og_lgp_palette_active()) flags |= OG_ALIVE_LGP_PALETTE;
    if (og_darkpack_active()) flags |= OG_ALIVE_DARKPACK;

    memset(m, 0, sizeof m);
    m[0] = OG_C_ALIVE;
    g_seq++;
    og_wr32(m + 1, g_seq);
    og_hash_module_code(GetModuleHandleA(NULL), OG_CANON_CODE, m + 5);
    og_wr16(m + 37, (uint16_t)unknown);
    m[39] = flags;
    og_hash_module_code(og_instance, OG_CANON_CODE | OG_CANON_RDATA, m + 40);
    og_net_broadcast_control(m, sizeof m);
}

static DWORD WINAPI guard_thread(LPVOID p)
{
    (void)p;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
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
