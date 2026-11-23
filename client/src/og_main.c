#include "og.h"
#include <stdio.h>
#include <string.h>

og_config og_cfg = { 0, 0, 1, "BurningRO", "2018-06-20", "6900,6121,5121,6901,6122,5122" };
char og_client_dir[MAX_PATH];
DWORD og_main_thread;

static volatile LONG g_started;

void og_config_load(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\odinguard.ini", dir);
    og_cfg.max_windows = GetPrivateProfileIntA("OdinGuard", "MaxWindows", og_cfg.max_windows, p);
    og_cfg.block_vm = GetPrivateProfileIntA("OdinGuard", "BlockVM", og_cfg.block_vm, p);
    og_cfg.log_only = GetPrivateProfileIntA("OdinGuard", "LogOnly", og_cfg.log_only, p);
    GetPrivateProfileStringA("OdinGuard", "Build", og_cfg.build, og_cfg.build, sizeof og_cfg.build, p);
    GetPrivateProfileStringA("OdinGuard", "Ports", og_cfg.ports, og_cfg.ports, sizeof og_cfg.ports, p);
}

static void fail(const char *msg)
{
    og_log("BLOCK: %s%s", msg, og_cfg.log_only ? " (log only)" : "");
    if (og_cfg.log_only) return;
    MessageBoxA(NULL, msg, "OdinGuard", MB_OK | MB_ICONSTOP | MB_TOPMOST);
    TerminateProcess(GetCurrentProcess(), 1);
}

OG_API int OdinGuard_Version(void) { return OG_ABI_VERSION; }

/* Human-readable and machine-parsable report: the same data the server hello will carry (O2). */
OG_API int OdinGuard_Report(char *out, size_t cap)
{
    char hex[65], why[64], uid[65];
    og_component comp[16];
    char *files = (char *)HeapAlloc(GetProcessHeap(), 0, 65536);
    char *mods = (char *)HeapAlloc(GetProcessHeap(), 0, 16384);
    size_t used = 0;
    int n, i, unk = 0;

    if (!files || !mods) return 0;
    used += snprintf(out + used, cap - used, "odinguard=%s abi=%d\n", OG_VERSION_STR, OG_ABI_VERSION);
    if (og_hash_module_code(GetModuleHandleA(NULL), hex))
        used += snprintf(out + used, cap - used, "exe_code=%s\n", hex);
    n = og_hwid_collect(comp, 16, og_cfg.salt);
    og_hwid_unique(comp, n, uid);
    used += snprintf(out + used, cap - used, "unique_id=%s\n", uid);
    for (i = 0; i < n; i++)
        used += snprintf(out + used, cap - used, "component.%s=%s\n", comp[i].name, comp[i].hex);
    used += snprintf(out + used, cap - used, "vm=%d %s\n", og_is_vm(why, sizeof why), why);
    {
        char detail[MAX_PATH + 64] = "";
        int bad = og_assets_check(detail, sizeof detail);
        used += snprintf(out + used, cap - used, "assets=%s %s\n", bad ? "violation" : "ok", detail);
    }
    og_scan_files(og_client_dir, files, 65536);
    used += snprintf(out + used, cap - used, "[files]\n%s", files);
    og_scan_modules(mods, 16384, &unk);
    used += snprintf(out + used, cap - used, "[modules unknown=%d]\n%s", unk, mods);
    HeapFree(GetProcessHeap(), 0, files);
    HeapFree(GetProcessHeap(), 0, mods);
    return (int)used;
}

OG_API void OdinGuard_CrashNow(void) { og_crash_write(NULL, "on demand (!crash)"); }

OG_API int OdinGuard_Start(void)
{
    char why[64];
    if (InterlockedExchange(&g_started, 1)) return 1;
    og_crash_install();
    if (og_cfg.block_vm && og_is_vm(why, sizeof why)) {
        og_log("vm detected: %s", why);
        fail("Running in a virtual machine is not allowed on this server.");
    }
    og_guard_start();
    og_log("started, client dir %s", og_client_dir);
    return 1;
}

static DWORD WINAPI start_thread(LPVOID p) { (void)p; OdinGuard_Start(); return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        char *s;
        DisableThreadLibraryCalls(inst);
        og_main_thread = GetCurrentThreadId();
        GetModuleFileNameA(NULL, og_client_dir, sizeof og_client_dir);
        s = strrchr(og_client_dir, '\\');
        if (s) *s = 0;
        og_config_load(og_client_dir);
        og_log_open(og_client_dir);
        InitializeCriticalSection(&og_policy_lock);
        /* hooks must be in place before the game opens its first connection */
        if (!og_net_init()) og_log("network layer failed to start");
        if (!og_cmds_init()) og_log("command hooks failed");
        /* everything else runs off the loader lock */
        CloseHandle(CreateThread(NULL, 0, start_thread, NULL, 0, NULL));
    }
    return TRUE;
}
