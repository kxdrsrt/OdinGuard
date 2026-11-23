#include <string.h>
#include <windows.h>

/* Proxy for ijl15.dll. The six JPEG exports are forwarded (see ijl15_loader.def) to ijl15r.dll,
   which is the previous ijl15.dll (ROExt proxy) renamed; it forwards to ijl15x.dll itself.
   The only job of this file is to bring odinguard.dll into the process. */
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH], *s;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, path, sizeof path);
        s = strrchr(path, '\\');
        if (s) {
            lstrcpyA(s + 1, "odinguard.dll");
            LoadLibraryA(path);
        }
    }
    return TRUE;
}
