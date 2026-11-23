/* Client commands sent by the server after the player typed them in chat (!vsync, !crash).
 * !vsync removes the frame cap: the main loop of the 2018 client (CModeMgr::Run) calls Sleep(1) after every
 * frame on the main thread; while unlocked that call returns at once. */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include "../../third_party/minhook/include/MinHook.h"

typedef VOID (WINAPI *sleep_t)(DWORD);
static sleep_t o_sleep;
static volatile LONG g_unlocked;
static volatile LONG g_skipped;

static VOID WINAPI h_sleep(DWORD ms)
{
    if (g_unlocked && ms == 1 && GetCurrentThreadId() == og_main_thread) {
        InterlockedIncrement(&g_skipped);
        SwitchToThread();
        return;
    }
    o_sleep(ms);
}

int og_cmds_init(void)
{
    void *target = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep");
    if (!target || MH_CreateHook(target, (void *)h_sleep, (void **)&o_sleep) != MH_OK) return 0;
    return MH_EnableHook(target) == MH_OK;
}

void og_cmd_handle(const char *cmd, char *reply, size_t cap)
{
    if (!_strnicmp(cmd, "vsync", 5)) {
        /* "vsync" toggles, "vsync off" removes the frame cap, "vsync on" restores it */
        LONG want = !_stricmp(cmd, "vsync off") ? 1 : !_stricmp(cmd, "vsync on") ? 0 : !g_unlocked;
        LONG was = InterlockedExchange(&g_unlocked, want);
        if (want) {
            if (!was) InterlockedExchange(&g_skipped, 0);
            snprintf(reply, cap, "FPS limit off (frame cap removed). !vsync on restores it.");
        } else {
            snprintf(reply, cap, "FPS limit on (default frame cap). %ld frame waits were skipped.", was ? (long)g_skipped : 0L);
        }
    } else if (!_stricmp(cmd, "crash")) {
        og_crash_write(NULL, "on demand (!crash)");
        snprintf(reply, cap, "Diagnostic report written: odinguard-crash\\%s", og_crash_last_file());
    }
}
