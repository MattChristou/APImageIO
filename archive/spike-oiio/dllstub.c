/* Stub loader: LoadLibrary the OIIO test DLL and call runtest(). Mirrors how
   LightWave loads our .p plugin (a /MT static DLL). */
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 3) { printf("usage: dllstub <dll> <imagefile>\n"); return 2; }
    fprintf(stderr, "[stub] LoadLibrary %s\n", argv[1]); fflush(stderr);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "[stub] LoadLibrary failed %lu\n", GetLastError()); return 3; }
    typedef int (*runtest_t)(const char*);
    runtest_t fn = (runtest_t)GetProcAddress(h, "runtest");
    if (!fn) { fprintf(stderr, "[stub] GetProcAddress failed\n"); return 4; }
    int r = fn(argv[2]);
    fprintf(stderr, "[stub] runtest returned %d\n", r); fflush(stderr);
    FreeLibrary(h);
    fprintf(stderr, "[stub] done\n"); fflush(stderr);
    return 0;
}
