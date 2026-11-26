#include "og.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tlhelp32.h>

#define PKT_RING 32
static struct { uint16_t id; uint16_t len; uint8_t dir; DWORD tick; } g_ring[PKT_RING];
static volatile LONG g_ring_pos;
static char g_last_file[64];

const char *og_crash_last_file(void) { return g_last_file; }

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
    snprintf(g_last_file, sizeof g_last_file, "crash-%04d%02d%02d-%02d%02d%02d.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    snprintf(path, sizeof path, "%s\\%s", dir, g_last_file);
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

/* ---------------------------------------------------------------- upload (OG_S_CRASH)
 * After the map login the server sends an upload URL and a session token. Reports in odinguard-crash\ that were not
 * sent yet are uploaded only when the player agreed once (odinguard.ini [crash] upload=yes; the question is asked
 * the first time there is something to send). Sent files are listed in odinguard-crash\sent.txt. */
#include <winhttp.h>

static char g_up_url[400], g_up_token[65];
static volatile LONG g_up_busy;

static int already_sent(const char *dir, const char *name)
{
    char p[MAX_PATH], line[128];
    FILE *f;
    int found = 0;
    snprintf(p, sizeof p, "%s\\sent.txt", dir);
    if (!(f = fopen(p, "r"))) return 0;
    while (!found && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        found = !strcmp(line, name);
    }
    fclose(f);
    return found;
}

static int post_file(const char *path, const char *name)
{
    wchar_t wurl[400], host[256], upath[1024];
    URL_COMPONENTS u;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    char *body = NULL, hdr[512];
    wchar_t whdr[512];
    DWORD status = 0, sl = sizeof status, len;
    long size;
    int ok = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 512 * 1024 || !(body = malloc((size_t)size))) { fclose(f); return 0; }
    len = (DWORD)fread(body, 1, (size_t)size, f);
    fclose(f);

    MultiByteToWideChar(CP_UTF8, 0, g_up_url, -1, wurl, 400);
    memset(&u, 0, sizeof u);
    u.dwStructSize = sizeof u;
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = upath; u.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &u)) goto out;
    ses = WinHttpOpen(L"OdinGuard", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto out;
    WinHttpSetTimeouts(ses, 10000, 10000, 20000, 20000);
    if (!(con = WinHttpConnect(ses, host, u.nPort, 0))) goto out;
    req = WinHttpOpenRequest(con, L"POST", upath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto out;
    snprintf(hdr, sizeof hdr, "Content-Type: text/plain; charset=utf-8\r\nX-OdinGuard-Token: %s\r\nX-OdinGuard-File: %s\r\nX-OdinGuard-Version: %s",
             g_up_token, name, OG_VERSION_STR);
    MultiByteToWideChar(CP_UTF8, 0, hdr, -1, whdr, 512);
    if (!WinHttpSendRequest(req, whdr, (DWORD)-1L, body, len, len, 0) || !WinHttpReceiveResponse(req, NULL)) goto out;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sl, WINHTTP_NO_HEADER_INDEX);
    ok = status >= 200 && status < 300;
    og_log("crash upload %s: HTTP %lu", name, status);
out:
    if (!ok && !status) og_log("crash upload %s: failed (%lu)", name, GetLastError());
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    free(body);
    return ok;
}

static DWORD WINAPI upload_thread(LPVOID unused)
{
    char dir[MAX_PATH], pattern[MAX_PATH], ini[MAX_PATH], answer[8], names[10][64];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0, i;
    (void)unused;
    snprintf(dir, sizeof dir, "%s\\odinguard-crash", og_client_dir);
    snprintf(pattern, sizeof pattern, "%s\\crash-*.txt", dir);
    if ((h = FindFirstFileA(pattern, &fd)) != INVALID_HANDLE_VALUE) {
        do {
            if (n < 10 && strlen(fd.cFileName) < sizeof names[0] && !already_sent(dir, fd.cFileName))
                snprintf(names[n++], sizeof names[0], "%s", fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (n == 0) goto done;
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    GetPrivateProfileStringA("crash", "upload", "", answer, sizeof answer, ini);
    if (!answer[0]) {
        char text[700];
        snprintf(text, sizeof text,
                 "OdinGuard found %d crash report(s) of this game.\n\n"
                 "Send them to the server team to help fix the crash?\n%s\n\n"
                 "A report holds the error address, the loaded modules with their file paths, DATA.INI and the "
                 "headers of the last network packets. No passwords, no chat.\n\n"
                 "Your answer is remembered (odinguard.ini, [crash] upload).", n, g_up_url);
        snprintf(answer, sizeof answer, "%s",
                 MessageBoxA(NULL, text, "OdinGuard: crash reports", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES ? "yes" : "no");
        WritePrivateProfileStringA("crash", "upload", answer, ini);
    }
    if (_stricmp(answer, "yes")) { og_log("crash upload: %d report(s) not sent (player declined)", n); goto done; }
    for (i = 0; i < n; i++) {
        char path[MAX_PATH], sent[MAX_PATH];
        FILE *s;
        snprintf(path, sizeof path, "%s\\%s", dir, names[i]);
        if (!post_file(path, names[i])) continue;
        snprintf(sent, sizeof sent, "%s\\sent.txt", dir);
        if ((s = fopen(sent, "a"))) { fprintf(s, "%s\n", names[i]); fclose(s); }
    }
done:
    InterlockedExchange(&g_up_busy, 0);
    return 0;
}

void og_crash_endpoint(const char *url, const char *token)
{
    HANDLE t;
    if (InterlockedExchange(&g_up_busy, 1)) return;
    snprintf(g_up_url, sizeof g_up_url, "%s", url);
    snprintf(g_up_token, sizeof g_up_token, "%s", token);
    t = CreateThread(NULL, 0, upload_thread, NULL, 0, NULL);
    if (t) CloseHandle(t); else InterlockedExchange(&g_up_busy, 0);
}
