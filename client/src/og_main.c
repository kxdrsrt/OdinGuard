/* Entry point, profile values and the exported ABI. Every protection switch comes from the server policy; the
 * ini next to the exe may only change cosmetic and diagnostic values. */
#include "og.h"
#include "../../third_party/monocypher.h"
#include <stdio.h>
#include <string.h>

og_config og_cfg = { 0, 0, OG_PROFILE_BUILD, OG_PROFILE_PORTS };
char og_client_dir[MAX_PATH];
DWORD og_main_thread;

static volatile LONG g_started;

static const uint8_t g_mask[32] = OG_PROFILE_MASK;
static const uint8_t g_hwid_salt[OG_PROFILE_HWID_SALT_LEN] = OG_PROFILE_HWID_SALT;
static const uint8_t g_proto_salt[OG_SALT_LEN] = OG_PROFILE_PROTO_SALT;
static const uint8_t g_build_secret[32] = OG_PROFILE_BUILD_SECRET;

size_t og_profile_hwid_salt(char *out, size_t cap)
{
    size_t i, n = OG_PROFILE_HWID_SALT_LEN < cap - 1 ? OG_PROFILE_HWID_SALT_LEN : cap - 1;
    for (i = 0; i < n; i++) out[i] = (char)(g_hwid_salt[i] ^ g_mask[i & 31]);
    out[n] = 0;
    return n;
}

void og_profile_proto_salt(uint8_t out[OG_SALT_LEN])
{
    int i;
    for (i = 0; i < OG_SALT_LEN; i++) out[i] = g_proto_salt[i] ^ g_mask[i];
}

void og_profile_build_secret(uint8_t out[32])
{
    int i;
    for (i = 0; i < 32; i++) out[i] = g_build_secret[i] ^ g_mask[i];
}

void og_profile_tag(uint8_t out[OG_TAG_LEN])
{
    static const char label[] = "OdinGuard/tag";
    uint8_t salt[OG_SALT_LEN];
    crypto_blake2b_ctx c;
    og_profile_proto_salt(salt);
    crypto_blake2b_init(&c, OG_TAG_LEN);
    crypto_blake2b_update(&c, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&c, salt, OG_SALT_LEN);
    crypto_blake2b_final(&c, out);
    crypto_wipe(salt, sizeof salt);
}

void og_config_load(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\odinguard.ini", dir);
    og_cfg.max_windows = GetPrivateProfileIntA("OdinGuard", "MaxWindows", og_cfg.max_windows, p);
    og_cfg.block_vm = GetPrivateProfileIntA("OdinGuard", "BlockVM", og_cfg.block_vm, p);
}

OG_API int OdinGuard_Version(void) { return OG_ABI_VERSION; }

#if OG_MOD_INTEGRITY
/* Human-readable report for the operator's self test (selftest.exe). */
OG_API int OdinGuard_Report(char *out, size_t cap)
{
    char hex[65], why[64];
    uint8_t h[32];
    char *files = (char *)HeapAlloc(GetProcessHeap(), 0, 65536);
    char *mods = (char *)HeapAlloc(GetProcessHeap(), 0, 16384);
    size_t used = 0;
    int unk = 0;

    if (!files || !mods) return 0;
    used += snprintf(out + used, cap - used, "odinguard=%s abi=%d profile=%s\n", OG_VERSION_STR, OG_ABI_VERSION, OG_PROFILE_ID);
    if (og_hash_module_code(GetModuleHandleA(NULL), OG_CANON_CODE, h)) { og_hex(h, 32, hex); used += snprintf(out + used, cap - used, "exe_code=%s\n", hex); }
    if (og_hash_module_code(og_instance, OG_CANON_CODE | OG_CANON_RDATA, h)) { og_hex(h, 32, hex); used += snprintf(out + used, cap - used, "guard_code=%s\n", hex); }
#if OG_MOD_HWID
    {
        og_component comp[OG_MAX_COMPONENTS];
        char salt[64], uid[65];
        int n, i;
        og_profile_hwid_salt(salt, sizeof salt);
        n = og_hwid_collect(comp, OG_MAX_COMPONENTS, salt);
        og_hwid_unique(comp, n, uid);
        used += snprintf(out + used, cap - used, "unique_id=%s\n", uid);
        for (i = 0; i < n; i++) used += snprintf(out + used, cap - used, "component.%s=%s\n", comp[i].name, comp[i].hex);
    }
#endif
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
#endif

#if OG_MOD_INTEGRITY
OG_API int OdinGuard_CanonDump(const char *path) { return og_canon_dump(og_instance, OG_CANON_CODE | OG_CANON_RDATA, path); }
#endif

#if OG_MOD_CRASH
OG_API void OdinGuard_CrashNow(void) { og_crash_write(NULL, "on demand (!crash)"); }
#endif

OG_API int OdinGuard_Start(void)
{
    char why[64];
    if (InterlockedExchange(&g_started, 1)) return 1;
    og_crash_install();
    if (og_cfg.block_vm && og_is_vm(why, sizeof why)) {
        og_log("vm detected: %s", why);
        og_message("Running in a virtual machine is not allowed on this server.", MB_OK | MB_ICONSTOP);
        TerminateProcess(GetCurrentProcess(), 1);
    }
    og_guard_start();
    og_log("started, profile %s, client dir %s", OG_PROFILE_ID, og_client_dir);
    return 1;
}

static DWORD WINAPI start_thread(LPVOID p) { (void)p; og_title_start(); OdinGuard_Start(); return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        char *s, ini[MAX_PATH];
        DisableThreadLibraryCalls(inst);
        og_instance = inst;
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
        if (!og_text_init()) og_log("text hook failed");
        og_ui_init();
        og_lgp_init();
#ifdef OG_TEST_CURSOR
        { void og_test_cursor_init(void); og_test_cursor_init(); }
#endif
#if OG_MOD_SPLASH
        snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
        og_splash_show(inst, (DWORD)GetPrivateProfileIntA("OdinGuard", "SplashMs", OG_PROFILE_SPLASH_MS, ini));
#else
        (void)ini;
#endif
        /* everything else runs off the loader lock */
        CloseHandle(CreateThread(NULL, 0, start_thread, NULL, 0, NULL));
    }
    return TRUE;
}
