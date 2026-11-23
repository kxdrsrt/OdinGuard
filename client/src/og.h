#ifndef OG_H
#define OG_H

#include <winsock2.h>
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#include "../../proto/og_proto.h"

#define OG_ABI_VERSION 1
#define OG_VERSION_STR "0.1.0"

#ifdef OG_BUILD
#define OG_API __declspec(dllexport)
#else
#define OG_API __declspec(dllimport)
#endif

/* sha256 */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } og_sha256_ctx;
void og_sha256_init(og_sha256_ctx *c);
void og_sha256_update(og_sha256_ctx *c, const void *d, size_t n);
void og_sha256_final(og_sha256_ctx *c, uint8_t out[32]);
void og_hex(const uint8_t *in, size_t n, char *out); /* out: 2n+1 bytes */
int  og_sha256_file(const char *path, char hex[65]);

/* log */
void og_log_open(const char *dir);
void og_log(const char *fmt, ...);

/* integrity */
int  og_hash_module_code(HMODULE m, char hex[65]);          /* non-writable executable sections */
int  og_scan_files(const char *dir, char *out, size_t cap);   /* "name=sha256\n" for exe + dll */
int  og_scan_modules(char *out, size_t cap, int *unknown);    /* loaded modules, flags those outside client/system dirs */

/* hwid */
typedef struct { char name[24]; char hex[65]; } og_component;
int  og_hwid_collect(og_component *c, int max, const char *salt);
void og_hwid_unique(const og_component *c, int n, char hex[65]);

/* environment */
int  og_is_vm(char *why, size_t cap);
int  og_window_slot_acquire(int max_windows);

/* crash */
void og_crash_install(void);
void og_crash_write(EXCEPTION_POINTERS *ep, const char *reason);

void og_crash_note_packet(int outbound, uint16_t id, uint16_t len);
const char *og_crash_last_file(void);

/* config (odinguard.ini next to the exe; the server policy overrides the protection switches) */
typedef struct {
    int max_windows;      /* 0 = unlimited */
    int block_vm;
    int log_only;
    char salt[64];        /* fixed per server; not read from the ini, a changed salt would evade hardware bans */
    char build[16];
    char ports[128];      /* game server ports that are protected */
} og_config;
extern og_config og_cfg;
void og_config_load(const char *dir);

extern char og_client_dir[MAX_PATH];
extern DWORD og_main_thread;

/* server policy, received after the handshake */
typedef struct { uint16_t id; uint8_t kind; char pattern[256]; } og_signature;
typedef struct {
    volatile LONG received;
    int block_vm, max_windows, alive_interval, input_filter, grf_strict;
    int nsig;
    og_signature sig[256];
} og_policy_t;
extern og_policy_t og_policy;
extern CRITICAL_SECTION og_policy_lock;

/* net */
int  og_net_init(void);
int  og_net_hooks_intact(void);
void og_net_broadcast_control(const uint8_t *msg, size_t n);   /* to every protected connection */
int  og_net_active_connections(void);

/* guard thread, commands, input filter, assets */
void og_guard_start(void);
void og_guard_scan_now(void);
void og_guard_new_connection(void);
void og_cmd_handle(const char *cmd, char *reply, size_t cap);
int  og_cmds_init(void);
void og_input_enable(int on);
int  og_assets_check(char *detail, size_t cap);   /* 1 = violation */

/* identity used by the hello */
void og_identity(uint8_t uid[32], uint8_t exe_code[32], og_component *comp, int *ncomp);
int  og_random(void *p, size_t n);

#endif
