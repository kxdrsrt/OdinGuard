/* End-to-end test of the network layer: loads odinguard.dll (hooks ws2_32), then talks plain RO packets.
 *   nettest <ip> <login-port> <user> <password> [char-port]
 * The DLL encrypts everything transparently; the test only sees normal game packets. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ULONGLONG wall_ms(void)
{
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    return (((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime) / 10000;
}

static int recv_some(SOCKET s, unsigned char *buf, int cap, int ms)
{
    DWORD end = GetTickCount() + ms;
    int got = 0;
    while ((LONG)(end - GetTickCount()) > 0) {
        fd_set rd;
        struct timeval tv = { 0, 50000 };
        int r;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        if (select(0, &rd, NULL, NULL, &tv) <= 0) { if (got) break; continue; }
        r = recv(s, (char *)buf + got, cap - got, 0);
        if (r > 0) { got += r; continue; }
        if (r == 0) break;
        if (WSAGetLastError() != WSAEWOULDBLOCK) break;
    }
    return got;
}

/* prints every chat/system message (0x008E) in buf */
static void print_messages(const char *what, const unsigned char *buf, int n)
{
    int k, any = 0;
    for (k = 0; k + 4 < n; k++) {
        if (buf[k] == 0x8E && buf[k + 1] == 0x00) {
            int plen = buf[k + 2] | (buf[k + 3] << 8);
            if (plen > 4 && k + plen <= n) { printf("MAP: %-12s -> %.*s\n", any ? "" : what, plen - 4, buf + k + 4); any = 1; k += plen - 1; }
        }
    }
    if (!any && what[0] != '(') printf("MAP: %-12s -> (no answer)\n", what);
}

static SOCKET dial(const char *ip, int port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    u_long nb = 1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = inet_addr(ip);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { printf("connect %s:%d failed %d\n", ip, port, WSAGetLastError()); return INVALID_SOCKET; }
    ioctlsocket(s, FIONBIO, &nb); /* like the game client */
    return s;
}

/* ---- tamper test stubs (detection tests only; they act on this test process and nothing else) ---- */
#include "MinHook.h"
static DWORD (WINAPI *real_tick)(void);
static BOOL (WINAPI *real_qpc)(LARGE_INTEGER *);
static int g_factor = 1;
static DWORD g_tick0;
static LONGLONG g_qpc0;
static DWORD WINAPI fast_tick(void) { DWORD t = real_tick(); return g_tick0 + (t - g_tick0) * (DWORD)g_factor; }
static BOOL WINAPI fast_qpc(LARGE_INTEGER *c)
{
    BOOL r = real_qpc(c);
    if (r) c->QuadPart = g_qpc0 + (c->QuadPart - g_qpc0) * g_factor;
    return r;
}

/* OG_SPEEDTEST=<factor>: time APIs of this process run <factor> times faster (speed-hack pattern) */
static void speed_test(int factor)
{
    LARGE_INTEGER q;
    HMODULE k = GetModuleHandleA("kernel32.dll");
    g_factor = factor;
    g_tick0 = GetTickCount();
    QueryPerformanceCounter(&q);
    g_qpc0 = q.QuadPart;
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook((LPVOID)GetProcAddress(k, "GetTickCount"), (LPVOID)fast_tick, (LPVOID *)&real_tick) != MH_OK ||
        MH_CreateHook((LPVOID)GetProcAddress(k, "QueryPerformanceCounter"), (LPVOID)fast_qpc, (LPVOID *)&real_qpc) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { printf("speed test: hooks failed\n"); return; }
    printf("speed test: time x%d\n", factor);
}

/* OG_CODEPATCH=guard|exe: changes one code byte of a function that is never called in this process */
static void unused_function(void) { printf("never called\n"); }
static void code_patch(const char *what)
{
    unsigned char *p = !_stricmp(what, "guard") ? (unsigned char *)GetProcAddress(GetModuleHandleA("odinguard.dll"), "OdinGuard_CanonDump")
                                                : (unsigned char *)unused_function;
    DWORD old;
    if (!p || !VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) { printf("code patch: failed\n"); return; }
    p[0] ^= 0x01;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    printf("code patch: one byte of the %s code changed\n", what);
}

int main(int argc, char **argv)
{
    WSADATA wd;
    unsigned char pkt[55], buf[8192];
    SOCKET s;
    int n;
    unsigned id;
    if (argc < 5) { printf("usage: nettest <ip> <login-port> <user> <password> [char-port [map-port [char-name]]]\n"); return 2; }
    WSAStartup(0x0202, &wd);
    if (!LoadLibraryA("odinguard.dll")) printf("odinguard.dll not loaded (%lu), plain connection\n", GetLastError());

    s = dial(argv[1], atoi(argv[2]));
    if (s == INVALID_SOCKET) return 1;
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 0x64; pkt[1] = 0x00;           /* CA_LOGIN */
    pkt[2] = 55;                             /* version */
    strncpy((char *)pkt + 6, argv[3], 24);
    strncpy((char *)pkt + 30, argv[4], 24);
    pkt[54] = 0;                             /* client type */
    if (send(s, (const char *)pkt, sizeof pkt, 0) != (int)sizeof pkt) { printf("send failed %d\n", WSAGetLastError()); return 1; }
    n = recv_some(s, buf, sizeof buf, 5000);
    if (n < 3) { printf("LOGIN: no answer (%d bytes, error %d)\n", n, WSAGetLastError()); return 1; }
    id = buf[0] | (buf[1] << 8);
    if (id == 0x006A || id == 0x083E) { printf("LOGIN: refused, code %d\n", buf[2]); closesocket(s); return 3; }
    if (id != 0x0069 && id != 0x0AC4) { printf("LOGIN: unexpected packet 0x%04X (%d bytes)\n", id, n); return 1; }
    {
        unsigned login1 = *(unsigned *)(buf + 4), account = *(unsigned *)(buf + 8), login2 = *(unsigned *)(buf + 12);
        unsigned char sex = buf[46];
        printf("LOGIN: accepted, packet 0x%04X, account %u\n", id, account);
        closesocket(s);
        if (argc > 5) {
            unsigned char c[17];
            s = dial(argv[1], atoi(argv[5]));
            if (s == INVALID_SOCKET) return 1;
            c[0] = 0x65; c[1] = 0;
            memcpy(c + 2, &account, 4); memcpy(c + 6, &login1, 4); memcpy(c + 10, &login2, 4);
            c[14] = 0; c[15] = 0; c[16] = sex;
            send(s, (const char *)c, sizeof c, 0);
            n = recv_some(s, buf, sizeof buf, 5000);
            if (n >= 4 && *(unsigned *)buf == account) {
                printf("CHAR: account id echoed, %d bytes total", n);
                if (n > 4) printf(", next packet 0x%04X", buf[4] | (buf[5] << 8));
                printf("\n");
            } else {
                printf("CHAR: unexpected answer (%d bytes)\n", n);
                return 1;
            }
            if (argc > 6) {
                /* select slot 0, then enter the map server and type the OdinGuard chat commands */
                unsigned char sel[3] = { 0x66, 0x00, 0 }, enter[19], ack[2] = { 0x7D, 0x00 };
                unsigned char *p = NULL;
                unsigned charid, mport, i;
                SOCKET m;
                send(s, (const char *)sel, 3, 0);
                n = recv_some(s, buf, sizeof buf, 8000);
                for (i = 0; i + 28 <= (unsigned)n; i++)
                    if ((buf[i] == 0x71 || buf[i] == 0xC5) && buf[i + 1] == (buf[i] == 0x71 ? 0x00 : 0x0A)) { p = buf + i; break; }
                if (!p) { printf("CHAR: no map server answer (%d bytes)\n", n); return 1; }
                charid = *(unsigned *)(p + 2);
                mport = p[26] | (p[27] << 8);
                printf("CHAR: char %u selected, map server port %u\n", charid, mport);
                closesocket(s);
                m = dial(argv[1], atoi(argv[6]));
                if (m == INVALID_SOCKET) return 1;
                enter[0] = 0x36; enter[1] = 0x04;
                memcpy(enter + 2, &account, 4); memcpy(enter + 6, &charid, 4); memcpy(enter + 10, &login1, 4);
                i = GetTickCount(); memcpy(enter + 14, &i, 4); enter[18] = sex;
                send(m, (const char *)enter, sizeof enter, 0);
                n = recv_some(m, buf, sizeof buf, 8000);
                printf("MAP: %d bytes after enter, first packet 0x%04X\n", n, n >= 2 ? buf[0] | (buf[1] << 8) : 0);
                send(m, (const char *)ack, 2, 0);
                recv_some(m, buf, sizeof buf, 2000);
                {
                    static const char *defcmds[] = { "!ping", "!vsync off", "!vsync on", "!crash" };
                    const char *cmds[16];
                    int c, ncmd = 0;
                    char envbuf[1024], *tok, *ctx = NULL;
                    /* OG_CHAT="cmd1;cmd2;..." replaces the default commands, OG_HOLD = seconds to stay afterwards */
                    if (GetEnvironmentVariableA("OG_CHAT", envbuf, sizeof envbuf)) {
                        for (tok = strtok_r(envbuf, ";", &ctx); tok && ncmd < 16; tok = strtok_r(NULL, ";", &ctx)) cmds[ncmd++] = tok;
                    } else {
                        for (c = 0; c < 4; c++) cmds[ncmd++] = defcmds[c];
                    }
                    for (c = 0; c < ncmd; c++) {
                        char chat[160];
                        int len = snprintf(chat + 4, sizeof chat - 4, "%s : %s", argc > 7 ? argv[7] : "OgTester", cmds[c]) + 5;
                        chat[0] = (char)0xF3; chat[1] = 0; chat[2] = (char)len; chat[3] = 0;
                        send(m, chat, len, 0);
                        n = recv_some(m, buf, sizeof buf, 2500);
                        print_messages(cmds[c], buf, n);
                    }
                    /* OG_SKILLSPAM=n: n First Aid requests on yourself without waiting (no-delay pattern);
                       OG_SKILLID / OG_SKILLLV: another self skill (e.g. 369 Gospel) */
                    if (GetEnvironmentVariableA("OG_SKILLSPAM", envbuf, sizeof envbuf)) {
                        int i, cnt = atoi(envbuf), id = 142, lv = 1;
                        unsigned char sk[10] = { 0x38, 0x04, 1, 0, 142, 0 };
                        if (GetEnvironmentVariableA("OG_SKILLID", envbuf, sizeof envbuf)) id = atoi(envbuf);
                        if (GetEnvironmentVariableA("OG_SKILLLV", envbuf, sizeof envbuf)) lv = atoi(envbuf);
                        sk[2] = (unsigned char)lv; sk[4] = (unsigned char)id; sk[5] = (unsigned char)(id >> 8);
                        memcpy(sk + 6, &account, 4);
                        for (i = 0; i < cnt; i++) { send(m, (const char *)sk, 10, 0); Sleep(20); }
                        n = recv_some(m, buf, sizeof buf, 1500);
                        printf("MAP: skillspam %d sent\n", cnt);
                    }
                    /* OG_FLOOD=n: n packets as fast as possible (rate limit) */
                    if (GetEnvironmentVariableA("OG_FLOOD", envbuf, sizeof envbuf)) {
                        int i, cnt = atoi(envbuf);
                        unsigned char act[7] = { 0x37, 0x04, 0, 0, 0, 0, 0 };
                        memcpy(act + 2, &account, 4);
                        for (i = 0; i < cnt; i++) { if (send(m, (const char *)act, 7, 0) <= 0) break; if (i % 50 == 0) Sleep(200); }
                        printf("MAP: flood %d sent\n", i);
                    }
                    if (GetEnvironmentVariableA("OG_SPEEDTEST", envbuf, sizeof envbuf) && atoi(envbuf) > 1) speed_test(atoi(envbuf));
                    if (GetEnvironmentVariableA("OG_CODEPATCH", envbuf, sizeof envbuf)) {
                        char *e = envbuf + strlen(envbuf);
                        while (e > envbuf && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;   /* cmd "set X=v &" keeps the space */
                        code_patch(envbuf);
                    }
                    /* OG_ATTACK=1: attack the honeypot unit (only a bot would) */
                    if (GetEnvironmentVariableA("OG_ATTACK", envbuf, sizeof envbuf)) {
                        unsigned char act[7] = { 0x37, 0x04 };
                        unsigned int id = 0x7A000000u | (account & 0xFFFFFF);
                        memcpy(act + 2, &id, 4);
                        act[6] = 0;
                        send(m, (const char *)act, 7, 0);
                        printf("MAP: attacked honeypot %08x\n", id);
                    }
                }
                {
                    char hold[16];
                    /* keep reading like the game does: the guard answers challenges and pings inside recv() */
                    /* wall clock: the speed test changes GetTickCount of this process */
                    ULONGLONG end = wall_ms() + (GetEnvironmentVariableA("OG_HOLD", hold, sizeof hold) ? atoi(hold) * 1000 : 6000);
                    /* OG_WATCH="09cb:7101,01d3": count received byte patterns (packet id, optionally followed by more
                       bytes, all hex, little endian as on the wire); printed as WATCH <pattern> <count> */
                    char watch[256] = "", *w, *wctx = NULL;
                    int wcount[8] = { 0 }, nw = 0, wlen[8], c;
                    unsigned char wpat[8][16];
                    if (GetEnvironmentVariableA("OG_WATCH", watch, sizeof watch)) {
                        char copy[256];
                        snprintf(copy, sizeof copy, "%s", watch);
                        for (w = strtok_r(copy, ",", &wctx); w && nw < 8; w = strtok_r(NULL, ",", &wctx)) {
                            int k = 0;
                            char *q = w;
                            while (q[0] && q[1] && k < 16) {
                                unsigned v;
                                if (*q == ':') { q++; continue; }
                                sscanf(q, "%2x", &v);
                                wpat[nw][k++] = (unsigned char)v;
                                q += 2;
                            }
                            /* the packet id is written big endian in the pattern: swap its two bytes */
                            if (k >= 2) { unsigned char t = wpat[nw][0]; wpat[nw][0] = wpat[nw][1]; wpat[nw][1] = t; }
                            wlen[nw++] = k;
                        }
                    }
                    while (wall_ms() < end) {
                        n = recv_some(m, buf, sizeof buf, 500);
                        if (n > 0) print_messages("(hold)", buf, n);
                        for (c = 0; c < nw; c++)
                            for (int i = 0; i + wlen[c] <= n; i++)
                                if (!memcmp(buf + i, wpat[c], wlen[c])) wcount[c]++;
                    }
                    for (c = 0; c < nw; c++) printf("WATCH %d %d\n", c, wcount[c]);
                }
                closesocket(m);
                {   /* OG_CANON=<file>: write the guard's canonical image after the session (self-integrity diagnostics) */
                    char path[MAX_PATH];
                    int (*dump)(const char *) = (int (*)(const char *))GetProcAddress(GetModuleHandleA("odinguard.dll"), "OdinGuard_CanonDump");
                    if (dump && GetEnvironmentVariableA("OG_CANON", path, sizeof path)) printf("canon dump: %d\n", dump(path));
                }
            } else {
                Sleep(1500);
                closesocket(s);
            }
        }
    }
    return 0;
}
