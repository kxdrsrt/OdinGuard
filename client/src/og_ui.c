/* Hooks inside the game exe. Every hook is bound to a byte signature of the target function; when the exe is a
 * different build the signature does not match and the hook is skipped (logged), so OdinGuard never patches
 * unknown code.
 *
 * UIWindow::TextOutWithOutline (2018-06-20: 0x006fd780, thiscall, ret 0x24):
 *   (x, y, text, len, fg colour, outline colour, font type, font height, bold)
 * draws the outline in four passes and then the text. The name balloons over characters use it, so colour nicks
 * replace the fg colour here. */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include "../../third_party/minhook/include/MinHook.h"

void og_text_outline_scope(int enter);
int og_colors_lookup_a(const char *text, int len, COLORREF *out);

typedef void (__fastcall *textoutline_t)(void *self, void *edx, int x, int y, const char *text, int len,
                                         COLORREF fg, COLORREF outline, int font, int height, int bold);
static textoutline_t o_textoutline;

static void __fastcall h_textoutline(void *self, void *edx, int x, int y, const char *text, int len,
                                     COLORREF fg, COLORREF outline, int font, int height, int bold)
{
    COLORREF want;
    if (text && og_colors_lookup_a(text, len, &want)) fg = want;
    og_text_outline_scope(1);
    o_textoutline(self, edx, x, y, text, len, fg, outline, font, height, bold);
    og_text_outline_scope(0);
}

typedef struct { const char *name; uintptr_t rva; const uint8_t *sig; size_t siglen; void *detour; void **orig; } og_patch;

static const uint8_t sig_textoutline[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x98, 0x92, 0xB8, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
                                           0x50, 0x83, 0xEC, 0x24, 0x53, 0x56, 0x57 };

/* alpha.exe is packed: its code is written after the DLLs are loaded, so the hooks wait until the
   signatures appear (checked every 100 ms for up to 60 s) */
static DWORD WINAPI ui_thread(LPVOID p)
{
    og_patch patches[] = {
        { "TextOutWithOutline", 0x006fd780 - 0x400000, sig_textoutline, sizeof sig_textoutline, (void *)h_textoutline, (void **)&o_textoutline },
    };
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    size_t i;
    int ok = 0, waited;
    (void)p;
    for (i = 0; i < sizeof patches / sizeof *patches; i++) {
        uint8_t *target = base + patches[i].rva;
        int found = 0;
        for (waited = 0; waited < 600 && !found; waited++) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(target, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && memcmp(target, patches[i].sig, patches[i].siglen) == 0)
                found = 1;
            else
                Sleep(100);
        }
        if (!found) {
            og_log("ui hook %s: signature not found (different client build), skipped", patches[i].name);
            continue;
        }
        if (MH_CreateHook(target, patches[i].detour, patches[i].orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
            og_log("ui hook %s: failed", patches[i].name);
            continue;
        }
        ok++;
    }
    og_log("ui hooks: %d of %d active", ok, (int)(sizeof patches / sizeof *patches));
    return 0;
}

void og_ui_init(void)
{
    CloseHandle(CreateThread(NULL, 0, ui_thread, NULL, 0, NULL));
}

#ifdef OG_TEST_CURSOR
/* test builds only: the Parallels VM forces the guest cursor to the host mouse, so automated tests put the game
   cursor where odinguard.ini [Test] Cursor=x,y (client coordinates, re-read every second) says */
typedef BOOL (WINAPI *getcursorpos_t)(LPPOINT);
static getcursorpos_t o_getcursorpos;
static volatile LONG g_cx = -1, g_cy = -1;

static BOOL WINAPI h_getcursorpos(LPPOINT p)
{
    BOOL r = o_getcursorpos(p);
    if (r && g_cx >= 0) {
        POINT c = { g_cx, g_cy };
        HWND w = FindWindowA(NULL, "Alpha - 20180620");
        if (w && ClientToScreen(w, &c)) *p = c;
    }
    return r;
}

static DWORD WINAPI cursor_thread(LPVOID unused)
{
    char ini[MAX_PATH], v[64];
    (void)unused;
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    for (;;) {
        int x = -1, y = -1;
        GetPrivateProfileStringA("Test", "Cursor", "", v, sizeof v, ini);
        if (sscanf(v, "%d,%d", &x, &y) != 2) x = y = -1;
        InterlockedExchange(&g_cx, x);
        InterlockedExchange(&g_cy, y);
        {
            int k;
            for (k = 0; k < 5; k++) {
                HWND w = FindWindowA(NULL, "Alpha - 20180620");
                if (w && x >= 0) PostMessageA(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Sleep(100);
            }
        }
    }
    return 0;
}

void og_test_cursor_init(void)
{
    void *t = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetCursorPos");
    if (t && MH_CreateHook(t, (void *)h_getcursorpos, (void **)&o_getcursorpos) == MH_OK && MH_EnableHook(t) == MH_OK) {
        CloseHandle(CreateThread(NULL, 0, cursor_thread, NULL, 0, NULL));
        og_log("TEST BUILD: cursor override active");
    }
}
#endif
