/* Resource rules: game files must come from the shipped GRFs. Loose files in data\ and extra GRFs can replace
 * sprites, maps or lua tables (no-delay ACTs, transparent walls, ...). Allowed overlays for custom visual features
 * (LGP ground textures, auras, colour tables) live in data\odinguard\ and data\texture\effect\lgp\. */
#include "og.h"
#include <stdio.h>
#include <string.h>

static const char *g_allowed_grf[] = { "cdata.grf", "core.grf", "cpal.grf", "data.grf", "rdata.grf" };
static const char *g_overlay[] = { "data\\odinguard\\", "data\\texture\\effect\\lgp\\", "data\\texture\\effect\\aura\\" };
static const char *g_protected_ext[] = { ".spr", ".act", ".str", ".gat", ".gnd", ".rsw", ".rsm", ".lub", ".lua",
                                         ".imf", ".pal", ".bmp", ".tga", ".txt", ".xml", ".grf" };

static int ends_with_i(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && _stricmp(s + a - b, suf) == 0;
}

static int is_overlay(const char *rel)
{
    size_t i;
    for (i = 0; i < sizeof g_overlay / sizeof *g_overlay; i++)
        if (_strnicmp(rel, g_overlay[i], strlen(g_overlay[i])) == 0) return 1;
    return 0;
}

static int walk(const char *root, const char *rel, char *detail, size_t cap, int depth)
{
    char mask[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    if (depth > 12) return 0;
    snprintf(mask, sizeof mask, "%s\\%s*", root, rel);
    h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char sub[MAX_PATH];
        size_t i;
        if (fd.cFileName[0] == '.') continue;
        snprintf(sub, sizeof sub, "%s%s", rel, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            strncat(sub, "\\", sizeof sub - strlen(sub) - 1);
            if (!is_overlay(sub)) found = walk(root, sub, detail, cap, depth + 1);
        } else if (!is_overlay(sub)) {
            for (i = 0; i < sizeof g_protected_ext / sizeof *g_protected_ext; i++) {
                if (ends_with_i(fd.cFileName, g_protected_ext[i])) {
                    snprintf(detail, cap, "loose file %s", sub);
                    found = 1;
                    break;
                }
            }
        }
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

int og_assets_check(char *detail, size_t cap)
{
    char ini[MAX_PATH], key[8], val[MAX_PATH];
    int i;
    size_t j;

    snprintf(ini, sizeof ini, "%s\\DATA.INI", og_client_dir);
    for (i = 0; i < 32; i++) {
        int ok = 0;
        snprintf(key, sizeof key, "%d", i);
        GetPrivateProfileStringA("Data", key, "", val, sizeof val, ini);
        if (!val[0]) continue;
        for (j = 0; j < sizeof g_allowed_grf / sizeof *g_allowed_grf; j++)
            if (!_stricmp(val, g_allowed_grf[j])) ok = 1;
        if (!ok) {
            snprintf(detail, cap, "unknown GRF %s in DATA.INI", val);
            return 1;
        }
    }
    return walk(og_client_dir, "data\\", detail, cap, 0);
}
