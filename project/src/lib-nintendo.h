// Nintendo-format registry and bounded decoders added by the Nintendo fork.
#ifndef SZS_LIB_NINTENDO_H
#define SZS_LIB_NINTENDO_H 1

#include "types.h"

typedef enum nfmt_type_t
{
    NFMT_UNKNOWN,
    NFMT_DSB, NFMT_TPL, NFMT_STPL, NFMT_SARC,
    NFMT_LZ10, NFMT_LZ11, NFMT_ASH0, NFMT_YAY0,
    NFMT_BFLIM, NFMT_BCLIM, NFMT_BNR, NFMT_NCGR, NFMT_NCER, NFMT_NANR,
    NFMT_BRFNT, NFMT_BRFNA, NFMT_BRLAN, NFMT_BRLYT,
    NFMT_BFLAN, NFMT_BFLYT, NFMT_BCLAN, NFMT_BCLYT,
    NFMT_MSBT, NFMT_BCRES, NFMT_BFRES
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
// Encode with the standard Nintendo LZ10 or LZ11 framing.  LZ11 uses the
// short token form where possible, so the output is accepted by both the DS
// and 3DS SDK decoders without relying on a host-side compressor.
enumError EncodeLZ10LZ11
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11
);
enumError DecodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

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

#endif
