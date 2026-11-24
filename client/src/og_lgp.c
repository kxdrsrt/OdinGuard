/* LGP colour palette (module lgp). The server draws the cells of ground skills for @lgp players with flat ground
 * units; the 2018-06-20 client has only two flat units nobody uses (0xc8/0xc9, effects 671/672) and tints both
 * blue. This patch gives the server nine colours:
 *
 *   - the unit switch (unit id - 0x7e, jump table 0xb0ceb4) sends seven unit ids that draw nothing in the
 *     vanilla client to the case of 0xc8 (effect 671): 0x89 (hidden trap) and 0x8b (hidden warp NPC), which no
 *     server sends, 0xb5 (Moonlit, not drawn by the client), 0xba (the server sends lite units with level 1:
 *     0x9dff81 turns 0xba at level 2-5 into 0xbb-0xbe), 0xdf, 0xec and 0x103. Every other id without a case is
 *     used elsewhere in the client (measured in game): 0xb0 is drawn as graffiti text (0xa52c53), 0xe5 grows
 *     plant sprites and 0x104/0x106 a large glow (actor message handler 0xb0f821, which also knows 0xd1 and
 *     0x107); traps, Pneuma and Earthquake have their own paths
 *   - the effect cases 671/672 call the flat quad helper 0x949b00(texture, 3); that call goes to lgp_tile,
 *     which looks up the unit id of the effect's owner (CGameActor at this+0x128, job getter 0x9c6a30), loads
 *     effect\odinlgp_<colour>.tga (odin.grf) and sets the primitive's tint (+0x304) to 110.0: white with normal
 *     alpha blending, so the texture keeps its colour (mode 3 sets 111.0, the fixed blue)
 *
 * Every site is checked against its bytes first; another client build leaves everything untouched. The client
 * reports OG_ALIVE_LGP_PALETTE once the patch is in, and only then the server uses the palette units (otherwise
 * the blue fallback units 0xc8/0xc9). alpha.exe is packed: the code appears after the DLLs load, so the patch
 * waits for it like the UI hooks. */
#include "og.h"
#include <string.h>

#define EXE_BASE 0x400000u

#define UNIT_DISPATCH 0x00b0b38du /* add eax,-0x7e; cmp eax,0x8a; ja; jmp [eax*4+0xb0ceb4] */
#define UNIT_TABLE    0x00b0ceb4u
#define UNIT_NONE     0x00b0cea4u /* "no effect" */
#define UNIT_CASE_C8  0x00b0b6f6u /* push 671 */
#define UNIT_CASE_C9  0x00b0b71au /* push 672 */
#define CALL_671      0x0093065bu /* call 0x949b00 after push 3; push "effect\hanmoon1.tga" */
#define CALL_672      0x0093067bu
#define FLATQUAD      0x00949b00u
#define GETJOB        0x009c6a30u

static const uint16_t g_free_units[] = { 0x89, 0x8b, 0xb5, 0xba, 0xdf, 0xec, 0x103 };

static const uint8_t sig_dispatch[] = { 0x83, 0xC0, 0x82, 0x3D, 0x8A, 0x00, 0x00, 0x00, 0x0F, 0x87, 0x09, 0x1B, 0x00, 0x00,
                                        0xFF, 0x24, 0x85, 0xB4, 0xCE, 0xB0, 0x00 };
static const uint8_t sig_case671[] = { 0x6A, 0x03, 0x68, 0x68, 0x66, 0xD3, 0x00, 0xE8, 0xA0, 0x94, 0x01, 0x00 };
static const uint8_t sig_case672[] = { 0x6A, 0x03, 0x68, 0x7C, 0x66, 0xD3, 0x00, 0xE8, 0x80, 0x94, 0x01, 0x00 };
static const uint8_t sig_flatquad[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x96, 0x28, 0x01, 0x00, 0x00 };
static const uint8_t sig_getjob[] = { 0x56, 0x8B, 0xF1, 0xFF, 0xB6, 0xB8, 0x02, 0x00, 0x00 };

/* unit id -> texture; the same table is in the server conf (og_lgp_color) and docs/07-darkworld-lgp.md */
static const struct { uint16_t unit; const char *tex; } g_palette[] = {
    { 0xc8, "effect\\odinlgp_blue.tga" },   { 0xc9, "effect\\odinlgp_cyan.tga" },
    { 0xba, "effect\\odinlgp_yellow.tga" }, { 0x8b, "effect\\odinlgp_red.tga" },
    { 0xdf, "effect\\odinlgp_orange.tga" }, { 0xec, "effect\\odinlgp_green.tga" },
    { 0x103, "effect\\odinlgp_purple.tga" }, { 0x89, "effect\\odinlgp_brown.tga" },
    { 0xb5, "effect\\odinlgp_pink.tga" },
};

typedef void (__fastcall *flatquad_t)(void *self, void *edx, const char *tex, int mode);
typedef int (__fastcall *getjob_t)(void *self, void *edx);
static flatquad_t o_flatquad;
static getjob_t o_getjob;
static volatile LONG g_active;

static uint8_t *va(uint32_t v) { return (uint8_t *)GetModuleHandleA(NULL) + (v - EXE_BASE); }

static const char *palette_texture(int unit)
{
    size_t i;
    for (i = 0; i < sizeof g_palette / sizeof *g_palette; i++)
        if (g_palette[i].unit == unit) return g_palette[i].tex;
    return NULL;
}

/* replaces "call 0x949b00" in the effect cases 671/672 (thiscall, ret 8) */
static void __fastcall lgp_tile(void *self, void *edx, const char *tex, int mode)
{
    void *owner = *(void **)((uint8_t *)self + 0x128);
    const char *mine = owner ? palette_texture(o_getjob(owner, NULL)) : NULL;
    void *prim;
    o_flatquad(self, edx, mine ? mine : tex, mode);
    if (!mine) return;
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
    size_t i;
    if (!readable(va(UNIT_DISPATCH), sizeof sig_dispatch) || memcmp(va(UNIT_DISPATCH), sig_dispatch, sizeof sig_dispatch) ||
        !readable(va(CALL_671 - 7), sizeof sig_case671) || memcmp(va(CALL_671 - 7), sig_case671, sizeof sig_case671) ||
        !readable(va(CALL_672 - 7), sizeof sig_case672) || memcmp(va(CALL_672 - 7), sig_case672, sizeof sig_case672) ||
        !readable(va(FLATQUAD), sizeof sig_flatquad) || memcmp(va(FLATQUAD), sig_flatquad, sizeof sig_flatquad) ||
        !readable(va(GETJOB), sizeof sig_getjob) || memcmp(va(GETJOB), sig_getjob, sizeof sig_getjob) ||
        !readable(va(UNIT_TABLE), 0x8b * 4))
        return 0;
    if (*(uint32_t *)va(UNIT_TABLE + (0xc8 - 0x7e) * 4) != UNIT_CASE_C8 || *(uint32_t *)va(UNIT_TABLE + (0xc9 - 0x7e) * 4) != UNIT_CASE_C9)
        return 0;
    for (i = 0; i < sizeof g_free_units / sizeof *g_free_units; i++)
        if (*(uint32_t *)va(UNIT_TABLE + (g_free_units[i] - 0x7e) * 4) != UNIT_NONE) return 0;
    return 1;
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
    for (i = 0; ok && i < sizeof g_free_units / sizeof *g_free_units; i++) {
        uint32_t to = UNIT_CASE_C8;
        ok &= write_code(va(UNIT_TABLE + (g_free_units[i] - 0x7e) * 4), &to, 4);
    }
    if (!ok) {
        og_log("lgp palette: patch failed");
        return 0;
    }
    InterlockedExchange(&g_active, 1);
    og_log("lgp palette: 9 colours active");
    return 0;
}

int og_lgp_palette_active(void) { return g_active != 0; }

void og_lgp_init(void)
{
    CloseHandle(CreateThread(NULL, 0, lgp_thread, NULL, 0, NULL));
}
