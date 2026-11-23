#include "og.h"
#include <stdio.h>
#include <string.h>

og_config og_cfg = { 0, 0, 1, "odinguard-default-salt" };
char og_client_dir[MAX_PATH];

static volatile LONG g_started;

void og_config_load(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\odinguard.ini", dir);
    og_cfg.max_windows = GetPrivateProfileIntA("OdinGuard", "MaxWindows", og_cfg.max_windows, p);
    og_cfg.block_vm = GetPrivateProfileIntA("OdinGuard", "BlockVM", og_cfg.block_vm, p);
    og_cfg.log_only = GetPrivateProfileIntA("OdinGuard", "LogOnly", og_cfg.log_only, p);
    GetPrivateProfileStringA("OdinGuard", "Salt", og_cfg.salt, og_cfg.salt, sizeof og_cfg.salt, p);
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
    if (!og_window_slot_acquire(og_cfg.max_windows))
        fail("Maximum number of game windows on this computer reached.");
    if (og_cfg.block_vm && og_is_vm(why, sizeof why)) {
        og_log("vm detected: %s", why);
        fail("Running in a virtual machine is not allowed on this server.");
    }
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
        GetModuleFileNameA(NULL, og_client_dir, sizeof og_client_dir);
        s = strrchr(og_client_dir, '\\');
        if (s) *s = 0;
        og_config_load(og_client_dir);
        og_log_open(og_client_dir);
        /* heavy work is done off the loader lock */
        CloseHandle(CreateThread(NULL, 0, start_thread, NULL, 0, NULL));
    }
    return TRUE;
}
