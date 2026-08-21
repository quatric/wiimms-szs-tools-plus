// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Monster Games Excite Truck / ExciteBots (Wii) asset formats.
//
// Ported from the standalone ExciteExtract research tool (Python), see the
// comments above ScanTEX() and DecodeExciteMSH() for format details.
//-----------------------------------------------------------------------------

#ifndef LIB_EXCITE_H
#define LIB_EXCITE_H

#include "types.h"

//-----------------------------------------------------------------------------
///////////////		.tex textures (GX pixel data + footer)		///////////////
//-----------------------------------------------------------------------------

typedef struct excite_tex_t
{
    u8   *rgba;   // owned, width*height*4, row-major RGBA8
    uint width;
    uint height;
    uint gx_format;  // the recovered GX texture format (0x0..0xE)
    float score;      // lower = more confident classification
}
excite_tex_t;

void      ResetExciteTEX ( excite_tex_t *tex );

// Recognise and decode a .tex file (Excite Truck / ExciteBots GX texture with
// a trailing dimension footer, no stored pixel format). Returns ERR_OK and
// fills *tex on success; ERR_NOTHING_TO_DO if this is not a recognisable
// Excite .tex payload.
enumError ScanTEX ( excite_tex_t *tex, const u8 *data, uint size );

// Same recovery, but for single-mip-level GUI art (.art/.img): no footer at
// all (file size is exactly 2^k+128, trailing bytes are zero), so dimensions
// and format are both recovered from tile-seam continuity alone.
enumError ScanART ( excite_tex_t *tex, const u8 *data, uint size );

//-----------------------------------------------------------------------------
///////////////		.msh collision meshes				///////////////
//-----------------------------------------------------------------------------

// Headerless, magic-less flat array of little-endian float32 XYZ triples,
// consumed as a sequential triangle soup (every 3 positions = 1 triangle).
// Writes a COLLADA .dae beside 'out_dae_path'. Returns ERR_NOTHING_TO_DO if
// 'size' is not a positive multiple of 12 (i.e. not plausibly this format).
enumError DecodeExciteMSH ( const u8 *data, uint size, ccp out_dae_path );

#endif // LIB_EXCITE_H
