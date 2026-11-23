#include "og.h"
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

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
