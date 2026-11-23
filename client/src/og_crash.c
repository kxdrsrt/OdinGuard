#include "og.h"
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

#define PKT_RING 32
static struct { uint16_t id; uint16_t len; uint8_t dir; DWORD tick; } g_ring[PKT_RING];
static volatile LONG g_ring_pos;

/* Called by the network hooks (O2): keeps the last packet headers for the crash log. */
void og_crash_note_packet(int outbound, uint16_t id, uint16_t len)
{
    LONG i = (InterlockedIncrement(&g_ring_pos) - 1) % PKT_RING;
    g_ring[i].id = id; g_ring[i].len = len; g_ring[i].dir = (uint8_t)outbound; g_ring[i].tick = GetTickCount();
}

static void dump_grfs(FILE *f)
{
    static const char *names[] = { "DATA.INI", "GRF.INI" };
    int i;
    for (i = 0; i < 2; i++) {
        char p[MAX_PATH], line[512];
        FILE *in;
        snprintf(p, sizeof p, "%s\\%s", og_client_dir, names[i]);
        in = fopen(p, "r");
        if (!in) continue;
        fprintf(f, "\n[%s]\n", names[i]);
        while (fgets(line, sizeof line, in)) fputs(line, f);
        fclose(in);
    }
}

void og_crash_write(EXCEPTION_POINTERS *ep, const char *reason)
{
    char path[MAX_PATH], dir[MAX_PATH];
    SYSTEMTIME t;
    FILE *f;
    int i;
    char mods[8192];
    int unk;

    GetLocalTime(&t);
    snprintf(dir, sizeof dir, "%s\\odinguard-crash", og_client_dir);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof path, "%s\\crash-%04d%02d%02d-%02d%02d%02d.txt", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "OdinGuard %s crash report\nreason: %s\n", OG_VERSION_STR, reason);
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        const CONTEXT *c = ep->ContextRecord;
        const DWORD *sp;
        DWORD ebp = c->Ebp;
        fprintf(f, "exception: 0x%08lX at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        fprintf(f, "eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX\nesi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX\n",
                c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, c->Eip);

        fprintf(f, "\n[call stack via EBP chain]\n");
        for (i = 0; i < 32 && ebp && !IsBadReadPtr((void *)ebp, 8); i++) {
            DWORD ret = ((DWORD *)ebp)[1];
            fprintf(f, "  %08lX\n", ret);
            if (((DWORD *)ebp)[0] <= ebp) break;
            ebp = ((DWORD *)ebp)[0];
        }

        fprintf(f, "\n[stack data]\n");
        sp = (const DWORD *)c->Esp;
        for (i = 0; i < 64 && !IsBadReadPtr((void *)(sp + i), 4); i++)
            fprintf(f, "%08lX%s", sp[i], (i % 8 == 7) ? "\n" : " ");
    } else {
        fprintf(f, "(on-demand report, no exception)\n");
    }

    fprintf(f, "\n[modules]\n");
    og_scan_modules(mods, sizeof mods, &unk);
    fputs(mods, f);
    dump_grfs(f);

    fprintf(f, "\n[last packets]  (dir id len tick)\n");
    {
        LONG pos = g_ring_pos;
        for (i = 0; i < PKT_RING; i++) {
            LONG k = (pos + i) % PKT_RING;
            if (g_ring[k].tick) fprintf(f, "  %s 0x%04X %u %lu\n", g_ring[k].dir ? "C->S" : "S->C", g_ring[k].id, g_ring[k].len, g_ring[k].tick);
        }
    }
    fclose(f);
    og_log("crash report written: %s", path);
}

static LONG WINAPI filter(EXCEPTION_POINTERS *ep)
{
    og_crash_write(ep, "unhandled exception");
    return EXCEPTION_CONTINUE_SEARCH; /* let the normal handling (and other handlers) continue */
}

void og_crash_install(void)
{
    SetUnhandledExceptionFilter(filter);
}
