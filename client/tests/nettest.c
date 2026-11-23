/* End-to-end test of the network layer: loads odinguard.dll (hooks ws2_32), then talks plain RO packets.
 *   nettest <ip> <login-port> <user> <password> [char-port]
 * The DLL encrypts everything transparently; the test only sees normal game packets. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                    static const char *cmds[] = { "!ping", "!vsync off", "!vsync on", "!crash" };
                    int c;
                    for (c = 0; c < 4; c++) {
                        char chat[80], found[200] = "";
                        int k, len = snprintf(chat + 4, sizeof chat - 4, "%s : %s", argc > 7 ? argv[7] : "OgTester", cmds[c]) + 5;
                        chat[0] = (char)0xF3; chat[1] = 0; chat[2] = (char)len; chat[3] = 0;
                        send(m, chat, len, 0);
                        n = recv_some(m, buf, sizeof buf, 3000);
                        for (k = 0; k + 4 < n; k++) {
                            if (buf[k] == 0x8E && buf[k + 1] == 0x00) {
                                int plen = buf[k + 2] | (buf[k + 3] << 8);
                                if (plen > 4 && k + plen <= n) { snprintf(found, sizeof found, "%.*s", plen - 4, buf + k + 4); break; }
                            }
                        }
                        printf("MAP: %-10s -> %s\n", cmds[c], found[0] ? found : "(no answer)");
                    }
                }
                Sleep(6000); /* heartbeat interval in the test config is 5 s */
                closesocket(m);
            } else {
                Sleep(1500);
                closesocket(s);
            }
        }
    }
    return 0;
}
