/* Start splash: the OdinGuard card fades in on top of the screen while the client loads, then fades out.
 * Layered, topmost, no taskbar entry, never takes the focus. The operator name of the profile is drawn onto the
 * card at run time, so one splash bitmap serves every operator. */
#include "og.h"
#include "og_version.h"
#include <stdio.h>

#define SPLASH_BITMAP 100
#define SPLASH_ICON 1

static HINSTANCE g_inst;

static void blend(HWND w, HDC mem, SIZE sz, BYTE alpha)
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    POINT src = { 0, 0 };
    UpdateLayeredWindow(w, NULL, NULL, &sz, mem, &src, 0, &bf, ULW_ALPHA);
}

/* draws the operator name into the premultiplied 32-bit DIB: GDI writes into a grey mask, the mask becomes the
   alpha of a gold colour that is blended over the card */
static void draw_operator(HBITMAP bmp, const BITMAP *bm)
{
    BITMAPINFO bi;
    uint8_t *mask = NULL, *dst = (uint8_t *)bm->bmBits;
    HDC dc;
    HBITMAP mb;
    HFONT font, oldf;
    HGDIOBJ oldb;
    RECT r;
    int x, y, w = bm->bmWidth, h = bm->bmHeight;
    (void)bmp;
    if (!dst || bm->bmBitsPixel != 32) return;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    dc = CreateCompatibleDC(NULL);
    mb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&mask, NULL, 0);
    if (!mb) { DeleteDC(dc); return; }
    oldb = SelectObject(dc, mb);
    font = CreateFontA(-17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Georgia");
    oldf = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetRect(&r, 250, h - 74, w - 30, h - 40);
    DrawTextA(dc, OG_PROFILE_OPERATOR, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    GdiFlush();
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        uint8_t *m = mask + (y * w + x) * 4, *d = dst + (y * w + x) * 4;
        unsigned a = m[1];
        if (!a) continue;
        /* gold (BGR 0x50, 0xC8, 0xF0), premultiplied, over the card */
        d[0] = (uint8_t)((0x50 * a + d[0] * (255 - a)) / 255);
        d[1] = (uint8_t)((0xC8 * a + d[1] * (255 - a)) / 255);
        d[2] = (uint8_t)((0xF0 * a + d[2] * (255 - a)) / 255);
        d[3] = (uint8_t)(a + d[3] * (255 - a) / 255);
    }
    SelectObject(dc, oldf);
    DeleteObject(font);
    SelectObject(dc, oldb);
    DeleteObject(mb);
    DeleteDC(dc);
}

static DWORD WINAPI splash_thread(LPVOID p)
{
    WNDCLASSA wc;
    HBITMAP bmp;
    BITMAP bm;
    HDC screen, mem;
    HGDIOBJ old;
    HWND w;
    SIZE sz;
    RECT work;
    int i;
    DWORD hold = (DWORD)(UINT_PTR)p;

    bmp = (HBITMAP)LoadImageA(g_inst, MAKEINTRESOURCEA(SPLASH_BITMAP), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bmp) return 0;
    GetObjectA(bmp, sizeof bm, &bm);
    sz.cx = bm.bmWidth;
    sz.cy = bm.bmHeight;
    draw_operator(bmp, &bm);

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = g_inst;
    wc.lpszClassName = "OdinGuardSplash";
    wc.hIcon = LoadIconA(g_inst, MAKEINTRESOURCEA(SPLASH_ICON));
    RegisterClassA(&wc);

    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    w = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                        wc.lpszClassName, "OdinGuard", WS_POPUP,
                        work.left + (work.right - work.left - sz.cx) / 2, work.top + (work.bottom - work.top - sz.cy) / 2,
                        sz.cx, sz.cy, NULL, NULL, g_inst, NULL);
    if (!w) { DeleteObject(bmp); return 0; }

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    old = SelectObject(mem, bmp);
    blend(w, mem, sz, 0);
    ShowWindow(w, SW_SHOWNOACTIVATE);
    for (i = 1; i <= 12; i++) { blend(w, mem, sz, (BYTE)(i * 255 / 12)); Sleep(16); }
    {
        DWORD end = GetTickCount() + hold;
        MSG msg;
        while ((LONG)(end - GetTickCount()) > 0) {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
            Sleep(20);
        }
    }
    for (i = 12; i >= 0; i--) { blend(w, mem, sz, (BYTE)(i * 255 / 12)); Sleep(20); }
    DestroyWindow(w);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    DeleteObject(bmp);
    return 0;
}

void og_splash_show(HINSTANCE inst, DWORD hold_ms)
{
    g_inst = inst;
    if (hold_ms) CloseHandle(CreateThread(NULL, 0, splash_thread, (LPVOID)(UINT_PTR)hold_ms, 0, NULL));
}

/* After a successful handshake the game window shows that the server is protected, like Gepard's title:
   "<operator> | OdinGuard Anti Cheat <version>". The window is found by its process and kept up to date, because the
   handshake can finish before the game window is visible and the client may set its own title again. */
static BOOL CALLBACK title_enum(HWND w, LPARAM lp)
{
    DWORD pid = 0;
    char cls[64], cur[256];
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    GetClassNameA(w, cls, sizeof cls);
    if (!_stricmp(cls, "OdinGuardSplash")) return TRUE;
    cur[0] = 0;
    GetWindowTextA(w, cur, sizeof cur);
    if (!cur[0]) return TRUE;
    *(HWND *)lp = w;
    return FALSE;
}

static volatile LONG g_title_state;   /* 0 = waiting for the server, 1 = protected, 2 = handshake failed */
static char g_title_note[100];

static DWORD WINAPI title_thread(LPVOID unused)
{
    static const char base[] = OG_PROFILE_OPERATOR " | OdinGuard Anti Cheat " OG_VERSION_STR;
    (void)unused;
    /* For the life of the process: the client sets its own title again at times (window mode switch, map change), and the
       first window can appear long after the guard started. One enumeration every 1.5 s costs nothing.
       The title always says what the guard is doing, so a player (or you) can see at a glance whether it is active. */
    for (;;) {
        HWND w = NULL;
        char want[256];
        LONG st = g_title_state;
        if (st == 1) snprintf(want, sizeof want, "%s", base);
        else if (st == 2) snprintf(want, sizeof want, "%s - NOT CONNECTED (%s)", base, g_title_note);
        else snprintf(want, sizeof want, "%s - waiting for the server", base);
        EnumWindows(title_enum, (LPARAM)&w);
        if (w) {
            char cur[256];
            cur[0] = 0;
            GetWindowTextA(w, cur, sizeof cur);
            if (strcmp(cur, want)) SetWindowTextA(w, want);
        }
        Sleep(1500);
    }
    return 0;
}

void og_title_start(void)
{
    static volatile LONG started;
    if (InterlockedExchange(&started, 1)) return;
    CloseHandle(CreateThread(NULL, 0, title_thread, NULL, 0, NULL));
}

void og_title_fail(void)
{
    /* a failed attempt must not overwrite a working connection (the client opens several sockets) */
    if (g_title_state == 1) return;
    snprintf(g_title_note, sizeof g_title_note, "%s", og_last_log());
    g_title_state = 2;
}

void og_title_apply(void)
{
    g_title_state = 1;
    og_title_start();
}
