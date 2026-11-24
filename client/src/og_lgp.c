/* LGP colours and patterns (module lgp). The server draws the cells of ground skills for @lgp players with the
 * two flat ground units of the 2018-06-20 client (0xc8/0xc9, effects 671/672), which the client tints blue.
 * This patch lets the server choose the colour and the shape per skill:
 *
 *   - the server puts a marker into the "creator id" field of the unit packet (0x7C000000 | symbol << 16 | shape << 8 | colour);
 *     the client keeps that id in the unit object at +0x3d0 (measured in game)
 *   - the effect cases 671/672 call the flat quad helper 0x949b00(texture, 3); that call goes to lgp_tile, which
 *     reads the marker of the effect's owner (checked: owner is unit 0xc8/0xc9 by the job getter 0x9c6a30),
 *     loads effect\odinlgp_<colour>[_t][_<symbol>].tga (odin.grf; _t = triangle for hostile skills) and sets the primitive's
 *     tint (+0x304) to 110.0: white with normal alpha blending, so the texture keeps its colour (mode 3 sets
 *     111.0, the fixed blue)
 *
 * Every site is checked against its bytes first; another client build leaves everything untouched. The client
 * announces the capability (hello flag and heartbeat flag OG_ALIVE_LGP_PALETTE) and only then the server sends
 * markers; otherwise it sends the plain units (0xc8 square = protective, 0xc9 triangle = hostile, blue).
 * alpha.exe is packed: the code appears after the DLLs load, so the patch waits for it like the UI hooks. */
#include "og.h"
#include <stdio.h>
#include <string.h>

#define EXE_BASE 0x400000u

#define CALL_671      0x0093065bu /* call 0x949b00 after push 3; push "effect\hanmoon1.tga" */
#define CALL_672      0x0093067bu
#define FLATQUAD      0x00949b00u
#define GETJOB        0x009c6a30u
#define OWNER_OFF     0x128       /* effect -> unit object */
#define CREATOR_OFF   0x3d0       /* unit object: creator id as sent by the server */
#define MARKER_MASK   0xFF000000u
#define MARKER_TAG    0x7C000000u

static const uint8_t sig_case671[] = { 0x6A, 0x03, 0x68, 0x68, 0x66, 0xD3, 0x00, 0xE8, 0xA0, 0x94, 0x01, 0x00 };
static const uint8_t sig_case672[] = { 0x6A, 0x03, 0x68, 0x7C, 0x66, 0xD3, 0x00, 0xE8, 0x80, 0x94, 0x01, 0x00 };
static const uint8_t sig_flatquad[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x96, 0x28, 0x01, 0x00, 0x00 };
static const uint8_t sig_getjob[] = { 0x56, 0x8B, 0xF1, 0xFF, 0xB6, 0xB8, 0x02, 0x00, 0x00 };

/* colour index of the marker (low byte); the same list is in the server conf (og_lgp_color) */
static const char *const g_colors[] = { "blue", "cyan", "yellow", "red", "orange", "green", "purple", "brown", "pink", "white", "gold", "grey", "lightgrey", "lime" };

/* symbol number (marker bits 16..23, 0 = none) -> tile name suffix; same list as SYMBOLS in make_lgp_tiles.py */
static const char *const g_symbols[] = { "plus", "minus", "shield", "note", "dnote", "apple", "heart", "star", "cross", "zzz", "coin", "swirl", "drum",
                                         "ring", "noentry", "diamond", "bolt", "exclaim", "arrowdown", "moon", "sun", "flame", "wand" };

typedef void (__fastcall *flatquad_t)(void *self, void *edx, const char *tex, int mode);
typedef int (__fastcall *getjob_t)(void *self, void *edx);
static flatquad_t o_flatquad;
static getjob_t o_getjob;
static volatile LONG g_active;

static uint8_t *va(uint32_t v) { return (uint8_t *)GetModuleHandleA(NULL) + (v - EXE_BASE); }

/* replaces "call 0x949b00" in the effect cases 671/672 (thiscall, ret 8) */
static void __fastcall lgp_tile(void *self, void *edx, const char *tex, int mode)
{
    static char name[80];
    void *owner = *(void **)((uint8_t *)self + OWNER_OFF);
    const char *use = tex;
    int tinted = 0;
    void *prim;
    if (owner) {
        int job = o_getjob(owner, NULL);
        uint32_t marker = *(uint32_t *)((uint8_t *)owner + CREATOR_OFF);
        if ((job == 0xc8 || job == 0xc9) && (marker & MARKER_MASK) == MARKER_TAG && (marker & 0xFF) < sizeof g_colors / sizeof *g_colors) {
            uint32_t sym = (marker >> 16) & 0xFF;
            const char *symname = sym >= 1 && sym <= sizeof g_symbols / sizeof *g_symbols ? g_symbols[sym - 1] : NULL;
            snprintf(name, sizeof name, "effect\\odinlgp_%s%s%s%s.tga", g_colors[marker & 0xFF], (marker & 0x100) ? "_t" : "", symname ? "_" : "", symname ? symname : "");
            use = name;
            tinted = 1;
        }
    }
    o_flatquad(self, edx, use, mode);
    if (!tinted) return;
    prim = *(void **)((uint8_t *)self + 0x11c74);
    if (prim) *(float *)((uint8_t *)prim + 0x304) = 110.0f;
}

static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (const uint8_t *)p + n <= (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;
}

static int write_code(uint8_t *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return 1;
}

static int sites_match(void)
{
    return readable(va(CALL_671 - 7), sizeof sig_case671) && !memcmp(va(CALL_671 - 7), sig_case671, sizeof sig_case671) &&
           readable(va(CALL_672 - 7), sizeof sig_case672) && !memcmp(va(CALL_672 - 7), sig_case672, sizeof sig_case672) &&
           readable(va(FLATQUAD), sizeof sig_flatquad) && !memcmp(va(FLATQUAD), sig_flatquad, sizeof sig_flatquad) &&
           readable(va(GETJOB), sizeof sig_getjob) && !memcmp(va(GETJOB), sig_getjob, sizeof sig_getjob);
}

static DWORD WINAPI lgp_thread(LPVOID unused)
{
    int waited, ok = 1;
    size_t i;
    (void)unused;
    for (waited = 0; waited < 600 && !sites_match(); waited++) Sleep(100);
    if (waited == 600) {
        og_log("lgp palette: client code not recognised (other build), blue tiles only");
        return 0;
    }
    o_flatquad = (flatquad_t)(void *)va(FLATQUAD);
    o_getjob = (getjob_t)(void *)va(GETJOB);
    for (i = 0; i < 2; i++) {
        uint8_t *call = va(i ? CALL_672 : CALL_671);
        uint8_t rel[4];
        int32_t d = (int32_t)((uintptr_t)lgp_tile - ((uintptr_t)call + 5));
        memcpy(rel, &d, 4);
        ok &= write_code(call + 1, rel, 4);
    }
    if (!ok) {
        og_log("lgp palette: patch failed");
        return 0;
    }
    InterlockedExchange(&g_active, 1);
    og_log("lgp palette: %d colours, square and triangle active", (int)(sizeof g_colors / sizeof *g_colors));
    return 0;
}

int og_lgp_palette_active(void) { return g_active != 0; }

void og_lgp_init(void)
{
    CloseHandle(CreateThread(NULL, 0, lgp_thread, NULL, 0, NULL));
}
