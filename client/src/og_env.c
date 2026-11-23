#include "og.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>

int og_is_vm(char *why, size_t cap)
{
    int r[4];
    __cpuid(r, 1);
    if (r[2] & (1 << 31)) {
        char v[13];
        __cpuid(r, 0x40000000);
        memcpy(v, &r[1], 4); memcpy(v + 4, &r[2], 4); memcpy(v + 8, &r[3], 4); v[12] = 0;
        snprintf(why, cap, "hypervisor:%s", v);
        return 1;
    }
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
