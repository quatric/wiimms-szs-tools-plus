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

// Same decode, but for a raw GX2Surface not wrapped in a Gfx2/GTX container
// -- e.g. a BFRES FTEX subfile, which embeds the identical surface fields
// (dim/width/height/format/tileMode/pitch) directly in its own header.
enumError DecodeGX2Surface_RGBA
(
    u8 **dest, uint *width, uint *height,
    uint dim, uint w, uint h, uint format, uint tile_mode, uint pitch,
    const u8 *data, uint data_size
);

// Encodes a tightly packed RGBA8 image to a standalone .gtx (Gfx2)
// container: a single 2D, single-mip GX2Texture in tile mode 1
// (LINEAR_ALIGNED), format R8_G8_B8_A8_UNORM, pitch = width elements. Tile
// mode 1 is used deliberately -- it's the one tile mode whose addressing
// (plain row-major, see gtx_detile()'s tile_mode==0||1 branch) is exact,
// not an approximation of the real GPU's macro-tile hashing, so the output
// always decodes back byte-for-byte via DecodeGTX_RGBA. The genuine Wii U
// hardware accepts LINEAR_ALIGNED textures fine (just without the tiled
// layout's cache locality), matching how this fork's other from-scratch
// encoders (BNTX, NCGR, ...) prioritize a verified-correct round trip over
// replicating retail files' exact tiling.
enumError EncodeGTX_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height
);

#endif
