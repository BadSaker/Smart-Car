
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(const char *p, const char *v) {
    FILE *f = fopen(p, "wb");
    if (!f) exit(91);
    fputs(v, f); fclose(f);
}
int main(int argc, char **argv) {
    int i;
    if (argc > 1 && strcmp(argv[1], "--bin") == 0) {
        if (getenv("FAKE_EXPORT_FAILURE")) return 42;
        for (i = 1; i + 1 < argc; ++i)
            if (strcmp(argv[i], "--output") == 0) put(argv[i+1], "new-bin");
        return 0;
    }
    for (i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "-o") == 0)
            put(argv[i+1], "autocar.axf - 0 Error(s), 0 Warning(s).\n");
    put("..\\tmp\\build-autocar\\objects\\autocar.axf", "new-axf");
    put("..\\tmp\\build-autocar\\objects\\autocar.hex", "new-hex");
    put("..\\tmp\\build-autocar\\listings\\autocar.map", "test-linker-map");
    return 0;
}
