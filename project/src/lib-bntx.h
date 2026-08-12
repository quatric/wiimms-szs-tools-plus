#ifndef SZS_LIB_BNTX_H
#define SZS_LIB_BNTX_H 1

#include "types.h"

// BNTX ("Binary NX TeXture"): the Switch texture container, also embedded
// inside BFRES/BFFNT/PTCL. Texture data is stored in the Tegra GPU's
// block-linear layout and has to be deswizzled before it means anything.

// Deswizzles one Tegra block-linear surface into linear order.
//
// WIDTH/HEIGHT are in pixels; BLK_W/BLK_H are the compression block size
// (1x1 for uncompressed formats, 4x4 for BC/ASTC), so the surface is
// measured in DIV_ROUND_UP(width,blk_w) x DIV_ROUND_UP(height,blk_h)
// elements of BPP bytes each. TILE_MODE 1 selects the linear (pitch) layout,
// anything else the block-linear one. BLOCK_HEIGHT_LOG2 comes from the
// texture's layout field and must be 0..5.
//
// Returns a malloc'd linear buffer of
// DIV_ROUND_UP(width,blk_w)*DIV_ROUND_UP(height,blk_h)*bpp bytes.
enumError BntxDeswizzle
(
    u8 **dest, uint *dest_size,
    const u8 *src, uint src_size,
    uint width, uint height, uint blk_w, uint blk_h,
    uint bpp, uint tile_mode, uint block_height_log2, bool round_pitch
);

// One texture inside a BNTX container.
typedef struct bntx_texture_t
{
    ccp  name;          // points into the source buffer
    uint width, height;
    uint format;        // raw BNTX format word
    uint tile_mode, block_height_log2;
    uint n_mips;
    const u8 *data;     // swizzled texture data
    uint data_size;
}
bntx_texture_t;

typedef struct bntx_t
{
    const u8 *data;
    uint size;
    uint n_textures;
    bntx_texture_t *textures; // owned
}
bntx_t;

enumError ScanBNTX ( bntx_t *bntx, const u8 *data, uint size );
void      ResetBNTX ( bntx_t *bntx );

// Decodes texture INDEX to tightly packed RGBA8. Supports the uncompressed
// RGBA8/RGB565/RGBA5551/RGBA4 formats and the BC1/BC2/BC3 block-compressed
// ones; other formats return EINVAL rather than guessing.
enumError DecodeBNTX_RGBA
(
    u8 **dest, uint *width, uint *height,
    const bntx_t *bntx, uint index
);

#endif
