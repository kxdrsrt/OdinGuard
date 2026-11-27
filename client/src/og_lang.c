/* Languages of the texts players see (message boxes, the window title, replies to ! commands). Built in: English and
 * German. The language is the Windows display language, or [OdinGuard] Language=de|en|... in odinguard.ini. A server can
 * add a language or change single texts in odinguard.ini (shipped by its patcher), UTF-8:
 *     [text.fr]
 *     dark.usage=Utilisation : !dark on | !dark off
 * Placeholders are {1} and {2}; no printf formats come from the file. The language also travels to the server in the
 * hello (og-lang), so the server's messages follow it. */
#include "og.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *key, *en, *de; } og_text_def;

/* German without umlauts where the text ends up in an ANSI window title */
static const og_text_def g_texts[OG_T_COUNT] = {
    [OG_T_ON] = { "on", "ON", "AN" },
    [OG_T_OFF] = { "off", "OFF", "AUS" },
    [OG_T_DARK_STATUS] = { "dark.status", "Competitive pack is {1} for the next start. !dark on / !dark off changes it (restart the game).",
                           "Competitive-Pack ist beim nächsten Start {1}. !dark on / !dark off ändert das (danach das Spiel neu starten)." },
    [OG_T_DARK_MISSING] = { "dark.missing", "odin-dark.grf is not installed: run the patcher first.",
                            "odin-dark.grf ist nicht installiert: bitte zuerst den Patcher starten." },
    [OG_T_DARK_ALREADY_ON] = { "dark.already_on", "Competitive pack is already ON. Restart the game if you just changed it.",
                               "Competitive-Pack ist schon AN. Starte das Spiel neu, falls du es gerade geändert hast." },
    [OG_T_DARK_ALREADY_OFF] = { "dark.already_off", "Competitive pack is already OFF.", "Competitive-Pack ist schon AUS." },
    [OG_T_DARK_USAGE] = { "dark.usage", "Usage: !dark on | !dark off", "Benutzung: !dark on | !dark off" },
    [OG_T_DARK_CHANGED] = { "dark.changed", "Competitive pack {1}. Restart the game to apply it (some servers require it on battleground / WoE maps).",
                            "Competitive-Pack {1}. Starte das Spiel neu, damit es wirkt (manche Server verlangen es auf Battleground-/WoE-Karten)." },
    [OG_T_VSYNC_OFF] = { "vsync.off", "FPS limit off (frame cap removed). !vsync on restores it.",
                         "FPS-Limit aus (Bildbegrenzung entfernt). !vsync on stellt sie wieder her." },
    [OG_T_VSYNC_ON] = { "vsync.on", "FPS limit on (default frame cap). {1} frame waits were skipped.",
                        "FPS-Limit an (normale Bildbegrenzung). {1} Wartezeiten wurden übersprungen." },
    [OG_T_CRASH_WRITTEN] = { "crash.written", "Diagnostic report written: odinguard-crash\\{1}",
                             "Diagnosebericht geschrieben: odinguard-crash\\{1}" },
    [OG_T_CRASH_TITLE] = { "crash.title", "OdinGuard: crash reports", "OdinGuard: Absturzberichte" },
    [OG_T_CRASH_ASK] = { "crash.ask",
                         "OdinGuard found {1} crash report(s) of this game.\n\nSend them to the server team to help fix the crash?\n{2}\n\n"
                         "A report holds the error address, the loaded modules with their file paths, DATA.INI and the headers of the last "
                         "network packets. No passwords, no chat.\n\nYour answer is remembered (odinguard.ini, [crash] upload).",
                         "OdinGuard hat {1} Absturzbericht(e) dieses Spiels gefunden.\n\nAn das Serverteam schicken, damit der Absturz behoben "
                         "werden kann?\n{2}\n\nEin Bericht enthält die Fehleradresse, die geladenen Module mit ihren Dateipfaden, DATA.INI und "
                         "die Kopfdaten der letzten Netzwerkpakete. Keine Passwörter, kein Chat.\n\nDeine Antwort wird gespeichert "
                         "(odinguard.ini, [crash] upload)." },
    [OG_T_MAX_WINDOWS] = { "max_windows", "The maximum number of game windows on this computer is reached.",
                           "Die höchste Zahl an Spielfenstern auf diesem Computer ist erreicht." },
    [OG_T_VM] = { "vm", "Running in a virtual machine is not allowed on this server.",
                  "Auf diesem Server darf das Spiel nicht in einer virtuellen Maschine laufen." },
    [OG_T_REFUSED] = { "refused", "The server refused the connection.", "Der Server hat die Verbindung abgelehnt." },
    [OG_T_TITLE_WAITING] = { "title.waiting", " - waiting for the server", " - wartet auf den Server" },
    [OG_T_TITLE_FAILED] = { "title.failed", " - NOT CONNECTED ({1})", " - NICHT VERBUNDEN ({1})" },
    [OG_T_TITLE_DARK] = { "title.dark", " | Darkworld / Competitive Mode ON", " | Darkworld / Competitive-Modus AN" },
    [OG_T_LAUNCHER] = { "launcher", OG_PROFILE_LAUNCHER_MSG, OG_PROFILE_LAUNCHER_MSG_DE },
    [OG_T_LANG_STATUS] = { "lang.status", "Language: {1} (set: {2}). !lang de | en | auto changes it.",
                           "Sprache: {1} (eingestellt: {2}). !lang de | en | auto ändert sie." },
    [OG_T_LANG_SET] = { "lang.set", "Language set to {1}.", "Sprache auf {1} gestellt." },
    [OG_T_LANG_UNKNOWN] = { "lang.unknown", "Unknown language {1}. Available: {2}, auto.", "Unbekannte Sprache {1}. Verfügbar: {2}, auto." },
    [OG_T_LANG_AUTO] = { "lang.auto", "auto (Windows)", "auto (Windows)" },
};

static char g_lang[9] = "en";
static char *g_custom[OG_T_COUNT];

static void lower_code(char *s)
{
    char *w = s;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || c == '-') *w++ = c;
    }
    *w = 0;
}

static int g_changed;

/* [OdinGuard] Language= (default "auto"); the part after "text." names the language sections */
static void lang_pref(char *out, size_t cap)
{
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    GetPrivateProfileStringA("OdinGuard", "Language", "auto", out, (DWORD)cap, ini);
    lower_code(out);
    if (!out[0]) snprintf(out, cap, "auto");
}

static void lang_load(const char *pref)
{
    char ini[MAX_PATH], want[16], sect[32], val[1024];
    int i;
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    snprintf(want, sizeof want, "%s", pref);
    if (!strcmp(want, "auto")) {
        want[0] = 0;
        GetLocaleInfoA(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), LOCALE_SISO639LANGNAME, want, sizeof want);
        lower_code(want);
    }
    snprintf(g_lang, sizeof g_lang, "%s", want[0] && strlen(want) < sizeof g_lang ? want : "en");
    for (i = 0; i < OG_T_COUNT; i++) { free(g_custom[i]); g_custom[i] = NULL; }
    snprintf(sect, sizeof sect, "text.%s", g_lang);
    for (i = 0; i < OG_T_COUNT; i++) {
        if (!g_texts[i].key) continue;
        val[0] = 0;
        GetPrivateProfileStringA(sect, g_texts[i].key, "", val, sizeof val, ini);
        if (val[0]) {
            /* "\n" in the file becomes a line break */
            char *r = val, *w = val;
            for (; *r; r++) {
                if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
                else *w++ = *r;
            }
            *w = 0;
            g_custom[i] = _strdup(val);
        }
    }
}

void og_lang_init(const char *dir)
{
    char pref[16];
    (void)dir;
    lang_pref(pref, sizeof pref);
    lang_load(pref);
}

/* a language the player can choose: built in, or with a [text.<code>] section in odinguard.ini */
static int lang_known(const char *code)
{
    char ini[MAX_PATH], sec[32], buf[8];
    if (!strcmp(code, "de") || !strcmp(code, "en")) return 1;
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    snprintf(sec, sizeof sec, "text.%s", code);
    return GetPrivateProfileSectionA(sec, buf, sizeof buf, ini) > 0;
}

int og_lang_set(const char *code)
{
    char c[16], ini[MAX_PATH];
    snprintf(c, sizeof c, "%s", code);
    lower_code(c);
    if (!c[0] || strlen(c) > 8) return 0;
    if (strcmp(c, "auto") && !lang_known(c)) return 0;
    snprintf(ini, sizeof ini, "%s\\odinguard.ini", og_client_dir);
    WritePrivateProfileStringA("OdinGuard", "Language", c, ini);
    lang_load(c);
    g_changed = 1;
    return 1;
}

int og_lang_take_changed(void)
{
    int c = g_changed;
    g_changed = 0;
    return c;
}

/* "!lang": status; "!lang de|en|auto|<code>": change */
void og_lang_command(const char *arg, char *reply, size_t cap)
{
    char pref[16];
    while (*arg == ' ') arg++;
    if (!*arg) {
        lang_pref(pref, sizeof pref);
        og_trf(reply, cap, OG_T_LANG_STATUS, g_lang, strcmp(pref, "auto") ? pref : og_tr(OG_T_LANG_AUTO));
        return;
    }
    if (og_lang_set(arg)) {
        lang_pref(pref, sizeof pref);
        og_trf(reply, cap, OG_T_LANG_SET, strcmp(pref, "auto") ? g_lang : og_tr(OG_T_LANG_AUTO), NULL);
    } else {
        char shown[16];
        snprintf(shown, sizeof shown, "%s", arg);
        lower_code(shown);
        og_trf(reply, cap, OG_T_LANG_UNKNOWN, shown, "de, en");
    }
}

const char *og_lang(void) { return g_lang; }

const char *og_tr(int id)
{
    if (id < 0 || id >= OG_T_COUNT || !g_texts[id].en) return "";
    if (g_custom[id]) return g_custom[id];
    if (!strcmp(g_lang, "de") && g_texts[id].de && g_texts[id].de[0]) return g_texts[id].de;
    return g_texts[id].en;
}

/* og_tr with {1} and {2} replaced by a1 and a2 */
void og_trf(char *out, size_t cap, int id, const char *a1, const char *a2)
{
    const char *s = og_tr(id);
    size_t n = 0;
    if (!cap) return;
    while (*s && n + 1 < cap) {
        if (s[0] == '{' && (s[1] == '1' || s[1] == '2') && s[2] == '}') {
            const char *a = s[1] == '1' ? a1 : a2;
            for (; a && *a && n + 1 < cap; a++) out[n++] = *a;
            s += 3;
        } else out[n++] = *s++;
    }
    out[n] = 0;
}
