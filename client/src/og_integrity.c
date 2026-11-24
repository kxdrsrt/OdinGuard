#include "og.h"
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include <intrin.h>
#include "../../third_party/monocypher.h"

/* Canonical image of a loaded module: the sections og_pe_section_included() selects, in section order, each as
   large as in memory, with every HIGHLOW relocation slot zeroed. Identical to og_pe_canonical() on the file, so
   the server can verify code range challenges against the release file (guard) or a reference dump (exe). */
static uint8_t *canon(HMODULE m, int which, size_t *size)
{
    const uint8_t *base = (const uint8_t *)m;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_SECTION_HEADER *sec;
    const IMAGE_DATA_DIRECTORY *rd, *iat;
    size_t total = 0, pos = 0;
    uint8_t *img;
    int i;

    *size = 0;
    if (!m || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    sec = IMAGE_FIRST_SECTION(nt);
    rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    iat = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD sz = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (og_pe_section_included(sec[i].Characteristics, which)) total += sz;
    }
    if (!total || total > 256u * 1024 * 1024) return NULL;
    img = (uint8_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!img) return NULL;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD va = sec[i].VirtualAddress, sz = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        DWORD done = 0;
        if (!og_pe_section_included(sec[i].Characteristics, which)) continue;
        while (done < sz) {
            MEMORY_BASIC_INFORMATION mbi;
            DWORD chunk;
            if (!VirtualQuery(base + va + done, &mbi, sizeof mbi)) break;
            chunk = (DWORD)((const uint8_t *)mbi.BaseAddress + mbi.RegionSize - (base + va + done));
            if (chunk > sz - done) chunk = sz - done;
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                memcpy(img + pos + done, base + va + done, chunk);
            done += chunk;
        }
        /* the import address table is written by the loader even when its section is read-only */
        if (iat->Size && iat->VirtualAddress < va + sz && iat->VirtualAddress + iat->Size > va) {
            DWORD a = iat->VirtualAddress > va ? iat->VirtualAddress : va;
            DWORD b = iat->VirtualAddress + iat->Size < va + sz ? iat->VirtualAddress + iat->Size : va + sz;
            memset(img + pos + (a - va), 0, b - a);
        }
        if (rd->Size) {
            const IMAGE_BASE_RELOCATION *br = (const IMAGE_BASE_RELOCATION *)(base + rd->VirtualAddress);
            const uint8_t *end = (const uint8_t *)br + rd->Size;
            while ((const uint8_t *)br < end && br->SizeOfBlock >= sizeof *br) {
                const WORD *e = (const WORD *)(br + 1);
                DWORD n = (br->SizeOfBlock - sizeof *br) / 2, k;
                for (k = 0; k < n; k++) {
                    DWORD off = br->VirtualAddress + (e[k] & 0xFFF);
                    if ((e[k] >> 12) == IMAGE_REL_BASED_HIGHLOW && off >= va && off + 4 <= va + sz)
                        memset(img + pos + (off - va), 0, 4);
                }
                br = (const IMAGE_BASE_RELOCATION *)((const uint8_t *)br + br->SizeOfBlock);
            }
        }
        pos += sz;
    }
    *size = total;
    return img;
}

/* diagnostics (selftest canon): writes the canonical image, so an operator can compare it with og_pe_canonical()
   of the release file when a self-integrity report looks wrong */
int og_canon_dump(HMODULE m, int which, const char *path)
{
    size_t n;
    uint8_t *img = canon(m, which, &n);
    FILE *f;
    if (!img) return 0;
    f = fopen(path, "wb");
    if (f) { fwrite(img, 1, n, f); fclose(f); }
    HeapFree(GetProcessHeap(), 0, img);
    return f != NULL;
}

int og_hash_module_code(HMODULE m, int which, uint8_t out[32])
{
    size_t n;
    uint8_t *img = canon(m, which, &n);
    memset(out, 0, 32);
    if (!img) return 0;
    crypto_blake2b(out, 32, img, n);
    HeapFree(GetProcessHeap(), 0, img);
    return 1;
}

int og_module_range(HMODULE m, int which, uint32_t off, uint32_t len, const uint8_t nonce[16], uint8_t out[32])
{
    size_t n;
    uint8_t *img = canon(m, which, &n);
    crypto_blake2b_ctx c;
    if (!img) return OG_CH_ST_MISSING;
    if ((size_t)off > n || len > n - off || len == 0) { HeapFree(GetProcessHeap(), 0, img); return OG_CH_ST_RANGE; }
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, nonce, 16);
    crypto_blake2b_update(&c, img + off, len);
    crypto_blake2b_final(&c, out);
    HeapFree(GetProcessHeap(), 0, img);
    return OG_CH_ST_OK;
}

/* ---- anti-debug and API hook checks (hurdles; reported to the server, which decides) ---- */

static int hw_breakpoints(void)
{
    CONTEXT ctx;
    HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, og_main_thread);
    int hit = 0;
    if (!t) return 0;
    ZeroMemory(&ctx, sizeof ctx);
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &ctx)) hit = (ctx.Dr0 | ctx.Dr1 | ctx.Dr2 | ctx.Dr3) != 0 && (ctx.Dr7 & 0xFF) != 0;
    CloseHandle(t);
    return hit;
}

/* where does a patched function jump to? 0 = not patched */
static const uint8_t *jump_target(const uint8_t *p)
{
    if (p[0] == 0xE9) return p + 5 + *(const int32_t *)(p + 1);
    if (p[0] == 0xFF && p[1] == 0x25) return **(const uint8_t *const *const *)(p + 2);
    if (p[0] == 0x68 && p[5] == 0xC3) return *(const uint8_t *const *)(p + 1);
    return NULL;
}

static int api_hooked(char *what, size_t cap)
{
    static const struct { const char *mod, *fn; } api[] = {
        { "kernel32.dll", "GetTickCount" }, { "kernel32.dll", "GetTickCount64" }, { "kernel32.dll", "QueryPerformanceCounter" },
        { "kernelbase.dll", "GetTickCount" }, { "kernelbase.dll", "QueryPerformanceCounter" }, { "ntdll.dll", "NtQueryPerformanceCounter" },
        { "ntdll.dll", "RtlQueryPerformanceCounter" }, { "winmm.dll", "timeGetTime" }, { "user32.dll", "GetAsyncKeyState" },
    };
    char win[MAX_PATH], path[MAX_PATH];
    size_t i;
    GetWindowsDirectoryA(win, sizeof win);
    for (i = 0; i < sizeof api / sizeof *api; i++) {
        HMODULE mod = GetModuleHandleA(api[i].mod), owner = NULL;
        const uint8_t *f, *dst;
        MEMORY_BASIC_INFORMATION mbi;
        if (!mod || !(f = (const uint8_t *)GetProcAddress(mod, api[i].fn))) continue;
        if (!VirtualQuery(f, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) continue;
        if (!(dst = jump_target(f))) continue;
        if (!VirtualQuery(dst, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) continue;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)dst, &owner)) {
            snprintf(what, cap, "%s!%s -> unmapped code", api[i].mod, api[i].fn);
            return 1;
        }
        if (owner == og_instance) continue;
        GetModuleFileNameA(owner, path, sizeof path);
        if (_strnicmp(path, win, strlen(win)) == 0 || og_path_tolerated(path) || og_file_allowed(path)) continue;
        snprintf(what, cap, "%s!%s -> %s", api[i].mod, api[i].fn, path);
        return 1;
    }
    return 0;
}

char og_integrity_detail[MAX_PATH + 64];

uint8_t og_integrity_flags(void)
{
    uint8_t f = 0;
    BOOL remote = FALSE;
    const uint8_t *peb = (const uint8_t *)__readfsdword(0x30);
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    if (IsDebuggerPresent() || remote || (peb && (*(const DWORD *)(peb + 0x68) & 0x70))) f |= OG_ALIVE_DEBUGGER;
    if (hw_breakpoints()) f |= OG_ALIVE_HW_BREAKPOINT;
    if (api_hooked(og_integrity_detail, sizeof og_integrity_detail)) f |= OG_ALIVE_API_HOOKED;
    return f;
}

int og_scan_files(const char *dir, char *out, size_t cap)
{
    static const char *pat[] = { "\\*.exe", "\\*.dll" };
    size_t used = 0;
    int p, count = 0;
    for (p = 0; p < 2; p++) {
        char mask[MAX_PATH], full[MAX_PATH], hex[65];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(mask, sizeof mask, "%s%s", dir, pat[p]);
        h = FindFirstFileA(mask, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
            if (!og_sha256_file(full, hex)) continue;
            used += snprintf(out + used, cap - used, "%s=%s\n", fd.cFileName, hex);
            count++;
            if (used + 128 >= cap) break;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return count;
}

static int starts_with_i(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    return n && _strnicmp(s, prefix, n) == 0;
}

/* Lists every loaded module; "unknown" counts modules that are neither in the client directory nor in the Windows directory. */
int og_scan_modules(char *out, size_t cap, int *unknown)
{
    char win[MAX_PATH];
    MODULEENTRY32 me;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    size_t used = 0;
    int count = 0;
    if (unknown) *unknown = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    GetWindowsDirectoryA(win, sizeof win);
    me.dwSize = sizeof me;
    if (Module32First(snap, &me)) do {
        int known = starts_with_i(me.szExePath, og_client_dir) || starts_with_i(me.szExePath, win);
        if (!known && unknown) (*unknown)++;
        if (used + MAX_PATH + 8 < cap)
            used += snprintf(out + used, cap - used, "%s%s\n", known ? "" : "? ", me.szExePath);
        count++;
    } while (Module32Next(snap, &me));
    CloseHandle(snap);
    return count;
}
