#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*report_fn)(char *, size_t);

/* Loads odinguard.dll from the current directory and prints its report. */
int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA("odinguard.dll");
    report_fn rep;
    char *buf;
    int n;
        if (!m) { printf("load failed: %lu\n", GetLastError()); return 1; }
    rep = (report_fn)GetProcAddress(m, "OdinGuard_Report");
    buf = malloc(1 << 20);
    n = rep(buf, 1 << 20);
    fwrite(buf, 1, n, stdout);
    if (argc > 2 && !strcmp(argv[1], "canon")) {
        int (*dump)(const char *) = (int (*)(const char *))GetProcAddress(m, "OdinGuard_CanonDump");
        printf("canon dump %s: %d\n", argv[2], dump ? dump(argv[2]) : -1);
    }
    if (argc > 1 && !strcmp(argv[1], "crash")) ((void (*)(void))GetProcAddress(m, "OdinGuard_CrashNow"))();
    Sleep(500);
    if (argc > 1 && !strcmp(argv[1], "hold")) Sleep(6000);
    return 0;
}
