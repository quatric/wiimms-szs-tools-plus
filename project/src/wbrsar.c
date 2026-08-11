// wbrsar - Wiimms BRSAR Tool
// Converts Wii BRSAR sound archives (and other formats vgmtrans recognizes)
// to MIDI + SF2. The vgmtrans BRSAR scanner/sequence/instrument logic is
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
    if (argc < 3)
    {
	printf("wbrsar - Wiimms BRSAR Tool\n"
	       "Converts a BRSAR (or other vgmtrans-recognized) sound bank to MIDI + SF2.\n"
	       "Usage: %s <input.brsar> <output_dir>\n", argv[0]);
	return 1;
    }

    struct stat st;
    if ( stat(argv[2],&st) != 0 )
	mkdir(argv[2],0755);

    int err = VgmtransConvertFile(argv[1],argv[2]);
    if (err)
    {
	fprintf(stderr,"wbrsar: conversion failed for %s\n",argv[1]);
	return err;
    }
    printf("wbrsar: converted %s -> %s\n",argv[1],argv[2]);
    return 0;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
