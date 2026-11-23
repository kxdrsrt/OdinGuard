#include "og.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE *g_log;
HINSTANCE og_instance;
og_policy_t og_policy;
CRITICAL_SECTION og_policy_lock;

void og_log_open(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\odinguard.log", dir);
    g_log = fopen(p, "a");
}

void og_log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    if (!g_log) return;
    GetLocalTime(&t);
    fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* message box with the guard icon and the operator name as caption */
int og_message(const char *text, UINT type)
{
    MSGBOXPARAMSA p;
    ZeroMemory(&p, sizeof p);
    p.cbSize = sizeof p;
    p.hInstance = og_instance;
    p.lpszText = text;
    p.lpszCaption = "OdinGuard Anti Cheat - " OG_PROFILE_OPERATOR;
    p.dwStyle = (type & ~MB_ICONMASK) | MB_USERICON | MB_TOPMOST | MB_SETFOREGROUND;
    p.lpszIcon = MAKEINTRESOURCEA(1);
    return MessageBoxIndirectA(&p);
}

int og_contains_i(const char *hay, const char *needle)
{
    size_t n = strlen(needle), h = strlen(hay), i;
    if (!n || n > h) return 0;
    for (i = 0; i + n <= h; i++)
        if (_strnicmp(hay + i, needle, n) == 0) return 1;
    return 0;
}

int og_path_tolerated(const char *path)
{
    static const char *ok[] = { "\\Warsaw\\", "\\GAS Tecnologia\\", "\\Diebold\\", "\\Topaz OFD\\",
                                "\\NVIDIA Corporation\\", "\\AMD\\", "\\Steam\\", "\\Discord\\", "\\Overwolf\\",
                                "\\Parallels\\", "\\Microsoft\\", "\\Windows Defender\\", "\\RivaTuner", "\\MSI Afterburner\\" };
    size_t i;
    int k, hit = 0;
    for (i = 0; i < sizeof ok / sizeof *ok; i++) if (og_contains_i(path, ok[i])) return 1;
    EnterCriticalSection(&og_policy_lock);
    for (k = 0; k < og_policy.nsig && !hit; k++)
        if (og_policy.sig[k].kind == OG_KIND_ALLOW_PATH && og_contains_i(path, og_policy.sig[k].pattern)) hit = 1;
    LeaveCriticalSection(&og_policy_lock);
    return hit;
}
