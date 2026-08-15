// C-linkage wrapper around the vendored ASTC LDR block decoder
// (astc_decomp.cpp/.h, ported from richgel999/astc_dec, itself derived from
// the Android Open Source Project's drawElements Quality Program; Apache-2.0,
// see src/astc/LICENSE). Exposed as plain C so lib-bntx.c (a C translation
// unit) can call it without pulling C++ headers into the rest of the tree.

#ifndef ASTC_WRAPPER_H
#define ASTC_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decodes one ASTC block ('data' must point to 16 bytes = 128 bits) into
// block_w*block_h RGBA8 texels (row-major, 4 bytes/texel) at 'dst_rgba'.
// Returns 1 on a structurally valid block, 0 if the block was invalid
// (in which case an ASTC-spec "error color" -- opaque magenta -- was
// written, matching what real decoders/GPUs show for corrupt blocks).
int astc_decompress_block
(
    uint8_t		*dst_rgba,
    const uint8_t	*data,
    int			block_w,
    int			block_h
);

#ifdef __cplusplus
}
#endif

#endif // ASTC_WRAPPER_H
