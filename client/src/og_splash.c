/* Start splash: the OdinGuard card fades in on top of the screen while the client loads, then fades out.
 * Layered, topmost, no taskbar entry, never takes the focus. Also provides the branded message box. */
#include "og.h"

#define SPLASH_BITMAP 100
#define SPLASH_ICON 1

static HINSTANCE g_inst;

static void blend(HWND w, HDC mem, SIZE sz, BYTE alpha)
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    POINT src = { 0, 0 };
    UpdateLayeredWindow(w, NULL, NULL, &sz, mem, &src, 0, &bf, ULW_ALPHA);
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

int og_message(const char *text, UINT type)
{
    MSGBOXPARAMSA p;
    ZeroMemory(&p, sizeof p);
    p.cbSize = sizeof p;
    p.hInstance = g_inst;
    p.lpszText = text;
    p.lpszCaption = "OdinGuard";
    p.dwStyle = (type & ~MB_ICONMASK) | MB_USERICON | MB_TOPMOST | MB_SETFOREGROUND;
    p.lpszIcon = MAKEINTRESOURCEA(SPLASH_ICON);
    return MessageBoxIndirectA(&p);
}
