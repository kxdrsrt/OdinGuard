/* Network layer: hooks on ws2_32 (connect, send, recv, select, closesocket), handshake and encrypted records.
 * alpha.exe resolves send/recv with GetProcAddress and calls them through pointers; inline hooks in ws2_32
 * catch these calls without changing the pointers the client checks. */
#include "og.h"
#include "og_pubkey.h"
#include "../../third_party/monocypher.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SOCKS 16
#define HANDSHAKE_TIMEOUT_MS 10000

enum { S_FREE = 0, S_NEW, S_ACTIVE, S_FAILED };

typedef struct {
    SOCKET s;
    int state;
    CRITICAL_SECTION lock;
    uint8_t k_c2s[32], k_s2c[32];
    uint64_t ctr_c2s, ctr_s2c;
    uint8_t *in;  size_t in_len, in_cap;     /* encrypted bytes from the server */
    uint8_t *pl;  size_t pl_len, pl_cap;     /* decrypted game bytes for the client */
    uint8_t *out; size_t out_len, out_cap;   /* encrypted bytes not yet sent */
} og_sock;

static og_sock g_socks[MAX_SOCKS];
static CRITICAL_SECTION g_table;

typedef int (WSAAPI *connect_t)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *send_t)(SOCKET, const char *, int, int);
typedef int (WSAAPI *recv_t)(SOCKET, char *, int, int);
typedef int (WSAAPI *select_t)(int, fd_set *, fd_set *, fd_set *, const struct timeval *);
typedef int (WSAAPI *closesocket_t)(SOCKET);

static connect_t o_connect;
static send_t o_send;
static recv_t o_recv;
static select_t o_select;
static closesocket_t o_closesocket;
static void *g_targets[5];
static void *g_detours[5];

/* ---------- small buffers ---------- */

static int buf_append(uint8_t **b, size_t *len, size_t *cap, const void *p, size_t n)
{
    if (*len + n > *cap) {
        size_t c = *cap ? *cap : 4096;
        uint8_t *nb;
        while (c < *len + n) c *= 2;
        nb = (uint8_t *)realloc(*b, c);
        if (!nb) return 0;
        *b = nb; *cap = c;
    }
    memcpy(*b + *len, p, n);
    *len += n;
    return 1;
}

static void buf_consume(uint8_t *b, size_t *len, size_t n)
{
    if (n >= *len) { *len = 0; return; }
    memmove(b, b + n, *len - n);
    *len -= n;
}

static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void wr32(uint8_t *p, uint32_t v) { int i; for (i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void make_nonce(uint8_t nonce[24], uint64_t ctr)
{
    int i;
    memset(nonce, 0, 24);
    for (i = 0; i < 8; i++) nonce[i] = (uint8_t)(ctr >> (8 * i));
}

/* ---------- socket table ---------- */

static og_sock *sock_find(SOCKET s)
{
    int i;
    for (i = 0; i < MAX_SOCKS; i++)
        if (g_socks[i].state != S_FREE && g_socks[i].s == s) return &g_socks[i];
    return NULL;
}

static void sock_reset(og_sock *k)
{
    crypto_wipe(k->k_c2s, 32);
    crypto_wipe(k->k_s2c, 32);
    k->ctr_c2s = k->ctr_s2c = 0;
    k->in_len = k->pl_len = k->out_len = 0;
}

static void sock_track(SOCKET s)
{
    int i;
    EnterCriticalSection(&g_table);
    if (!sock_find(s)) {
        for (i = 0; i < MAX_SOCKS; i++) {
            if (g_socks[i].state != S_FREE) continue;
            EnterCriticalSection(&g_socks[i].lock);
            sock_reset(&g_socks[i]);
            g_socks[i].s = s;
            g_socks[i].state = S_NEW;
            LeaveCriticalSection(&g_socks[i].lock);
            break;
        }
    }
    LeaveCriticalSection(&g_table);
}

static void sock_untrack(SOCKET s)
{
    og_sock *k;
    EnterCriticalSection(&g_table);
    k = sock_find(s);
    if (k) {
        EnterCriticalSection(&k->lock);
        sock_reset(k);
        k->state = S_FREE;
        k->s = INVALID_SOCKET;
        LeaveCriticalSection(&k->lock);
    }
    LeaveCriticalSection(&g_table);
}

static int port_protected(unsigned short port)
{
    char list[128], *t, *ctx = NULL;
    strncpy(list, og_cfg.ports, sizeof list - 1);
    list[sizeof list - 1] = 0;
    for (t = strtok_r(list, ", ", &ctx); t; t = strtok_r(NULL, ", ", &ctx))
        if (atoi(t) == port) return 1;
    return 0;
}

/* ---------- blocking helpers for the handshake (socket may be non-blocking) ---------- */

static int wait_socket(SOCKET s, int write, DWORD deadline)
{
    for (;;) {
        fd_set set;
        struct timeval tv;
        DWORD now = GetTickCount();
        int r;
        if ((LONG)(deadline - now) <= 0) return 0;
        FD_ZERO(&set);
        FD_SET(s, &set);
        tv.tv_sec = 0; tv.tv_usec = 100000;
        r = o_select(0, write ? NULL : &set, write ? &set : NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0) return 0;
    }
}

static int send_all(SOCKET s, const uint8_t *p, size_t n, DWORD deadline)
{
    while (n) {
        int r = o_send(s, (const char *)p, (int)n, 0);
        if (r > 0) { p += r; n -= r; continue; }
        if (r < 0 && WSAGetLastError() == WSAEWOULDBLOCK) { if (!wait_socket(s, 1, deadline)) return 0; continue; }
        return 0;
    }
    return 1;
}

static int recv_exact(SOCKET s, uint8_t *p, size_t n, DWORD deadline)
{
    while (n) {
        int r = o_recv(s, (char *)p, (int)n, 0);
        if (r > 0) { p += r; n -= r; continue; }
        if (r < 0 && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAENOTCONN)) { if (!wait_socket(s, 0, deadline)) return 0; continue; }
        return 0;
    }
    return 1;
}

/* ---------- records ---------- */

static void seal(og_sock *k, uint8_t type, const uint8_t *p, size_t n)
{
    uint8_t *plain = (uint8_t *)malloc((n < OG_RECORD_MAX ? n : OG_RECORD_MAX) + 1);
    if (!plain) return;
    do {
        size_t chunk = n < OG_RECORD_MAX ? n : OG_RECORD_MAX;
        uint8_t head[18], nonce[24];
        size_t pos;
        plain[0] = type;
        if (chunk) memcpy(plain + 1, p, chunk);
        wr16(head, (uint16_t)(chunk + 1));
        make_nonce(nonce, k->ctr_c2s++);
        pos = k->out_len;
        if (!buf_append(&k->out, &k->out_len, &k->out_cap, head, 18)) break;
        if (!buf_append(&k->out, &k->out_len, &k->out_cap, plain, chunk + 1)) break;
        crypto_aead_lock(k->out + pos + 18, k->out + pos + 2, k->k_c2s, nonce, k->out + pos, 2, plain, chunk + 1);
        p += chunk;
        n -= chunk;
    } while (n);
    crypto_wipe(plain, 1);
    free(plain);
}

/* sends what fits; returns 0 on a hard socket error */
static int flush_out(og_sock *k)
{
    while (k->out_len) {
        int r = o_send(k->s, (const char *)k->out, (int)k->out_len, 0);
        if (r > 0) { buf_consume(k->out, &k->out_len, r); continue; }
        if (r < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 1;
        return 0;
    }
    return 1;
}

static void control(og_sock *k, const uint8_t *msg, size_t n)
{
    seal(k, OG_REC_CONTROL, msg, n);
    flush_out(k);
}

static void handle_control(og_sock *k, const uint8_t *p, size_t n)
{
    if (!n) return;
    switch (p[0]) {
    case OG_S_POLICY: {
        size_t pos = 9;
        int i, cnt;
        if (n < 9) return;
        EnterCriticalSection(&og_policy_lock);
        og_policy.block_vm = p[1];
        og_policy.max_windows = p[2];
        og_policy.alive_interval = rd16(p + 3);
        og_policy.input_filter = p[5];
        og_policy.grf_strict = p[6];
        cnt = rd16(p + 7);
        og_policy.nsig = 0;
        for (i = 0; i < cnt && og_policy.nsig < 256 && pos + 4 <= n; i++) {
            og_signature *s = &og_policy.sig[og_policy.nsig];
            uint8_t len = p[pos + 3];
            if (pos + 4 + len > n) break;
            s->id = rd16(p + pos);
            s->kind = p[pos + 2];
            memcpy(s->pattern, p + pos + 4, len);
            s->pattern[len] = 0;
            og_policy.nsig++;
            pos += 4 + len;
        }
        LeaveCriticalSection(&og_policy_lock);
        InterlockedExchange(&og_policy.received, 1);
        og_guard_new_connection();
        og_log("policy: vm=%d windows=%d alive=%ds input=%d grf=%d signatures=%d", og_policy.block_vm, og_policy.max_windows,
               og_policy.alive_interval, og_policy.input_filter, og_policy.grf_strict, og_policy.nsig);
        og_input_enable(og_policy.input_filter);
        break;
    }
    case OG_S_PING: {
        uint8_t m[5];
        if (n < 5) return;
        m[0] = OG_C_PONG;
        memcpy(m + 1, p + 1, 4);
        control(k, m, 5);
        break;
    }
    case OG_S_COLORS:
        if (n >= 5) og_colors_update(p[1], p[2], p + 5, n - 5);
        break;
    case OG_S_COMMAND: {
        char cmd[64], reply[256];
        uint8_t m[257];
        size_t len = n - 1 < sizeof cmd - 1 ? n - 1 : sizeof cmd - 1, rl;
        memcpy(cmd, p + 1, len);
        cmd[len] = 0;
        reply[0] = 0;
        og_cmd_handle(cmd, reply, sizeof reply);
        rl = strlen(reply);
        if (rl) {
            m[0] = OG_C_REPORT;
            memcpy(m + 1, reply, rl);
            control(k, m, rl + 1);
        }
        break;
    }
    }
}

/* decrypts complete records from k->in; returns 0 when the stream is broken */
static int open_records(og_sock *k)
{
    size_t pos = 0;
    int ok = 1;
    uint8_t *plain = NULL;
    while (k->in_len - pos >= 2) {
        const uint8_t *r = k->in + pos;
        uint16_t blen = rd16(r);
        uint8_t nonce[24];
        if (!blen) { ok = 0; break; }
        if (k->in_len - pos < (size_t)18 + blen) break;
        plain = (uint8_t *)realloc(plain, blen);
        if (!plain) { ok = 0; break; }
        make_nonce(nonce, k->ctr_s2c);
        if (crypto_aead_unlock(plain, r + 2, k->k_s2c, nonce, r, 2, r + 18, blen) != 0) {
            og_log("record from server failed authentication");
            ok = 0;
            break;
        }
        k->ctr_s2c++;
        pos += 18 + blen;
        if (plain[0] == OG_REC_GAME) {
            if (blen >= 5) og_crash_note_packet(0, rd16(plain + 1), (uint16_t)(blen - 1));
            buf_append(&k->pl, &k->pl_len, &k->pl_cap, plain + 1, blen - 1);
        } else if (plain[0] == OG_REC_CONTROL) {
            handle_control(k, plain + 1, blen - 1);
        }
    }
    free(plain);
    buf_consume(k->in, &k->in_len, pos);
    return ok;
}

/* ---------- handshake ---------- */

static int handshake(og_sock *k)
{
    uint8_t hello[OG_CLIENT_HELLO_FIXED + OG_MAX_COMPONENTS * (OG_COMPONENT_NAME + 32)];
    uint8_t e_sec[32], e_pub[32], nonce_c[32], shared[32], keys[64], transcript[64];
    uint8_t sh[OG_SERVER_HELLO_FIXED + 512];
    og_component comp[OG_MAX_COMPONENTS];
    int ncomp = 0, i;
    size_t len;
    uint16_t slen;
    DWORD deadline = GetTickCount() + HANDSHAKE_TIMEOUT_MS;
    crypto_blake2b_ctx bc;
    static const uint8_t zero[32];
    static const char label[] = "OdinGuard/v1";
    char why[64];

    if (!og_random(e_sec, 32) || !og_random(nonce_c, 32)) return 0;
    crypto_x25519_public_key(e_pub, e_sec);

    memset(hello, 0, sizeof hello);
    wr16(hello, OG_HELLO_ID);
    hello[4] = OG_PROTO_VERSION;
    hello[5] = (uint8_t)((og_is_vm(why, sizeof why) ? OG_FLAG_VM : 0) | (IsDebuggerPresent() ? OG_FLAG_DEBUGGER : 0));
    memcpy(hello + 6, e_pub, 32);
    memcpy(hello + 38, nonce_c, 32);
    og_identity(hello + 70, hello + 102, comp, &ncomp);
    strncpy((char *)hello + 134, og_cfg.build, 16);
    if (ncomp > OG_MAX_COMPONENTS) ncomp = OG_MAX_COMPONENTS;
    hello[OG_CLIENT_HELLO_FIXED - 1] = (uint8_t)ncomp;
    for (i = 0; i < ncomp; i++) {
        uint8_t *c = hello + OG_CLIENT_HELLO_FIXED + i * (OG_COMPONENT_NAME + 32);
        size_t j;
        strncpy((char *)c, comp[i].name, OG_COMPONENT_NAME);
        for (j = 0; j < 32; j++) {
            unsigned v;
            sscanf(comp[i].hex + j * 2, "%2x", &v);
            c[OG_COMPONENT_NAME + j] = (uint8_t)v;
        }
    }
    len = OG_CLIENT_HELLO_FIXED + ncomp * (OG_COMPONENT_NAME + 32);
    wr16(hello + 2, (uint16_t)len);

    if (!send_all(k->s, hello, len, deadline)) { og_log("handshake: send failed (%d)", WSAGetLastError()); goto fail; }
    if (!recv_exact(k->s, sh, 4, deadline)) { og_log("handshake: no answer (server without OdinGuard?)"); goto fail; }
    slen = rd16(sh + 2);
    if (rd16(sh) != OG_HELLO_ID || slen < OG_SERVER_HELLO_FIXED || slen > sizeof sh) { og_log("handshake: bad server hello"); goto fail; }
    if (!recv_exact(k->s, sh + 4, slen - 4, deadline)) { og_log("handshake: server hello truncated"); goto fail; }

    crypto_blake2b_init(&bc, 64);
    crypto_blake2b_update(&bc, hello, len);
    crypto_blake2b_update(&bc, sh, 70);
    crypto_blake2b_final(&bc, transcript);
    if (crypto_eddsa_check(sh + 70, og_server_pubkey, transcript, 64) != 0) {
        og_log("handshake: server signature invalid (wrong server or man in the middle)");
        goto fail;
    }
    if (sh[5] != OG_STATUS_OK) {
        char msg[513];
        size_t ml = slen - OG_SERVER_HELLO_FIXED;
        memcpy(msg, sh + OG_SERVER_HELLO_FIXED, ml);
        msg[ml] = 0;
        og_log("handshake: refused (%d) %s", sh[5], msg);
        og_message(ml ? msg : "The server refused the connection.", MB_OK | MB_ICONWARNING);
        goto fail;
    }

    crypto_x25519(shared, e_sec, sh + 6);
    if (crypto_verify32(shared, zero) == 0) goto fail;
    crypto_blake2b_init(&bc, 64);
    crypto_blake2b_update(&bc, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&bc, shared, 32);
    crypto_blake2b_update(&bc, e_pub, 32);
    crypto_blake2b_update(&bc, sh + 6, 32);
    crypto_blake2b_update(&bc, nonce_c, 32);
    crypto_blake2b_update(&bc, sh + 38, 32);
    crypto_blake2b_final(&bc, keys);
    memcpy(k->k_c2s, keys, 32);
    memcpy(k->k_s2c, keys + 32, 32);
    crypto_wipe(keys, 64);
    crypto_wipe(shared, 32);
    crypto_wipe(e_sec, 32);
    k->state = S_ACTIVE;
    og_log("handshake ok on socket %u", (unsigned)k->s);
    return 1;
fail:
    crypto_wipe(e_sec, 32);
    k->state = S_FAILED;
    return 0;
}

/* ---------- hooks ---------- */

static int WSAAPI h_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    int r = o_connect(s, name, namelen);
    int err = WSAGetLastError();
    if (name && namelen >= (int)sizeof(struct sockaddr_in) && name->sa_family == AF_INET &&
        (r == 0 || err == WSAEWOULDBLOCK) && port_protected(ntohs(((const struct sockaddr_in *)name)->sin_port)))
        sock_track(s);
    WSASetLastError(err);
    return r;
}

static int WSAAPI h_send(SOCKET s, const char *buf, int len, int flags)
{
    og_sock *k = sock_find(s);
    int ret = len;
    if (!k) return o_send(s, buf, len, flags);
    EnterCriticalSection(&k->lock);
    if (k->state == S_NEW) handshake(k);
    if (k->state != S_ACTIVE) {
        LeaveCriticalSection(&k->lock);
        WSASetLastError(WSAECONNRESET);
        return SOCKET_ERROR;
    }
    if (len >= 2) og_crash_note_packet(1, rd16((const uint8_t *)buf), (uint16_t)len);
    seal(k, OG_REC_GAME, (const uint8_t *)buf, len);
    if (!flush_out(k)) { ret = SOCKET_ERROR; k->state = S_FAILED; }
    LeaveCriticalSection(&k->lock);
    if (ret == SOCKET_ERROR) WSASetLastError(WSAECONNRESET);
    return ret;
}

static int WSAAPI h_recv(SOCKET s, char *buf, int len, int flags)
{
    og_sock *k = sock_find(s);
    int n, err = 0, closed = 0;
    if (!k || k->state == S_NEW) return o_recv(s, buf, len, flags);
    EnterCriticalSection(&k->lock);
    if (k->state != S_ACTIVE) {
        LeaveCriticalSection(&k->lock);
        WSASetLastError(WSAECONNRESET);
        return SOCKET_ERROR;
    }
    flush_out(k);
    if (!k->pl_len) {
        char tmp[16384];
        int r = o_recv(s, tmp, sizeof tmp, 0);
        if (r > 0) {
            buf_append(&k->in, &k->in_len, &k->in_cap, tmp, r);
            if (!open_records(k)) { k->state = S_FAILED; err = WSAECONNRESET; }
        } else if (r == 0) closed = 1;
        else err = WSAGetLastError();
    }
    if (k->pl_len) {
        n = (int)(k->pl_len < (size_t)len ? k->pl_len : (size_t)len);
        memcpy(buf, k->pl, n);
        buf_consume(k->pl, &k->pl_len, n);
        LeaveCriticalSection(&k->lock);
        return n;
    }
    LeaveCriticalSection(&k->lock);
    if (closed) return 0;
    WSASetLastError(err ? err : WSAEWOULDBLOCK);
    return SOCKET_ERROR;
}

/* sockets with decrypted data waiting must count as readable, the client checks with select() first */
static int WSAAPI h_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, const struct timeval *tv)
{
    SOCKET ready[MAX_SOCKS];
    int nready = 0, i, r;
    if (rd) {
        for (i = 0; i < (int)rd->fd_count; i++) {
            og_sock *k = sock_find(rd->fd_array[i]);
            if (k && k->state == S_ACTIVE && k->pl_len && nready < MAX_SOCKS) ready[nready++] = rd->fd_array[i];
        }
    }
    if (!nready) return o_select(nfds, rd, wr, ex, tv);
    {
        struct timeval zero = { 0, 0 };
        r = o_select(nfds, rd, wr, ex, &zero);
        if (r < 0) {
            r = 0;
            if (rd) FD_ZERO(rd);
            if (wr) FD_ZERO(wr);
            if (ex) FD_ZERO(ex);
        }
        for (i = 0; i < nready; i++) {
            if (!FD_ISSET(ready[i], rd)) { FD_SET(ready[i], rd); r++; }
        }
        return r;
    }
}

static int WSAAPI h_closesocket(SOCKET s)
{
    sock_untrack(s);
    return o_closesocket(s);
}

/* ---------- public ---------- */

void og_net_broadcast_control(const uint8_t *msg, size_t n)
{
    int i;
    for (i = 0; i < MAX_SOCKS; i++) {
        og_sock *k = &g_socks[i];
        if (k->state != S_ACTIVE) continue;
        EnterCriticalSection(&k->lock);
        if (k->state == S_ACTIVE) control(k, msg, n);
        LeaveCriticalSection(&k->lock);
    }
}

int og_net_active_connections(void)
{
    int i, c = 0;
    for (i = 0; i < MAX_SOCKS; i++) if (g_socks[i].state == S_ACTIVE) c++;
    return c;
}

/* every hooked function must still start with our jump (MinHook writes E9 rel32 to the detour or a relay) */
int og_net_hooks_intact(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        const uint8_t *p = (const uint8_t *)g_targets[i];
        if (!p) continue;
        if (p[0] != 0xE9) return 0;
    }
    return 1;
}

int og_net_init(void)
{
    HMODULE ws = GetModuleHandleA("ws2_32.dll");
    int i;
    static const char *names[5] = { "connect", "send", "recv", "select", "closesocket" };
    void *det[5] = { (void *)h_connect, (void *)h_send, (void *)h_recv, (void *)h_select, (void *)h_closesocket };
    void **orig[5] = { (void **)&o_connect, (void **)&o_send, (void **)&o_recv, (void **)&o_select, (void **)&o_closesocket };

    InitializeCriticalSection(&g_table);
    for (i = 0; i < MAX_SOCKS; i++) { InitializeCriticalSection(&g_socks[i].lock); g_socks[i].s = INVALID_SOCKET; }
    if (!ws) ws = LoadLibraryA("ws2_32.dll");
    if (!ws) return 0;
    if (MH_Initialize() != MH_OK) return 0;
    for (i = 0; i < 5; i++) {
        g_targets[i] = (void *)GetProcAddress(ws, names[i]);
        g_detours[i] = det[i];
        if (!g_targets[i] || MH_CreateHook(g_targets[i], det[i], orig[i]) != MH_OK) {
            og_log("hook %s failed", names[i]);
            return 0;
        }
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return 0;
    og_log("network hooks installed (ports %s)", og_cfg.ports);
    return 1;
}
