/* Test helper: brings the RO window to the front and types a line with SendInput (synthetic input).
 *   inject.exe "<text>"  -> result written to inject.out next to the exe */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static UINT g_ok, g_fail;
static DWORD g_err;

static void key(WORD vk, int up)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (SendInput(1, &in, sizeof in) == 1) g_ok++; else { g_fail++; g_err = GetLastError(); }
}

int main(int argc, char **argv)
{
    char out[MAX_PATH], *s;
    FILE *f;
    HWND w = FindWindowA(NULL, "Alpha - 20180620");
    int sent = 0;
    const char *t = argc > 1 ? argv[1] : "+heal";
    GetModuleFileNameA(NULL, out, sizeof out);
    s = strrchr(out, '\\');
    strcpy(s + 1, "inject.out");
    if (w) { SetForegroundWindow(w); Sleep(300); }
    for (; *t; t++) {
        SHORT v = VkKeyScanA(*t);
        if (v == -1) continue;
        if (v & 0x100) key(VK_SHIFT, 0);
        key(LOBYTE(v), 0); key(LOBYTE(v), 1);
        if (v & 0x100) key(VK_SHIFT, 1);
        sent++;
        Sleep(40);
    }
    key(VK_RETURN, 0); key(VK_RETURN, 1);
    f = fopen(out, "w");
    if (f) { fprintf(f, "window %p foreground %d sent %d ok %u fail %u err %lu\n", (void *)w, GetForegroundWindow() == w, sent, g_ok, g_fail, g_err); fclose(f); }
    return 0;
}
