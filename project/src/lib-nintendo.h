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
    NFMT_BRFNT, NFMT_BRFNA, NFMT_BCFNT, NFMT_BRLAN, NFMT_BRLYT,
    NFMT_BFLAN, NFMT_BFLYT, NFMT_BCLAN, NFMT_BCLYT,
    NFMT_PLT0,
    NFMT_MSBT, NFMT_BCRES, NFMT_BFRES, NFMT_BNTX, NFMT_GFA, NFMT_BCH, NFMT_QLZ,
    NFMT_PAC,
    NFMT_RNC, NFMT_PSDK, NFMT_AT7, NFMT_CTPK,
    NFMT_BYML, NFMT_NARC
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
enumError EncodeCamelot ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeLZ10LZ11 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoHuff ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeNintendoHuff
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool four_bit
);
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

// BLZ ("backward LZSS", DS ARM9/ARM7/overlay compression). No header magic
// to detect by -- only call this where the caller already knows the file
// might be BLZ (e.g. an ndstool-staged arm9.bin/arm7.bin/overlay), not from
// generic format-dispatch code. A file whose footer says "not coded"
// (BLZ_Encode() left it uncompressed) decodes to the input verbatim,
// matching the real reference tool's own behavior.
enumError DecodeBLZ ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeBLZ ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// LZH8 (0x40) compression, used by Wii Virtual Console titles.  The decoder
// also accepts the WarioWare Snapped variant with a 4-byte LE size prefix.
enumError DecodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// AT7 (AT7P/AT7X/AT7E) compression, used by Pokémon Mystery Dungeon WiiWare titles.
enumError DecodeAT7 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeAT7 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// RNC (Rob Northen Compression), RNC1/RNC2 methods. Decodes the 18-byte
// framed stream; keyed (encrypted) streams are rejected with EINVAL.
enumError DecodeRNC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeRNC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method );

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
// Encode an RGBA sheet to a Wii bitmap font (BRFNT / RFNT) with FINF/TGLP/CWDH/CMAP.
enumError EncodeBRFNT_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h
);

enumError EncodeBRFNA_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h
);
// Encode an RGBA sheet to a 3DS/Wii U bitmap font (BCFNT / BFFNT).
enumError EncodeBCFNT_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h, bool is_wiiu
);
// Decode NCGR tile data as a 16-tile-wide indexed grayscale sheet. A paired
// NCLR palette can be applied by the higher-level DS asset project layer.
enumError DecodeNCGR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

enumError EncodeNCGR_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool is_8bpp
);

// Decode a Nitro NCLR (RLCN/TTLP) palette into a readable RGBA8 swatch
// image. Each palette entry is an opaque BGR555 colour; entries are laid out
// in 16 columns with 8x8 pixel swatches.
enumError DecodeNCLR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

enumError EncodeNCLR_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height
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

// CTPK (CTR Texture Package, 3DS container)
typedef struct nintendo_ctpk_entry_t
{
    char name[PATH_MAX];
    uint width;
    uint height;
    uint format;
    uint mip_level;
    uint type;
    const u8 *data;
    uint data_size;
}
nintendo_ctpk_entry_t;

typedef struct nintendo_ctpk_t
{
    const u8 *data;
    uint size;
    uint version;
    uint n_entries;
    uint texture_offset;
    uint texture_size;
}
nintendo_ctpk_t;

enumError ScanCTPK ( nintendo_ctpk_t *ctpk, const u8 *data, uint size );
enumError GetCTPKEntry
(
    const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry
);
enumError DecodeCTPKTexture_RGBA
(
    u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry
);
enumError EncodeCTPK
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name
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

enumError CreateNARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
    uint n_entries, bool is_le
);

enumError CreateDARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreatePAC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateRARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateGFA
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
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
enumError EncodeLZ10Raw ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

//-----------------------------------------------------------------------------
// PAC ("ARC\0" magic): Super Smash Bros. Brawl's flat archive format,
// bundling a fighter/stage/UI's models, textures, animations (including
// embedded PLT0 palette-animation chunks) as uncompressed, unnamed, typed
// entries. Layout per BrawlLib's own struct definitions (libertyernie/
// BrawlCrate, BrawlLib/SSBB/Types/ARC.cs ARCHeader/ARCFileHeader) -- unlike
// GFA/SARC there is no compression and no per-entry filename, so members are
// exposed by index + a numeric ARCFileType (BrawlLib's ARCFileType enum).

typedef struct pac_entry_t
{
    u16      type;    // ARCFileType: 1=Misc 2=Model 3=Texture 4=Animation
                       // 5=Scene 6=Type6 7=GroupedArchive 8=Effect
    u16      index;
    u8       group_index;
    s16      redirect_index; // -1 (0xffff) if this entry owns its own data
    const u8 *data;   // points into the source buffer; NULL when redirected
    u32      size;
}
pac_entry_t;

typedef struct pac_t
{
    const u8    *data;
    uint        size;
    char        name[48]; // archive's embedded name, e.g. "FitPeach"
    pac_entry_t *entries;  // owned
    uint        n_entries;
}
pac_t;

void      ResetPAC ( pac_t *pac );
enumError ScanPAC  ( pac_t *pac, const u8 *data, uint size );
// Byte Pair Encoding, the other GFCP compression mode.
enumError DecodeBPE ( u8 *dest, uint dest_size, const u8 *src, uint src_size );

//-----------------------------------------------------------------------------
// DARC ("darc" magic): the 3DS/NW4C "differential archive" container --
// structurally the CTR-era counterpart of the Wii's U8/SARC format (same
// idea: a flat table of file/folder nodes plus a name blob), but with its
// own header shape and a parent-index/end-index tree instead of SARC's
// hash-based node table. Real titles use it to bundle a whole family of
// related BFLYT/BFLAN layouts into one romfs file (e.g. Tomodachi Life's
// romfs/layout/*.bin -- confirmed on a real retail disc, CTR-P-EC6E).
//
// Layout verified byte-for-byte against a real sample plus two independent
// docs (GBATEK's 3DS-files-archive-darc page, 3dbrew's DARC page) and one
// working reference implementation (Tyulis/3DSkit's unpack/DARC.py) -- all
// three agree on the essential offsets:
//   Header (0x1C bytes), all fields little-endian:
//     0x00 char[4] magic "darc"
//     0x04 u16     BOM (bytes FF FE -> 0xFEFF read LE)
//     0x06 u16     header_size (0x001C)
//     0x08 u32     version
//     0x0C u32     file_size (total archive size)
//     0x10 u32     table_offset (start of the entry table; usually == 0x1C)
//     0x14 u32     table_size (entry table + name area, combined)
//     0x18 u32     data_offset (where file content starts; 32-byte aligned)
//   Entry (12 bytes each), table_size/12 of them, starting at table_offset:
//     +0x00 u32  bits 0-23 = name offset (from the name area, which starts
//                right after the last entry, i.e. table_offset + n*12);
//                bit 24 (0x01000000) = is-directory flag
//     +0x04 u32  files: absolute data offset from archive start.
//                dirs:  parent entry index
//     +0x08 u32  files: data length in bytes.
//                dirs:  end index (exclusive) -- this dir's descendants are
//                       every entry with index in (this_index, end_index)
//   Entry 0 is always the root directory; its end-index equals the total
//   entry count. Entry 1 is often a "." alias of the root and should be
//   skipped when reconstructing paths, same as GBATEK documents.
//   Names are UTF-16LE, NUL-terminated, stored in the name area.
// All of the above matched a real sample byte-for-byte: magic/BOM/header
// size/version(0x01000000)/table+data offsets and alignment, root entry's
// directory flag + end-index, and the "." alias entry's name offset.

typedef struct darc_entry_t
{
    bool  is_dir;
    u32   parent_or_offset; // dirs: parent index.   files: data offset.
    u32   end_or_size;      // dirs: end index.       files: data size.
    char  *name;            // owned, UTF-8; "" for the root entry
}
darc_entry_t;

typedef struct darc_t
{
    const u8     *data;   // not owned, borrowed from the caller's buffer
    uint         size;
    darc_entry_t *entries; // owned
    uint         n_entries;
}
darc_t;

void      ResetDARC ( darc_t *darc );
enumError ScanDARC  ( darc_t *darc, const u8 *data, uint size );

// BYML / BYAML (Binary YAML parameter format, 3DS / Wii U / Switch)
enumError DecodeBYML_YAML ( FILE *out, const u8 *data, size_t size );
enumError EncodeBYML_Text ( u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version );

// NARC (Nitro Archive, DS / 3DS container)
typedef struct narc_entry_t
{
    char *name; // owned, UTF-8
    u32  offset; // relative to FIMG data
    u32  size;
} narc_entry_t;

typedef struct narc_t
{
    const u8     *raw;
    size_t       raw_size;
    bool         is_le;
    narc_entry_t *entries;
    uint         n_entries;
    const u8     *fimg_data;
    uint         fimg_size;
} narc_t;

void      ResetNARC ( narc_t *narc );
enumError ScanNARC  ( narc_t *narc, const u8 *data, size_t size );

#endif
