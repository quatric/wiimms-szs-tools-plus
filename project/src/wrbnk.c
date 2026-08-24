// wrbnk - Wiimms RBNK Tool
// Dumps a Wii RBNK instrument bank (program -> note-range -> InstParam
// lookup tree, plus embedded WaveInfo metadata for version < 2 banks) as a
// lossless-structure XML. Container/tree-walk logic is a documented port
// of BrawlLib's real RBNK parser (soopercool101/BrawlCrate) -- see
// lib-rbnk.h for provenance and scope.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-rbnk.h"

static void print_usage ( ccp prog )
{
    printf("wrbnk - Wiimms RBNK Tool\n"
           "Dumps a Wii RBNK instrument bank as a lossless-structure XML.\n\n"
           "Usage:\n"
           "  %s dump <input.rbnk> [output.xml]\n", prog);
}

int main ( int argc, char **argv )
{
    stdlog = stderr; // unset otherwise (this tool skips wszst.c's usual startup init)

    if ( argc < 3 || strcasecmp(argv[1],"dump") )
    {
        print_usage(argv[0]);
        return ERR_SYNTAX;
    }

    ccp input_path = argv[2];
    ccp output_path = argc > 3 ? argv[3] : 0;
    char out_buf[PATH_MAX];
    if ( !output_path )
    {
        snprintf(out_buf, sizeof(out_buf), "%s.xml", input_path);
        output_path = out_buf;
    }

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
    if ( err )
    {
        fprintf(stderr, "Error: failed to load input file '%s'\n", input_path);
        return err;
    }

    rbnk_t rbnk;
    err = ScanRBNK(&rbnk, raw, (uint)raw_size);
    FREE(raw);
    if (err)
        return err;

    File_t F;
    err = CreateFileOpt(&F, true, output_path, false, input_path);
    if ( !err && F.f )
    {
        DumpRBNK_XML(&rbnk, F.f, input_path);
        printf("wrbnk: dumped %s -> %s (v%u.%u, %u programs, %u waves)\n",
            input_path, output_path, rbnk.version_major, rbnk.version_minor,
            rbnk.n_program, rbnk.n_wave);
    }
    ResetFile(&F, 0);
    ResetRBNK(&rbnk);
    return err;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
