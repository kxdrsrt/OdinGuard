#ifndef OG_H
#define OG_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

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

/* config (odinguard.ini next to the exe) */
typedef struct {
    int max_windows;      /* 0 = unlimited */
    int block_vm;
    int log_only;
    char salt[64];
} og_config;
extern og_config og_cfg;
void og_config_load(const char *dir);

extern char og_client_dir[MAX_PATH];

#endif
