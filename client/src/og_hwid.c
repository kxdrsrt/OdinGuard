#include "og.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>

static void add(og_component *c, int *n, int max, const char *name, const char *salt, const void *data, size_t len)
{
    og_sha256_ctx s;
    uint8_t out[32];
    if (*n >= max || !len) return;
    og_sha256_init(&s);
    og_sha256_update(&s, salt, strlen(salt));
    og_sha256_update(&s, name, strlen(name));
    og_sha256_update(&s, data, len);
    og_sha256_final(&s, out);
    strncpy(c[*n].name, name, sizeof c[*n].name - 1);
    c[*n].name[sizeof c[*n].name - 1] = 0;
    og_hex(out, 32, c[*n].hex);
    (*n)++;
}

static int reg_str(HKEY root, const char *key, const char *val, char *out, DWORD cap)
{
    HKEY k;
    DWORD type, sz = cap - 1;
    int ok = 0;
    if (RegOpenKeyExA(root, key, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(k, val, NULL, &type, (BYTE *)out, &sz) == ERROR_SUCCESS && type == REG_SZ) { out[sz] = 0; ok = 1; }
    RegCloseKey(k);
    return ok;
}

/* Several independent sources, each hashed with a per-server salt. The server matches fuzzily (k of n), so one
   changed part (new disk, new Windows install) does not create a new identity. */
int og_hwid_collect(og_component *c, int max, const char *salt)
{
    int n = 0;
    char buf[256];
    DWORD serial = 0;
    int regs[4];

    if (reg_str(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid", buf, sizeof buf))
        add(c, &n, max, "machine_guid", salt, buf, strlen(buf));
    if (reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName", buf, sizeof buf))
        add(c, &n, max, "bios_product", salt, buf, strlen(buf));
    if (reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardProduct", buf, sizeof buf))
        add(c, &n, max, "board", salt, buf, strlen(buf));
    if (reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSReleaseDate", buf, sizeof buf))
        add(c, &n, max, "bios_date", salt, buf, strlen(buf));

    {
        char root[4] = "C:\\";
        char sysdir[MAX_PATH];
        if (GetSystemDirectoryA(sysdir, sizeof sysdir)) root[0] = sysdir[0];
        if (GetVolumeInformationA(root, NULL, 0, &serial, NULL, NULL, NULL, 0))
            add(c, &n, max, "volume_serial", salt, &serial, sizeof serial);
    }

    __cpuid(regs, 0);
    {
        char vendor[13];
        memcpy(vendor, &regs[1], 4); memcpy(vendor + 4, &regs[3], 4); memcpy(vendor + 8, &regs[2], 4); vendor[12] = 0;
        add(c, &n, max, "cpu_vendor", salt, vendor, 12);
    }
    {
        int r[12];
        char brand[49];
        __cpuid(r, 0x80000000);
        if ((unsigned)r[0] >= 0x80000004u) {
            __cpuid(r, 0x80000002); memcpy(brand, r, 16);
            __cpuid(r, 0x80000003); memcpy(brand + 16, r, 16);
            __cpuid(r, 0x80000004); memcpy(brand + 32, r, 16);
            brand[48] = 0;
            add(c, &n, max, "cpu_brand", salt, brand, strlen(brand));
        }
        __cpuid(r, 1);
        add(c, &n, max, "cpu_signature", salt, &r[0], 4);
    }
    {
        SYSTEM_INFO si;
        MEMORYSTATUSEX ms;
        GetNativeSystemInfo(&si);
        ms.dwLength = sizeof ms;
        GlobalMemoryStatusEx(&ms);
        {
            DWORD ram_gb = (DWORD)(ms.ullTotalPhys >> 30);
            add(c, &n, max, "cpu_cores", salt, &si.dwNumberOfProcessors, sizeof si.dwNumberOfProcessors);
            add(c, &n, max, "ram_gb", salt, &ram_gb, sizeof ram_gb);
        }
    }
    return n;
}

void og_hwid_unique(const og_component *c, int n, char hex[65])
{
    og_sha256_ctx s;
    uint8_t out[32];
    int i;
    /* only the stable, strongest components identify the machine */
    static const char *core[] = { "machine_guid", "volume_serial", "bios_product", "board", "cpu_brand" };
    og_sha256_init(&s);
    for (i = 0; i < n; i++) {
        size_t k;
        for (k = 0; k < sizeof core / sizeof *core; k++)
            if (!strcmp(c[i].name, core[k])) { og_sha256_update(&s, c[i].hex, 64); break; }
    }
    og_sha256_final(&s, out);
    og_hex(out, 32, hex);
}
