#include "og.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <winioctl.h>

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

    {
        /* serial number of the first physical disk (no admin rights needed for the descriptor) */
        HANDLE d = CreateFileA("\\\\.\\PhysicalDrive0", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (d != INVALID_HANDLE_VALUE) {
            STORAGE_PROPERTY_QUERY q;
            uint8_t out[1024];
            DWORD got = 0;
            memset(&q, 0, sizeof q);
            q.PropertyId = StorageDeviceProperty;
            q.QueryType = PropertyStandardQuery;
            if (DeviceIoControl(d, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, out, sizeof out, &got, NULL) && got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
                const STORAGE_DEVICE_DESCRIPTOR *sd = (const STORAGE_DEVICE_DESCRIPTOR *)out;
                if (sd->SerialNumberOffset && sd->SerialNumberOffset < got) {
                    const char *sn = (const char *)out + sd->SerialNumberOffset;
                    size_t len = strnlen(sn, got - sd->SerialNumberOffset);
                    while (len && sn[len - 1] == ' ') len--;
                    while (len && *sn == ' ') { sn++; len--; }
                    if (len) add(c, &n, max, "disk_serial", salt, sn, len);
                }
            }
            CloseHandle(d);
        }
    }
    {
        /* MAC of the first physical Ethernet or Wi-Fi adapter */
        ULONG size = 0;
        if (GetAdaptersInfo(NULL, &size) == ERROR_BUFFER_OVERFLOW) {
            IP_ADAPTER_INFO *ai = (IP_ADAPTER_INFO *)HeapAlloc(GetProcessHeap(), 0, size), *a;
            if (ai && GetAdaptersInfo(ai, &size) == NO_ERROR) {
                for (a = ai; a; a = a->Next) {
                    if ((a->Type == MIB_IF_TYPE_ETHERNET || a->Type == IF_TYPE_IEEE80211) && a->AddressLength == 6 &&
                        !strstr(a->Description, "Virtual") && !strstr(a->Description, "VPN") && !strstr(a->Description, "Loopback")) {
                        add(c, &n, max, "mac", salt, a->Address, 6);
                        break;
                    }
                }
            }
            if (ai) HeapFree(GetProcessHeap(), 0, ai);
        }
    }
    {
        /* SMBIOS system UUID (structure type 1) */
        UINT size = GetSystemFirmwareTable(0x52534D42u, 0, NULL, 0);
        uint8_t *t = size ? (uint8_t *)HeapAlloc(GetProcessHeap(), 0, size) : NULL;
        if (t && GetSystemFirmwareTable(0x52534D42u, 0, t, size) == size && size > 8) {
            const uint8_t *p = t + 8, *end = t + size;
            while (p + 4 <= end) {
                uint8_t type = p[0], len = p[1];
                const uint8_t *q;
                if (len < 4) break;
                if (type == 1 && len >= 0x18 && p + 0x18 <= end) {
                    static const uint8_t ff[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
                    static const uint8_t zz[16];
                    if (memcmp(p + 8, ff, 16) && memcmp(p + 8, zz, 16)) add(c, &n, max, "smbios_uuid", salt, p + 8, 16);
                    break;
                }
                q = p + len;
                while (q + 1 < end && (q[0] || q[1])) q++;
                p = q + 2;
            }
        }
        if (t) HeapFree(GetProcessHeap(), 0, t);
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
    static const char *core[] = { "machine_guid", "smbios_uuid", "disk_serial", "board", "cpu_brand" };
    og_sha256_init(&s);
    for (i = 0; i < n; i++) {
        size_t k;
        for (k = 0; k < sizeof core / sizeof *core; k++)
            if (!strcmp(c[i].name, core[k])) { og_sha256_update(&s, c[i].hex, 64); break; }
    }
    og_sha256_final(&s, out);
    og_hex(out, 32, hex);
}

int og_random(void *p, size_t n)
{
    typedef BOOLEAN (WINAPI *rtlgenrandom_t)(PVOID, ULONG);
    static rtlgenrandom_t gen;
    if (!gen) gen = (rtlgenrandom_t)GetProcAddress(LoadLibraryA("advapi32.dll"), "SystemFunction036");
    return gen && gen(p, (ULONG)n);
}

/* identity for the server hello: unique id, in-memory code hash of the exe and the hashed components */
void og_identity(uint8_t uid[32], uint8_t exe_code[32], og_component *comp, int *ncomp)
{
    static og_component cache[OG_MAX_COMPONENTS];
    static int ncache = -1;
    static char uid_hex[65];
    char hex[65];
    int i;
    if (ncache < 0) {
        ncache = og_hwid_collect(cache, OG_MAX_COMPONENTS, og_cfg.salt);
        og_hwid_unique(cache, ncache, uid_hex);
    }
    for (i = 0; i < 32; i++) { unsigned v; sscanf(uid_hex + i * 2, "%2x", &v); uid[i] = (uint8_t)v; }
    memset(exe_code, 0, 32);
    if (og_hash_module_code(GetModuleHandleA(NULL), hex))
        for (i = 0; i < 32; i++) { unsigned v; sscanf(hex + i * 2, "%2x", &v); exe_code[i] = (uint8_t)v; }
    memcpy(comp, cache, sizeof(og_component) * ncache);
    *ncomp = ncache;
}
