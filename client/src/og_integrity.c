#include "og.h"
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

/* Hash all executable, non-writable sections of a loaded module as they are in memory.
   Relocated 32-bit slots are zeroed in the hashed copy so the value does not depend on the load address. */
int og_hash_module_code(HMODULE m, char hex[65])
{
    const uint8_t *base = (const uint8_t *)m;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_SECTION_HEADER *sec;
    const IMAGE_DATA_DIRECTORY *rd;
    og_sha256_ctx c;
    uint8_t out[32];
    int i, any = 0;

    if (!m || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    og_sha256_init(&c);

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD ch = sec[i].Characteristics, va = sec[i].VirtualAddress, sz = sec[i].Misc.VirtualSize;
        uint8_t *tmp;
        MEMORY_BASIC_INFORMATION mbi;
        if (!(ch & IMAGE_SCN_MEM_EXECUTE) || (ch & IMAGE_SCN_MEM_WRITE) || !sz) continue;
        if (!VirtualQuery(base + va, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) continue;
        tmp = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, sz);
        if (!tmp) return 0;
        memcpy(tmp, base + va, sz);
        if (rd->Size) {
            const IMAGE_BASE_RELOCATION *br = (const IMAGE_BASE_RELOCATION *)(base + rd->VirtualAddress);
            const uint8_t *end = (const uint8_t *)br + rd->Size;
            while ((const uint8_t *)br < end && br->SizeOfBlock >= sizeof *br) {
                const WORD *e = (const WORD *)(br + 1);
                DWORD n = (br->SizeOfBlock - sizeof *br) / 2, k;
                for (k = 0; k < n; k++) {
                    DWORD off = br->VirtualAddress + (e[k] & 0xFFF);
                    if ((e[k] >> 12) == IMAGE_REL_BASED_HIGHLOW && off >= va && off + 4 <= va + sz)
                        memset(tmp + (off - va), 0, 4);
                }
                br = (const IMAGE_BASE_RELOCATION *)((const uint8_t *)br + br->SizeOfBlock);
            }
        }
        og_sha256_update(&c, tmp, sz);
        HeapFree(GetProcessHeap(), 0, tmp);
        any = 1;
    }
    if (!any) return 0;
    og_sha256_final(&c, out);
    og_hex(out, 32, hex);
    return 1;
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
