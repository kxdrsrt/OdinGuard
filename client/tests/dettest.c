/* Detection test for the cheat-signatures module (og_detect.c linked directly, no network).
 *
 *   dettest <odinguard.sigpack> positive <empty work folder>
 *       For the first signature of every client-checkable kind in the package a harmless stub is created
 *       (renamed copy of ping.exe / version.dll, own window class or title, empty file) and must be reported.
 *       File, GRF and allowlist matching use synthetic hashes of fixture files. No cheat software is involved.
 *   dettest <odinguard.sigpack> clean <client folder>
 *       Every 32-bit DLL of the client folder is mapped (without running its code) and the folder is scanned
 *       like the guard does; any signature hit is a false alarm.
 *
 * Prints PASS/FAIL lines; the exit code is the number of failures. */
#include "og.h"
#include <stdio.h>
#include <string.h>

char og_client_dir[MAX_PATH];

#define MAX_HITS 256
static struct { uint16_t id; uint8_t kind; char text[260]; } g_hits[MAX_HITS];
static int g_nhits, g_fail;

void og_net_broadcast_control(const uint8_t *m, size_t n)
{
    if (n < 4 || m[0] != OG_C_DETECT || g_nhits >= MAX_HITS) return;
    g_hits[g_nhits].id = (uint16_t)(m[1] | (m[2] << 8));
    g_hits[g_nhits].kind = m[3];
    snprintf(g_hits[g_nhits].text, sizeof g_hits[0].text, "%.*s", (int)(n - 4), (const char *)m + 4);
    g_nhits++;
}

static void result(int ok, const char *what, const char *detail)
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail ? ": " : "", detail ? detail : "");
    if (!ok) g_fail++;
}

static int hit(uint16_t id, uint8_t kind)
{
    int i;
    for (i = 0; i < g_nhits; i++) if (g_hits[i].id == id && g_hits[i].kind == kind) return 1;
    return 0;
}

/* signature package (OGSP v1): entries are taken as they are; the server checks the signature */
static int load_pack(const char *path)
{
    static uint8_t buf[1 << 20];
    FILE *f = fopen(path, "rb");
    size_t len, off = 15;
    unsigned n, i;
    if (!f) return 0;
    len = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (len < 15 + 64 || memcmp(buf, "OGSP", 4) || buf[4] != 1) return 0;
    n = buf[13] | (buf[14] << 8);
    og_policy.nsig = 0;
    for (i = 0; i < n && off + 6 <= len - 64; i++) {
        uint16_t id = (uint16_t)(buf[off] | (buf[off + 1] << 8));
        uint8_t kind = buf[off + 2], nlen = buf[off + 5], plen;
        off += 6 + nlen;
        plen = buf[off++];
        if (off + plen > len - 64) return 0;
        if (og_policy.nsig < OG_MAX_SIGS) {
            og_signature *s = &og_policy.sig[og_policy.nsig++];
            s->id = id;
            s->kind = kind;
            memcpy(s->pattern, buf + off, plen);
            s->pattern[plen] = 0;
        }
        off += plen;
    }
    return og_policy.nsig;
}

static og_signature *first_of(uint8_t kind)
{
    int i;
    for (i = 0; i < og_policy.nsig; i++) if (og_policy.sig[i].kind == kind) return &og_policy.sig[i];
    return NULL;
}

static void add_sig(uint16_t id, uint8_t kind, const char *pattern)
{
    og_signature *s = &og_policy.sig[og_policy.nsig++];
    s->id = id;
    s->kind = kind;
    snprintf(s->pattern, sizeof s->pattern, "%s", pattern);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static void scan(void)
{
    g_nhits = 0;
    og_detect_reset();
    og_detect_scan();
}

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

static HWND stub_window(const char *cls, const char *title)
{
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    return CreateWindowA(cls, title, WS_POPUP | WS_VISIBLE, -2000, -2000, 10, 10, NULL, NULL, wc.hInstance, NULL);
}

static int positive(const char *dir)
{
    char sys[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], hex[65], ini[MAX_PATH];
    og_signature *s;
    PROCESS_INFORMATION pi = { 0 };
    HWND w1 = NULL, w2 = NULL;
    HMODULE mod = NULL;
    HANDLE mtx = NULL;
    uint16_t pid_sig = 0, mod_sig = 0, cls_sig = 0, title_sig = 0, file_sig = 0;

    snprintf(og_client_dir, sizeof og_client_dir, "%s", dir);
    GetSystemWow64DirectoryA(sys, sizeof sys);
    if (!sys[0]) GetSystemDirectoryA(sys, sizeof sys);

    if ((s = first_of(OG_KIND_PROCESS))) {
        STARTUPINFOA si;
        char cmd[MAX_PATH * 2];
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        pid_sig = s->id;
        snprintf(src, sizeof src, "%s\\PING.EXE", sys);
        snprintf(dst, sizeof dst, "%s\\%s%s", dir, s->pattern, og_contains_i(s->pattern, ".exe") ? "" : ".exe");
        CopyFileA(src, dst, FALSE);
        snprintf(cmd, sizeof cmd, "\"%s\" -n 30 127.0.0.1", dst);
        if (!CreateProcessA(dst, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) pid_sig = 0;
    }
    if ((s = first_of(OG_KIND_MODULE))) {
        mod_sig = s->id;
        snprintf(src, sizeof src, "%s\\version.dll", sys);
        snprintf(dst, sizeof dst, "%s\\%s", dir, s->pattern);
        CopyFileA(src, dst, FALSE);
        if (!(mod = LoadLibraryExA(dst, NULL, DONT_RESOLVE_DLL_REFERENCES))) mod_sig = 0;
    }
    if ((s = first_of(OG_KIND_WINDOW_CLASS))) { cls_sig = s->id; w1 = stub_window(s->pattern, "stub"); }
    if ((s = first_of(OG_KIND_WINDOW))) {
        char title[300];
        title_sig = s->id;
        snprintf(title, sizeof title, "stub%s1", s->pattern);
        w2 = stub_window("OgDetTestTitle", title);
    }
    if ((s = first_of(OG_KIND_FILE_NAME))) {
        file_sig = s->id;
        snprintf(dst, sizeof dst, "%s\\%s", dir, s->pattern);
        write_file(dst, "stub");
    }
    /* synthetic hash signatures over fixture files */
    snprintf(dst, sizeof dst, "%s\\fixture.bin", dir);
    write_file(dst, "OdinGuard detection fixture");
    og_sha256_file(dst, hex);
    add_sig(65001, OG_KIND_FILE_SHA256, hex);
    snprintf(dst, sizeof dst, "%s\\fixture.grf", dir);
    write_file(dst, "Master of Magic fixture grf");
    og_sha256_file(dst, hex);
    add_sig(65002, OG_KIND_GRF_SHA256, hex);
    snprintf(ini, sizeof ini, "%s\\DATA.INI", dir);
    write_file(ini, "[Data]\r\n0=fixture.grf\r\n1=data.grf\r\n");

    /* kernel object, registry key and driver: synthetic signatures over harmless objects */
    {
        static char drv[256];
        SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
        HKEY k;
        mtx = CreateMutexA(NULL, FALSE, "OdinGuardDetTestMutex");
        add_sig(65005, OG_KIND_MUTEX, "OdinGuardDetTestMutex");
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\OdinGuardDetTest", 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) RegCloseKey(k);
        add_sig(65006, OG_KIND_REGISTRY, "HKCU\\Software\\OdinGuardDetTest");
        add_sig(65008, OG_KIND_REGISTRY, "HKCU\\Software\\OdinGuardDetTestMissing");
        if (scm) {
            DWORD need = 0, count = 0, resume = 0;
            ENUM_SERVICE_STATUSA *buf;
            EnumServicesStatusA(scm, SERVICE_DRIVER, SERVICE_ACTIVE, NULL, 0, &need, &count, &resume);
            if (need && (buf = (ENUM_SERVICE_STATUSA *)HeapAlloc(GetProcessHeap(), 0, need))) {
                resume = 0;
                if (EnumServicesStatusA(scm, SERVICE_DRIVER, SERVICE_ACTIVE, buf, need, &need, &count, &resume) && count)
                    snprintf(drv, sizeof drv, "%s", buf[0].lpServiceName);
                HeapFree(GetProcessHeap(), 0, buf);
            }
            CloseServiceHandle(scm);
        }
        if (drv[0]) add_sig(65007, OG_KIND_DRIVER, drv);
        printf("  driver used for the driver test: %s\n", drv[0] ? drv : "(none)");
    }

    scan();
    {
        int i;
        for (i = 0; i < g_nhits; i++) printf("  hit kind %u id %u: %s\n", g_hits[i].kind, g_hits[i].id, g_hits[i].text);
    }
    if (pid_sig) result(hit(pid_sig, OG_KIND_PROCESS), "process name signature", NULL);
    if (mod_sig) result(hit(mod_sig, OG_KIND_MODULE), "loaded module signature", NULL);
    if (cls_sig) result(hit(cls_sig, OG_KIND_WINDOW_CLASS), "window class signature", NULL);
    if (title_sig) result(hit(title_sig, OG_KIND_WINDOW), "window title signature", NULL);
    if (file_sig) result(hit(file_sig, OG_KIND_FILE_NAME), "file name signature", NULL);
    result(hit(65001, OG_KIND_FILE_SHA256), "file hash signature", NULL);
    result(hit(65002, OG_KIND_GRF_SHA256), "GRF hash signature (DATA.INI)", NULL);
    result(hit(65005, OG_KIND_MUTEX), "named mutex signature", NULL);
    result(hit(65006, OG_KIND_REGISTRY), "registry key signature", NULL);
    result(!hit(65008, OG_KIND_REGISTRY), "missing registry key is not reported", NULL);
    result(hit(65007, OG_KIND_DRIVER), "driver / service signature", NULL);
    result(!pid_sig + !mod_sig + !cls_sig + !title_sig + !file_sig == 0, "package has a signature of every stub kind", NULL);

    /* every hit is reported once per connection */
    g_nhits = 0;
    og_detect_scan();
    {
        int i, again = 0;
        for (i = 0; i < g_nhits; i++) if (g_hits[i].id) again++;
        result(again == 0, "hits are not repeated within a connection", NULL);
    }

    /* allowlist: the same files with allowlisted hashes are not reported */
    if (file_sig) {
        snprintf(dst, sizeof dst, "%s\\%s", dir, first_of(OG_KIND_FILE_NAME)->pattern);
        og_sha256_file(dst, hex);
        add_sig(65003, OG_KIND_ALLOW_SHA256, hex);
    }
    snprintf(dst, sizeof dst, "%s\\fixture.bin", dir);
    og_sha256_file(dst, hex);
    add_sig(65004, OG_KIND_ALLOW_SHA256, hex);
    scan();
    result(!hit(65001, OG_KIND_FILE_SHA256), "allowlisted file hash is not reported", NULL);
    if (file_sig) result(!hit(file_sig, OG_KIND_FILE_NAME), "allowlisted file with a signature name is not reported", NULL);

    if (pi.hProcess) { TerminateProcess(pi.hProcess, 0); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    if (mod) FreeLibrary(mod);
    if (w1) DestroyWindow(w1);
    if (w2) DestroyWindow(w2);
    if (mtx) CloseHandle(mtx);
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\OdinGuardDetTest");
    return g_fail;
}

static int clean(const char *dir)
{
    char mask[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int i, mapped = 0, sig_hits = 0;
    snprintf(og_client_dir, sizeof og_client_dir, "%s", dir);
    snprintf(mask, sizeof mask, "%s\\*.dll", dir);
    if ((h = FindFirstFileA(mask, &fd)) != INVALID_HANDLE_VALUE) {
        do {
            snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
            if (LoadLibraryExA(full, NULL, DONT_RESOLVE_DLL_REFERENCES)) { printf("  mapped %s\n", fd.cFileName); mapped++; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    scan();
    for (i = 0; i < g_nhits; i++) {
        printf("  %s kind %u id %u: %s\n", g_hits[i].id ? "HIT" : "inject-report", g_hits[i].kind, g_hits[i].id, g_hits[i].text);
        if (g_hits[i].id) sig_hits++;
    }
    result(mapped > 0, "client DLLs mapped", NULL);
    result(sig_hits == 0, "legitimate client folder: no signature hit", NULL);
    return g_fail;
}

int main(int argc, char **argv)
{
    InitializeCriticalSection(&og_policy_lock);
    if (argc < 4 || !load_pack(argv[1])) {
        fprintf(stderr, "usage: dettest <odinguard.sigpack> positive|clean <folder>\n");
        return 100;
    }
    printf("%d entries loaded\n", og_policy.nsig);
    if (!strcmp(argv[2], "positive")) return positive(argv[3]);
    if (!strcmp(argv[2], "clean")) return clean(argv[3]);
    return 100;
}
