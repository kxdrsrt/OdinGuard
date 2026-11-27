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

/* !dark [on|off]: the competitive pack (odin-dark.grf) is a normal GRF in DATA.INI right behind odin.grf. The game reads
   DATA.INI once at start, so the change needs a restart; nothing is loaded or unloaded here. */
static void dark_command(const char *arg, char *reply, size_t cap)
{
    char ini[MAX_PATH], pack[MAX_PATH], list[48][MAX_PATH], key[8];
    int n = 0, i, has = -1, old_n;
    DWORD attr;
    snprintf(ini, sizeof ini, "%s\\DATA.INI", og_client_dir);
    snprintf(pack, sizeof pack, "%s\\odin-dark.grf", og_client_dir);
    for (i = 0; i < 48; i++) {
        snprintf(key, sizeof key, "%d", i);
        GetPrivateProfileStringA("Data", key, "", list[n], MAX_PATH, ini);
        if (list[n][0]) { if (!_stricmp(list[n], "odin-dark.grf")) has = n; n++; }
    }
    old_n = n;
    if (!arg[0]) {
        og_trf(reply, cap, OG_T_DARK_STATUS, og_tr(has >= 0 ? OG_T_ON : OG_T_OFF), NULL);
        return;
    }
    if (!_stricmp(arg, "on")) {
        int at = 0;
        attr = GetFileAttributesA(pack);
        if (attr == INVALID_FILE_ATTRIBUTES) { og_trf(reply, cap, OG_T_DARK_MISSING, NULL, NULL); return; }
        if (has >= 0) { og_trf(reply, cap, OG_T_DARK_ALREADY_ON, NULL, NULL); return; }
        for (i = 0; i < n; i++) if (!_stricmp(list[i], "odin.grf")) at = i + 1;
        for (i = n; i > at; i--) snprintf(list[i], MAX_PATH, "%s", list[i - 1]);
        snprintf(list[at], MAX_PATH, "odin-dark.grf");
        n++;
    } else if (!_stricmp(arg, "off")) {
        if (has < 0) { og_trf(reply, cap, OG_T_DARK_ALREADY_OFF, NULL, NULL); return; }
        for (i = has; i < n - 1; i++) snprintf(list[i], MAX_PATH, "%s", list[i + 1]);
        n--;
    } else {
        og_trf(reply, cap, OG_T_DARK_USAGE, NULL, NULL);
        return;
    }
    for (i = 0; i < n; i++) { snprintf(key, sizeof key, "%d", i); WritePrivateProfileStringA("Data", key, list[i], ini); }
    for (i = n; i < old_n; i++) { snprintf(key, sizeof key, "%d", i); WritePrivateProfileStringA("Data", key, NULL, ini); }
    og_trf(reply, cap, OG_T_DARK_CHANGED, og_tr(n > old_n ? OG_T_ON : OG_T_OFF), NULL);
}

void og_cmd_handle(const char *cmd, char *reply, size_t cap)
{
    if (!_strnicmp(cmd, "dark", 4) && (cmd[4] == 0 || cmd[4] == ' ')) {
        dark_command(cmd[4] ? cmd + 5 : cmd + 4, reply, cap);
        return;
    }
    if (!_strnicmp(cmd, "lang", 4) && (cmd[4] == 0 || cmd[4] == ' ')) {
        og_lang_command(cmd + 4, reply, cap);
        return;
    }
    if (!_strnicmp(cmd, "vsync", 5)) {
        /* "vsync" toggles, "vsync off" removes the frame cap, "vsync on" restores it */
        LONG want = !_stricmp(cmd, "vsync off") ? 1 : !_stricmp(cmd, "vsync on") ? 0 : !g_unlocked;
        LONG was = InterlockedExchange(&g_unlocked, want);
        if (want) {
            if (!was) InterlockedExchange(&g_skipped, 0);
            og_trf(reply, cap, OG_T_VSYNC_OFF, NULL, NULL);
        } else {
            char n[24];
            snprintf(n, sizeof n, "%ld", was ? (long)g_skipped : 0L);
            og_trf(reply, cap, OG_T_VSYNC_ON, n, NULL);
        }
    } else if (!_stricmp(cmd, "crash")) {
        og_crash_write(NULL, "on demand (!crash)");
        og_trf(reply, cap, OG_T_CRASH_WRITTEN, og_crash_last_file(), NULL);
    }
}
