/* Colour nicks and colour items: the 2018 client draws every text with GDI (TextOutW into a DIB that becomes a texture,
 * see docs/04). The server sends name -> colour and item name -> colour lists (OG_S_COLORS); this hook changes the
 * text colour of matching strings. Outlined texts (name balloons) are handled in og_ui.c. */
#include "og.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../../third_party/minhook/include/MinHook.h"

#define BUCKETS 1024
#define MAX_TEXT 64

typedef struct entry { struct entry *next; uint8_t kind; COLORREF color; wchar_t text[MAX_TEXT]; } entry;

static entry *g_table[BUCKETS];
static SRWLOCK g_lock = SRWLOCK_INIT;
static volatile LONG g_count;
static int g_textlog;

typedef BOOL (WINAPI *textoutw_t)(HDC, int, int, LPCWSTR, int);
static textoutw_t o_textoutw;

static unsigned hash(const wchar_t *s, size_t n)
{
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) { h ^= (unsigned)s[i]; h *= 16777619u; }
    return h & (BUCKETS - 1);
}

static entry *find(const wchar_t *s, size_t n, uint8_t kind)
{
    entry *e;
    if (!n || n >= MAX_TEXT) return NULL;
    for (e = g_table[hash(s, n)]; e; e = e->next)
        if (e->kind == kind && wcsncmp(e->text, s, n) == 0 && e->text[n] == 0) return e;
    return NULL;
}

void og_colors_update(uint8_t kind, uint8_t op, const uint8_t *p, size_t n)
{
    size_t pos = 0;
    int i;
    AcquireSRWLockExclusive(&g_lock);
    if (op == OG_COLOR_OP_REPLACE) {
        for (i = 0; i < BUCKETS; i++) {
            entry **pp = &g_table[i];
            while (*pp) {
                if ((*pp)->kind == kind) { entry *d = *pp; *pp = d->next; free(d); InterlockedDecrement(&g_count); }
                else pp = &(*pp)->next;
            }
        }
    }
    while (pos + 4 <= n) {
        uint8_t len = p[pos + 3];
        wchar_t w[MAX_TEXT];
        int wl;
        COLORREF c = RGB(p[pos], p[pos + 1], p[pos + 2]);
        if (pos + 4 + len > n) break;
        wl = MultiByteToWideChar(CP_ACP, 0, (const char *)p + pos + 4, len, w, MAX_TEXT - 1);
        pos += 4 + len;
        if (wl <= 0) continue;
        w[wl] = 0;
        {
            entry *e = find(w, wl, kind);
            if (op == OG_COLOR_OP_REMOVE) {
                if (e) {
                    entry **pp = &g_table[hash(w, wl)];
                    while (*pp != e) pp = &(*pp)->next;
                    *pp = e->next;
                    free(e);
                    InterlockedDecrement(&g_count);
                }
            } else if (e) {
                e->color = c;
            } else if ((e = (entry *)calloc(1, sizeof *e)) != NULL) {
                unsigned h = hash(w, wl);
                e->kind = kind;
                e->color = c;
                wcsncpy(e->text, w, MAX_TEXT - 1);
                e->next = g_table[h];
                g_table[h] = e;
                InterlockedIncrement(&g_count);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static int is_digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

/* "+7 Blade [4]", "Jellopy: 5 ea", "Jellopy 5 ea" -> "Blade", "Jellopy" */
static void item_key(const wchar_t *s, size_t n, size_t *start, size_t *len)
{
    size_t a = 0, b = n;
    if (n > 2 && s[0] == L'+' && is_digit(s[1])) {
        a = 1;
        while (a < n && is_digit(s[a])) a++;
        while (a < n && s[a] == L' ') a++;
    }
    if (b > a + 3 && s[b - 1] == L']') {                 /* " [4]" slots */
        size_t i = b - 2;
        while (i > a && is_digit(s[i])) i--;
        if (s[i] == L'[') { b = i; while (b > a && s[b - 1] == L' ') b--; }
    }
    if (b > a + 3 && s[b - 2] == L'e' && s[b - 1] == L'a') { /* ": 5 ea" / " 5 ea" */
        size_t i = b - 2;
        while (i > a && s[i - 1] == L' ') i--;
        while (i > a && is_digit(s[i - 1])) i--;
        while (i > a && (s[i - 1] == L' ' || s[i - 1] == L':')) i--;
        b = i;
    }
    *start = a;
    *len = b - a;
}

static int lookup(const wchar_t *s, int n, COLORREF *out)
{
    entry *e;
    size_t a, l, k;
    if (n <= 0 || !g_count) return 0;
    /* names: "Name" or "Name (Party)" */
    for (k = 0; k < (size_t)n && !(s[k] == L' ' && k + 1 < (size_t)n && s[k + 1] == L'('); k++) { }
    if ((e = find(s, k, OG_COLOR_NAME)) != NULL) { *out = e->color; return 1; }
    item_key(s, n, &a, &l);
    if ((e = find(s + a, l, OG_COLOR_ITEM)) != NULL) { *out = e->color; return 1; }
    return 0;
}

/* set while UIWindow::TextOutWithOutline runs: that function already got the right colour, its outline passes
   must stay black */
static DWORD g_tls_in_outline = TLS_OUT_OF_INDEXES;

int og_colors_lookup_a(const char *text, int len, COLORREF *out)
{
    wchar_t w[MAX_TEXT];
    int wl, hit;
    if (!g_count || !text) return 0;
    if (len <= 0) len = (int)strlen(text);
    wl = MultiByteToWideChar(CP_ACP, 0, text, len < MAX_TEXT - 1 ? len : MAX_TEXT - 1, w, MAX_TEXT - 1);
    if (wl <= 0) return 0;
    AcquireSRWLockShared(&g_lock);
    hit = lookup(w, wl, out);
    ReleaseSRWLockShared(&g_lock);
    return hit;
}

void og_text_outline_scope(int enter)
{
    if (g_tls_in_outline != TLS_OUT_OF_INDEXES) TlsSetValue(g_tls_in_outline, (LPVOID)(INT_PTR)enter);
}

static BOOL WINAPI h_textoutw(HDC dc, int x, int y, LPCWSTR s, int n)
{
    COLORREF want, cur;
    BOOL r;
    int hit;
    if (g_textlog && s && n > 0 && n < 80) og_log("text %06lx '%.*ls'", (unsigned long)GetTextColor(dc), n, s);
    if (!s || n <= 0 || !g_count) return o_textoutw(dc, x, y, s, n);
    if (g_tls_in_outline != TLS_OUT_OF_INDEXES && TlsGetValue(g_tls_in_outline)) return o_textoutw(dc, x, y, s, n);
    AcquireSRWLockShared(&g_lock);
    hit = lookup(s, n, &want);
    ReleaseSRWLockShared(&g_lock);
    if (!hit) return o_textoutw(dc, x, y, s, n);
    cur = GetTextColor(dc);
    /* outline and shadow passes are drawn in black/very dark colours first; keep them */
    if (g_textlog) og_log("colour hit '%.*ls' cur %06lx want %06lx", n, s, (unsigned long)cur, (unsigned long)want);
    SetTextColor(dc, want);
    r = o_textoutw(dc, x, y, s, n);
    SetTextColor(dc, cur);
    return r;
}

int og_text_init(void)
{
    char p[MAX_PATH];
    void *target = (void *)GetProcAddress(GetModuleHandleA("gdi32.dll") ? GetModuleHandleA("gdi32.dll") : LoadLibraryA("gdi32.dll"), "TextOutW");
    g_tls_in_outline = TlsAlloc();
    snprintf(p, sizeof p, "%s\\odinguard.ini", og_client_dir);
    g_textlog = GetPrivateProfileIntA("OdinGuard", "TextLog", 0, p);
    if (!target || MH_CreateHook(target, (void *)h_textoutw, (void **)&o_textoutw) != MH_OK) return 0;
    return MH_EnableHook(target) == MH_OK;
}
