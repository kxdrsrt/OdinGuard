#include "og.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>

static int reg_contains(const char *key, const char *val, const char **needles, char *hit, size_t cap)
{
    HKEY k;
    char buf[256];
    DWORD type, sz = sizeof buf - 1;
    int i, found = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(k, val, NULL, &type, (BYTE *)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
        buf[sz] = 0;
        for (i = 0; needles[i]; i++)
            if (strstr(buf, needles[i])) { snprintf(hit, cap, "firmware:%s", buf); found = 1; break; }
    }
    RegCloseKey(k);
    return found;
}

/* CPUID hypervisor bit, and as second source the firmware strings (the bit is not visible under x86
   emulation on ARM, e.g. Parallels on Apple Silicon) */
int og_is_vm(char *why, size_t cap)
{
    static const char *vendors[] = { "Parallels", "VMware", "VirtualBox", "innotek", "QEMU", "KVM", "Xen", "Bochs",
                                     "Virtual Machine", "BHYVE", "Hyper-V", NULL };
    int r[4];
    __cpuid(r, 1);
    if (r[2] & (1 << 31)) {
        char v[13];
        __cpuid(r, 0x40000000);
        memcpy(v, &r[1], 4); memcpy(v + 4, &r[2], 4); memcpy(v + 8, &r[3], 4); v[12] = 0;
        snprintf(why, cap, "hypervisor:%s", v);
        return 1;
    }
    if (reg_contains("HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer", vendors, why, cap) ||
        reg_contains("HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName", vendors, why, cap) ||
        reg_contains("HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSVendor", vendors, why, cap))
        return 1;
    why[0] = 0;
    return 0;
}

/* N named mutexes act as slots shared by all processes on the machine (all sessions).
   The handle stays open for the lifetime of the process; the slot is freed when the process exits. */
int og_window_slot_acquire(int max_windows)
{
    static HANDLE slots[64];
    static int used;
    HANDLE h;
    char name[64];
    int i;
    if (max_windows <= 0) return 1;
    for (i = 0; i < max_windows && used < 64; i++) {
        snprintf(name, sizeof name, "Global\\OdinGuard_Slot_%d", i);
        h = CreateMutexA(NULL, FALSE, name);
        if (!h) {
            snprintf(name, sizeof name, "Local\\OdinGuard_Slot_%d", i);
            h = CreateMutexA(NULL, FALSE, name);
            if (!h) continue;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); continue; }
        slots[used++] = h;
        (void)slots;
        return 1;
    }
    return 0;
}
