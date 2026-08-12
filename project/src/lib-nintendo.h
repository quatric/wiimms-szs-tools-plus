// Nintendo-format registry and bounded decoders added by the Nintendo fork.
#ifndef SZS_LIB_NINTENDO_H
#define SZS_LIB_NINTENDO_H 1

#include "types.h"

typedef enum nfmt_type_t
{
    NFMT_UNKNOWN,
    NFMT_DSB, NFMT_TPL, NFMT_STPL, NFMT_SARC,
    NFMT_LZ10, NFMT_LZ11, NFMT_HUFF4, NFMT_HUFF8, NFMT_RL, NFMT_ASH0, NFMT_YAY0, NFMT_LZH8,
    NFMT_BFLIM, NFMT_BCLIM, NFMT_BNR, NFMT_NCGR, NFMT_NCLR, NFMT_NCER, NFMT_NANR,
    NFMT_BRFNT, NFMT_BRFNA, NFMT_BRLAN, NFMT_BRLYT,
    NFMT_BFLAN, NFMT_BFLYT, NFMT_BCLAN, NFMT_BCLYT,
    NFMT_PLT0,
    NFMT_MSBT, NFMT_BCRES, NFMT_BFRES, NFMT_BNTX, NFMT_GFA
} nfmt_type_t;

typedef struct nfmt_info_t
{
    nfmt_type_t type;
    bool big_endian;
    bool compressed;
    u32 declared_size;
} nfmt_info_t;

// Detect formats by their stable magic/header fields. Never reads past SIZE.
nfmt_info_t DetectNintendoFormat ( const void *data, uint size, ccp filename );
ccp GetNintendoFormatName ( nfmt_type_t type );

// All decode functions allocate *DEST with malloc(); release it with free().
// Return 0 on success, EINVAL for malformed input, EFBIG for unsafe sizes.
enumError DecodeCamelot ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeLZ10LZ11 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoHuff ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// Encode with the standard Nintendo LZ10 or LZ11 framing.  LZ11 uses the
// short token form where possible, so the output is accepted by both the DS
// and 3DS SDK decoders without relying on a host-side compressor.
enumError EncodeLZ10LZ11
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11
);
enumError DecodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// LZH8 (0x40) compression, used by Wii Virtual Console titles.  The decoder
// also accepts the WarioWare Snapped variant with a 4-byte LE size prefix.
enumError DecodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

// Decode Animal Crossing: Wild World TXTR/DSB A3I5 textures to tightly packed
// RGBA8 pixels. WIDTH and HEIGHT are populated on success.
enumError DecodeDSB_RGBA
(
    u8 **dest, uint *width, uint *height,
    const u8 *src, uint src_size
);

// Encode the AC:WW 128x128 TXTR layout: a 32-colour RGB555 palette followed
// by A3I5 texels.
enumError EncodeDSB_RGBA ( u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height );

// Decode the 96x32 RGB5A3 icon embedded at offset 0x20 in Wii BNR1/BNR2
// banner files. The caller owns *DEST on success.
enumError DecodeBNR_RGBA ( u8 **dest, const u8 *src, uint src_size );
// Create a complete BNR1 banner with a 96x32 RGB5A3 icon and zero-filled
// textual metadata fields.  BNR1 is accepted by Wii/GameCube banner readers.
enumError EncodeBNR_RGBA ( u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height );
// Decode NCGR tile data as a 16-tile-wide indexed grayscale sheet. A paired
// NCLR palette can be applied by the higher-level DS asset project layer.
enumError DecodeNCGR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

// Decode a Nitro NCLR (RLCN/TTLP) palette into a readable RGBA8 swatch
// image. Each palette entry is an opaque BGR555 colour; entries are laid out
// in 16 columns with 8x8 pixel swatches.
enumError DecodeNCLR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

// Bounded view of a Nitro NCER cell bank. Object attributes are the original
// six-byte DS OAM records and remain owned by the NCER file buffer.
typedef struct nintendo_ncer_t
{
    const u8 *data, *cells, *objects;
    uint size, n_cells, cell_size, objects_size;
}
nintendo_ncer_t;

enumError ScanNCER ( nintendo_ncer_t *ncer, const u8 *data, uint size );
enumError GetNCERCell
(
    const nintendo_ncer_t *ncer, uint index, uint *n_objects,
    const u8 **oam_records
);

// Bounded view of a Nitro NANR animation bank. Frame records and cell indices
// remain pointers into the original input data.
typedef struct nintendo_nanr_t
{
    const u8 *data, *animations, *frames, *frame_data;
    uint size, n_animations, n_frames, frames_size, frame_data_size;
}
nintendo_nanr_t;

enumError ScanNANR ( nintendo_nanr_t *nanr, const u8 *data, uint size );
enumError GetNANRAnimation
(
    const nintendo_nanr_t *nanr, uint index, uint *n_frames,
    const u8 **frame_records
);

// Decode the common BFLIM/BCLIM trailing-footer layout.  The uncompressed
// formats R8, RGB565, RGBA5551, RGBA4 and RGBA8 are accepted in both linear
// and 8x8 Morton-swizzled order.  Unsupported GPU-compressed formats return
// EINVAL rather than producing pixels with a bogus alpha channel.
enumError DecodeFLIM_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);
// Write a little-endian, RGBA8, 8x8-Morton-swizzled BFLIM or BCLIM.  The
// encoder deliberately uses the shared portable subset understood by the
// matching decoder and common CTR tooling.
enumError EncodeFLIM_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
    bool bclim
);

typedef struct nintendo_sarc_t
{
    const u8 *data;
    uint size, data_offset, sfnt_offset, entries_offset, n_entries;
    bool big_endian;
}
nintendo_sarc_t;

// Parse either SARC byte order without allocating.  Entry pointers returned
// by GetSARCEntry remain owned by the original input buffer.
enumError ScanSARC ( nintendo_sarc_t *sarc, const u8 *data, uint size );
enumError GetSARCEntry
(
    const nintendo_sarc_t *sarc, uint index, ccp *name,
    const u8 **data, uint *size
);

typedef struct nintendo_sarc_entry_t
{
    ccp name;
    const u8 *data;
    uint size;
}
nintendo_sarc_entry_t;

// Build a canonical SARC with a 0x100-byte aligned data section.  ENTRIES are
// sorted by the standard 0x65 SFAT hash; the returned buffer is malloc-owned.
enumError CreateSARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
    uint n_entries, bool big_endian
);


//-----------------------------------------------------------------------------
// GFA (Good-Feel archive, "GFAC" magic): the container used by Good-Feel's
// Wii titles -- Wario Land: Shake It! / The Shake Dimension, Kirby's Epic
// Yarn. A GFAC header plus a name/offset table, whose payload is one
// "GFCP"-compressed blob holding every member file back to back.

typedef struct gfa_entry_t
{
    ccp  name;    // points into the gfa_t name storage
    u32  offset;  // offset within the decompressed blob
    u32  size;    // 0 marks a directory entry
}
gfa_entry_t;

typedef struct gfa_t
{
    u8          *blob;      // decompressed payload (owned)
    uint        blob_size;
    gfa_entry_t *entries;   // owned
    uint        n_entries;
    char        *names;     // owned name storage
    uint        compression; // GFCP type: 1=BPE, 2/3=raw LZ10
}
gfa_t;

void      ResetGFA ( gfa_t *gfa );
enumError ScanGFA  ( gfa_t *gfa, const u8 *data, uint size );

// Raw (headerless) LZ10 as used by GFCP: the stream starts directly with the
// block data, so the output size has to be supplied by the caller.
enumError DecodeLZ10Raw ( u8 *dest, uint dest_size, const u8 *src, uint src_size );
// Byte Pair Encoding, the other GFCP compression mode.
enumError DecodeBPE ( u8 *dest, uint dest_size, const u8 *src, uint src_size );

#endif
