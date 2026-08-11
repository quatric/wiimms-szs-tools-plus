#include <stdio.h>
#include <string.h>
#include "lib-std.h"

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }

extern int main_cmp(int argc, char **argv);
extern int main_dec(int argc, char **argv);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("wlzh8 - Nintendo LZH8 Compression/Decompression Tool\n");
        printf("Usage: wlzh8 [cmp|dec] [options...]\n");
        return 1;
    }
    if (strcmp(argv[1], "cmp") == 0) {
        return main_cmp(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "dec") == 0) {
        return main_dec(argc - 1, argv + 1);
    } else {
        printf("wlzh8 - Nintendo LZH8 Compression/Decompression Tool\n");
        printf("Usage: wlzh8 [cmp|dec] [options...]\n");
        return 1;
    }
}
