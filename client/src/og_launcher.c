/* launcher module: the game only runs when the patcher started it (OpenThor launch ticket). The patcher writes
 * <client>\_patch\launch-<token>.ticket and starts the game with OPENTHOR_LAUNCH=<token>; a ticket is valid for 5 minutes
 * and only once. Started directly, the player is asked to use the patcher, which is started on "Yes"; the game ends
 * before its window opens. This keeps players from skipping updates; it is not a security boundary. */
#include "og.h"
#include <stdio.h>
#include <string.h>

#if OG_MOD_LAUNCHER
#define TICKET_SECS 300

static int ticket_ok(void)
{
    char tok[80], path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA a;
    ULARGE_INTEGER t, now;
    FILETIME ft;
    size_t i, n;
    DWORD got = GetEnvironmentVariableA("OPENTHOR_LAUNCH", tok, sizeof tok);
    if (got == 0 || got >= sizeof tok) return 0;
    n = strlen(tok);
    if (n < 16) return 0;
    for (i = 0; i < n; i++)
        if (!((tok[i] >= '0' && tok[i] <= '9') || (tok[i] >= 'a' && tok[i] <= 'f'))) return 0;
    snprintf(path, sizeof path, "%s\\_patch\\launch-%s.ticket", og_client_dir, tok);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
    t.LowPart = a.ftLastWriteTime.dwLowDateTime;
    t.HighPart = a.ftLastWriteTime.dwHighDateTime;
    GetSystemTimeAsFileTime(&ft);
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    DeleteFileA(path); /* one start per ticket */
    /* the variable must not reach programs the game starts later (e.g. the patcher itself) */
    SetEnvironmentVariableA("OPENTHOR_LAUNCH", NULL);
    return now.QuadPart >= t.QuadPart ? (now.QuadPart - t.QuadPart) / 10000000ull <= TICKET_SECS : 1;
}

void og_launcher_check(void)
{
    char exe[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    if (ticket_ok()) {
        og_log("launcher: started by the patcher");
        return;
    }
    og_log("launcher: started without the patcher, refused");
    if (og_message(og_tr(OG_T_LAUNCHER), MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        snprintf(exe, sizeof exe, "%s\\%s", og_client_dir, OG_PROFILE_LAUNCHER_EXE);
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        if (CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, og_client_dir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            og_log("launcher: %s could not be started (%lu)", exe, GetLastError());
        }
    }
    TerminateProcess(GetCurrentProcess(), 0);
}
#endif
