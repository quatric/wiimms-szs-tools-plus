#ifndef SZS_LIB_NITRO_H
#define SZS_LIB_NITRO_H 1

#include "types.h"
#include "lib-nintendo.h"

// Nintendo DS ("Nitro") sprite compositing.
//
// A DS sprite is spread across four files: NCGR holds the 8x8 tile pixels,
// NCLR the palettes, NCER the cells (each a list of hardware OAM records
// placing tiles on screen), and NANR the animations (sequences of cells).
// Rendering one needs all of them together, which is what this layer does --
// the individual decoders in lib-nintendo.c only ever see one file.

// Raw view of NCGR tile pixels (indices, not colours).
typedef struct nitro_ncgr_t
{
    const u8 *tiles;     // tile pixel data
    uint tiles_size;
    uint n_tiles;
    uint bpp;            // 4 or 8
}
nitro_ncgr_t;

// Palette entries as RGBA8, 16 colours per palette bank for 4bpp.
typedef struct nitro_nclr_t
{
    u8   *rgba;          // 4 bytes per entry (owned)
    uint n_entries;
}
nitro_nclr_t;

enumError ScanNitroNCGR ( nitro_ncgr_t *ncgr, const u8 *data, uint size );
enumError ScanNitroNCLR ( nitro_nclr_t *nclr, const u8 *data, uint size );
void      ResetNitroNCLR ( nitro_nclr_t *nclr );

// Renders one NCER cell into a tightly packed RGBA8 image. The cell's OAM
// records use signed screen coordinates, so the output is the bounding box
// of every object in the cell; *ox/*oy receive the box origin so callers can
// line up multiple cells consistently.
enumError RenderNCERCell
(
    u8 **dest, uint *width, uint *height, int *ox, int *oy,
    const nintendo_ncer_t *ncer, uint cell_index,
    const nitro_ncgr_t *ncgr, const nitro_nclr_t *nclr
);

#endif
