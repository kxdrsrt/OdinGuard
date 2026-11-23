/* Optional input filter: drops synthetic (SendInput / keybd_event / mouse_event) keyboard and mouse input while
 * the game window has focus. Hardware input and other applications are not affected. */
#include "og.h"
#include <string.h>

static volatile LONG g_on;
static HANDLE g_thread;
static DWORD g_tid;
static DWORD g_last_report;

static int game_has_focus(void)
{
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static void report(const char *what)
{
    DWORD now = GetTickCount();
    uint8_t m[64];
    size_t n = strlen(what);
    if (now - g_last_report < 60000 && g_last_report) return;
    g_last_report = now;
    m[0] = OG_C_DETECT; m[1] = 0; m[2] = 0; m[3] = OG_KIND_INPUT;
    memcpy(m + 4, what, n);
    og_net_broadcast_control(m, n + 4);
}

static LRESULT CALLBACK kb_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_on) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
        if ((k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) && game_has_focus()) {
            report("synthetic keyboard input blocked");
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static LRESULT CALLBACK mouse_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_on) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;
        if ((m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) && game_has_focus()) {
            report("synthetic mouse input blocked");
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static DWORD WINAPI input_thread(LPVOID p)
{
    MSG msg;
    HHOOK kb = SetWindowsHookExA(WH_KEYBOARD_LL, kb_proc, GetModuleHandleA(NULL), 0);
    HHOOK ms = SetWindowsHookExA(WH_MOUSE_LL, mouse_proc, GetModuleHandleA(NULL), 0);
    (void)p;
    og_log("input filter %s", kb && ms ? "active" : "failed");
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { }
    if (kb) UnhookWindowsHookEx(kb);
    if (ms) UnhookWindowsHookEx(ms);
    return 0;
}

void og_input_enable(int on)
{
    InterlockedExchange(&g_on, on ? 1 : 0);
    if (on && !g_thread) g_thread = CreateThread(NULL, 0, input_thread, NULL, 0, &g_tid);
}
