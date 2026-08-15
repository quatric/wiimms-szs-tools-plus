// wbrsar - Wiimms BRSAR Tool
// Converts Wii BRSAR sound archives (and other formats vgmtrans recognizes)
// to MIDI + SF2/DLS. The vgmtrans BRSAR scanner/sequence/instrument logic is
// statically linked into this binary (see src/vgmtrans/src/ui/cli/
// vgmtrans_bridge.cpp) -- no external process is spawned at runtime.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lib-std.h"
#include "vgmtrans_bridge.h"

int main(int argc, char *argv[])
{
    const char *in_file = NULL;
    const char *out_dir = NULL;
    int format_flags = VGMTRANS_FMT_SF2;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
        {
            printf("wbrsar - Wiimms BRSAR Tool\n"
                   "Converts a BRSAR (or other vgmtrans-recognized) sound bank to MIDI + SF2/DLS.\n"
                   "Extracts all MIDI sequences and exactly 1 copy of the soundfont for the archive.\n\n"
                   "Usage: %s [options] <input.brsar> [output_dir]\n\n"
                   "Options:\n"
                   "  --sf2            Export SoundFont 2 (.sf2) [default]\n"
                   "  --dls            Export DLS (.dls)\n"
                   "  --both           Export both .sf2 and .dls\n"
                   "  -d, --dest <dir> Specify destination directory\n"
                   "  -h, --help       Show this help\n", argv[0]);
            return 0;
        }
        else if (!strcmp(arg, "--sf2"))
            format_flags = VGMTRANS_FMT_SF2;
        else if (!strcmp(arg, "--dls"))
            format_flags = VGMTRANS_FMT_DLS;
        else if (!strcmp(arg, "--both"))
            format_flags = VGMTRANS_FMT_BOTH;
        else if (!strcmp(arg, "-d") || !strcmp(arg, "--dest"))
        {
            if (++i < argc)
                out_dir = argv[i];
        }
        else if (!strncmp(arg, "--dest=", 7))
            out_dir = arg + 7;
        else if (!strncmp(arg, "-d=", 3))
            out_dir = arg + 3;
        else if (*arg != '-')
        {
            if (!in_file)
                in_file = arg;
            else if (!out_dir)
                out_dir = arg;
        }
    }

    if (!in_file)
    {
        printf("wbrsar - Wiimms BRSAR Tool\n"
               "Usage: %s [options] <input.brsar> [output_dir]\n"
               "Type '%s --help' for available options.\n", argv[0], argv[0]);
        return 1;
    }

    char dest_buf[1024];
    if (!out_dir)
    {
        snprintf(dest_buf, sizeof(dest_buf), "%s.d", in_file);
        out_dir = dest_buf;
    }

    struct stat st;
    if (stat(out_dir, &st) != 0)
        mkdir(out_dir, 0755);

    int err = VgmtransConvertFileExt(in_file, out_dir, format_flags);
    if (err)
    {
        fprintf(stderr, "wbrsar: conversion failed for %s\n", in_file);
        return err;
    }
    printf("wbrsar: converted %s -> %s\n", in_file, out_dir);
    return 0;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
