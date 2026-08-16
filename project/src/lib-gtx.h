#ifndef SZS_LIB_GTX_H
#define SZS_LIB_GTX_H 1

#include "types.h"

// GTX/GSH ("Gfx2" magic): the Wii U GX2 texture/shader container. Textures
// are stored in the Latte GPU's tiled surface layout (AMD-derived
// micro/macro-tile addressing, fixed at 2 pipes / 4 banks on Wii U hardware)
// and have to be detiled before the pixels mean anything -- a different,
// unrelated algorithm from BNTX's Tegra block-linear layout or BFLIM's
// simple 8x8 Morton scheme. See the long comment above GtxDetile() in
// lib-gtx.c for what's verified and what's deliberately left out.

typedef struct gtx_texture_t
{
    uint dim, width, height, depth, num_mips;
    uint format;               // raw GX2SurfaceFormat word
    uint aa, use;
    uint tile_mode, swizzle, pitch;
    const u8 *data;            // tiled image data (level 0), points into source
    uint data_size;
}
gtx_texture_t;

typedef struct gtx_t
{
    const u8 *data;
    uint size;
    uint n_textures;
    gtx_texture_t *textures;   // owned
}
gtx_t;

enumError ScanGTX  ( gtx_t *gtx, const u8 *data, uint size );
void      ResetGTX ( gtx_t *gtx );

// Decodes texture INDEX (level 0 only) to tightly packed RGBA8. Supports the
// uncompressed RGBA8/RGB565/RGBA5551/RGBA4/R8/R8G8 formats and the BC1-5
// block-compressed ones, tile modes 1 (1D_TILED_THIN1) and 2/3
// (micro-tiled) and 4/7/8/11 (macro-tiled, aspect-ratio-1 family only);
// anything else returns EINVAL rather than guessing.
enumError DecodeGTX_RGBA
(
    u8 **dest, uint *width, uint *height,
    const gtx_t *gtx, uint index
);

#endif
