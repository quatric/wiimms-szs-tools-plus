// See astc_wrapper.h.

#include "astc_wrapper.h"
#include "astc_decomp.h"

extern "C" int astc_decompress_block
(
    uint8_t		*dst_rgba,
    const uint8_t	*data,
    int			block_w,
    int			block_h
)
{
    // isSRGB=false: BNTX ASTC textures observed in the wild (Super Mario
    // Odyssey UI/effect layouts) use the plain UNORM format code, not the
    // _SRGB variant, so we decode as linear LDR and let the caller treat
    // the output as straight RGBA8, matching every other BNTX pixel format
    // this decoder already handles.
    return basisu::astc::decompress(dst_rgba, data, false, block_w, block_h) ? 1 : 0;
}
