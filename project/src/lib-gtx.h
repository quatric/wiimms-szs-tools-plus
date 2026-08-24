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
    uint mip_offsets[13];
    uint view_first_mip, view_num_mips, view_first_slice, view_num_slices;
    u8 comp_sel[4];
    const u8 *data;            // tiled image data (level 0), points into source
    uint data_size;
    const u8 *mip_data;        // packed levels 1..n, points into source
    uint mip_data_size;
}
gtx_texture_t;

typedef struct gtx_block_t
{
    uint type, offset, header_size, data_size;
    const u8 *data;            // payload, points into source
}
gtx_block_t;

typedef enum gtx_shader_stage_t
{
    GTX_SHADER_VERTEX, GTX_SHADER_PIXEL, GTX_SHADER_GEOMETRY,
    GTX_SHADER_COMPUTE
}
gtx_shader_stage_t;

typedef struct gtx_shader_t
{
    gtx_shader_stage_t stage;
    const gtx_block_t *header, *program, *copy_program;
}
gtx_shader_t;

typedef struct gtx_t
{
    const u8 *data;
    uint size;
    uint version_major, version_minor, gpu_version, alignment;
    uint n_blocks;
    gtx_block_t *blocks;       // owned; payloads point into source
    uint n_textures;
    gtx_texture_t *textures;   // owned
    uint n_shaders;
    gtx_shader_t *shaders;     // owned; block pointers refer to blocks[]
}
gtx_t;

enumError ScanGTX  ( gtx_t *gtx, const u8 *data, uint size );
void      ResetGTX ( gtx_t *gtx );

// Return the byte offset of one element in a level-0, single-sample GX2
// surface. This is shared by decoding, future tiled encoding, and the
// differential AddrLib regression test. UINT64_MAX denotes an unsupported
// tile mode or invalid input.
u64 GetGX2SurfaceOffset
(
    uint x, uint y, uint bpp, uint width, uint height,
    uint tile_mode, uint pitch, uint swizzle
);

// Decodes texture INDEX (level 0 only) to tightly packed RGBA8. Supports the
// uncompressed RGBA8/RGB565/RGBA5551/RGBA4/R8/R8G8 formats and the BC1-5
// block-compressed ones, tile modes 0/1 (LINEAR_GENERAL/LINEAR_ALIGNED,
// plain row-major), 2/3 (1D_TILED_THIN1/THICK, micro-tiled), and the full
// 2D/2B macro-tiled family 4-11, including aspect ratios 1/2/4, bank swaps,
// and the GX2 pipe/bank swizzle. 3D tile modes and multisampled/depth
// surfaces remain outside this RGBA decoder's contract.
enumError DecodeGTX_RGBA
(
    u8 **dest, uint *width, uint *height,
    const gtx_t *gtx, uint index
);

// Decode a selected mip level. Level 0 is identical to DecodeGTX_RGBA().
// Packed mip offsets and dimensions are interpreted according to GX2Texture.
enumError DecodeGTXMip_RGBA
(
    u8 **dest, uint *width, uint *height,
    const gtx_t *gtx, uint index, uint mip_level
);

// Same decode, but for a raw GX2Surface not wrapped in a Gfx2/GTX container
// -- e.g. a BFRES FTEX subfile, which embeds the identical surface fields
// (dim/width/height/format/tileMode/pitch) directly in its own header.
enumError DecodeGX2Surface_RGBA
(
    u8 **dest, uint *width, uint *height,
    uint dim, uint w, uint h, uint format, uint tile_mode, uint pitch,
    uint swizzle, const u8 *data, uint data_size
);

// Encodes a tightly packed RGBA8 image to a standalone .gtx (Gfx2)
// container: a single 2D, single-mip GX2Texture in genuine tile mode 4
// (2D_TILED_THIN1), with a macro-tile-aligned pitch and explicit EOF block.
enumError EncodeGTX_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height
);

#endif
