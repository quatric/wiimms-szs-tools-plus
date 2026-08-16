// Wii Virtual Console formats (CCF archive, romc/romchu ROM compression) --
// see lib-vc.h.

#include "lib-std.h"
#include "lib-vc.h"
#include "lib-nintendo.h"
#include <zlib.h>

static inline u32 vrd32 ( const u8 *p )
    { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }

//-----------------------------------------------------------------------------
///////////////		CCF archive				///////////////
//-----------------------------------------------------------------------------

// Layout per the WiiBrew "CCF archive" page: 32-byte header ("CCF\0" +
// 12 zero bytes + offset_multiplier(u32) + file_count(u32) + 8 zero bytes),
// then file_count 32-byte descriptors (name[20] + data_offset(u32, in units
// of offset_multiplier) + size(u32) + decompressed_size(u32)). Not yet
// cross-checked against a real CCF-magic file -- none has turned up in any
// real retail Wii VC WAD examined so far (Yoshi's Story N64 stores its ROM
// completely uncompressed with no CCF wrapper at all; Kirby 64 N64 uses
// "romc" compression -- see DecodeRomC() -- applied directly, again with no
// CCF wrapper). Struct layout cross-checked against TWO independent
// sources that agree byte-for-byte: the WiiBrew wiki's documented struct
// AND paulguy/ccf-tools' actual reference C source (ccfex.c: `struct
// ccffile { char filename[20]; uint offset; uint datasize; uint filesize;
// }`, decompressed via zlib's standard uncompress(), matching this file's
// inflateInit2(&zs,15) -- not raw deflate). That repo's later commit
// history ("Updated to handle different chunk sizes found in Switch SEGA
// AGES games") confirms CCF also appears on Switch, not just Wii VC -- a
// lead worth chasing if a Wii sample keeps not turning up. Bounds-checked
// the usual way, but flagged here as unverified-against-real-bytes until
// an actual CCF-magic file is found.
enumError ScanCCF ( ccf_t *ccf, const u8 *data, uint size )
{
    if ( !ccf || !data || size < 32 || memcmp(data,"CCF\0",4) )
	return EINVAL;

    const u32 mult = vrd32(data+16);
    const u32 n    = vrd32(data+20);
    if ( !mult || !n || n > 0x10000 || (u64)32 + (u64)n*32 > size )
	return EINVAL;

    ccf_entry_t *entries = CALLOC(n,sizeof(*entries));
    if (!entries) return ERR_CANT_CREATE;

    for ( u32 i = 0; i < n; i++ )
    {
	const u8 *d = data + 32 + (size_t)i*32;
	memcpy(entries[i].name,d,20);
	entries[i].name[20] = 0;
	const u32 off_units = vrd32(d+20);
	const u32 fsize = vrd32(d+24);
	const u32 dsize = vrd32(d+28);
	const u64 off = (u64)off_units * mult;
	if ( off+fsize > size )
	    { FREE(entries); return EINVAL; }
	entries[i].data = data+off;
	entries[i].size = fsize;
	entries[i].decompressed_size = dsize;
    }

    memset(ccf,0,sizeof(*ccf));
    ccf->data = data;
    ccf->size = size;
    ccf->n_entries = n;
    ccf->entries = entries;
    return ERR_OK;
}

void ResetCCF ( ccf_t *ccf )
{
    if (!ccf) return;
    FREE(ccf->entries);
    memset(ccf,0,sizeof(*ccf));
}

enumError DecodeCCFEntry
(
    u8 **dest, uint *dest_size, const ccf_entry_t *entry
)
{
    if ( !dest || !dest_size || !entry || !entry->data )
	return EINVAL;

    if ( entry->decompressed_size == entry->size )
    {
	// stored raw
	u8 *out = MALLOC(entry->size);
	if (!out) return ERR_CANT_CREATE;
	memcpy(out,entry->data,entry->size);
	*dest = out;
	*dest_size = entry->size;
	return ERR_OK;
    }

    // Zlib-compressed (standard zlib stream, per the WiiBrew doc's
    // "pass into ZLib's deflate()/zpipe" wording -- i.e. a plain
    // compress()-style stream with the normal 2-byte zlib header, not
    // raw deflate).
    u8 *out = MALLOC(entry->decompressed_size);
    if (!out) return ERR_CANT_CREATE;

    z_stream zs; memset(&zs,0,sizeof(zs));
    zs.next_in   = (Bytef*)entry->data;
    zs.avail_in  = entry->size;
    zs.next_out  = out;
    zs.avail_out = entry->decompressed_size;
    if ( inflateInit2(&zs,15) != Z_OK )
	{ FREE(out); return ERR_ERROR; }
    const int rc = inflate(&zs,Z_FINISH);
    const uint produced = entry->decompressed_size - zs.avail_out;
    inflateEnd(&zs);
    if ( rc != Z_STREAM_END || produced != entry->decompressed_size )
	{ FREE(out); return ERR_INVALID_DATA; }

    *dest = out;
    *dest_size = produced;
    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		romc / romchu ROM compression		///////////////
//-----------------------------------------------------------------------------

// N64 Virtual Console's own ROM compressor (real tool name "romc", per a
// 2008 GBAtemp thread -- "For encoding a rom for use in the latest versions
// of the Nintendo 64 Virtual Console emulator... Currently supported by
// games such as Cruis'n USA and Kirby 64"). Not every N64 VC title uses it
// -- verified on two real retail titles: Yoshi's Story (USA) stores its ROM
// completely uncompressed (a plain file literally named "rom" inside the
// emulator app's U8 archive, real N64 header, no compression at all), while
// Kirby 64 - The Crystal Shards (USA) has a file named "romc" in the same
// location that IS compressed with this format.
//
// Header (4 bytes, found by inspecting the real Kirby 64 "romc" file and
// cross-checked against Plombo/vcromclaim's romc.py, itself a from-scratch
// reimplementation predating any sample access here):
//   byte 0: number of 4MB units in the DECOMPRESSED size (the N64 VC
//           emulator always maps a fixed-size cart image regardless of the
//           real game's ROM size -- Kirby 64 is a real 12MB cart but
//           decompresses to a full 32MB image, byte 0 = 8).
//   bytes 1-2: unused/reserved in both samples (always 0 so far).
//   byte 3, low 2 bits: compression type. 1 = plain LZ77 (this fork's
//           already-verified DecodeLZ10LZ11, type 0x10 -- romc's own
//           stream has no embedded tag/size prefix the way a standalone
//           .lz10 file would, so one is synthesized here before handing
//           off to the shared decoder). 2 = "romchu" (LZ77 + a Huffman
//           second pass) -- NOT implemented; no real sample of type 2 has
//           been found yet, so this declines (EINVAL) rather than guess.
//
// Verified byte-exact on the real Kirby 64 sample: decompresses to exactly
// 33554432 bytes (8 * 4MB), starting with the real N64 magic (0x80371240)
// and the correct internal ROM name ("Kirby64") at the standard offset
// 0x20.
enumError DecodeRomC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if ( !dest || !dest_size || !src || src_size < 4 )
	return EINVAL;

    const uint units = src[0];
    const uint out_len = units * 4*1024*1024;
    const uint comp_type = src[3] & 3;
    if ( !units || !out_len )
	return EINVAL;

    if ( comp_type == 1 )
    {
	// This is the same LZ77/LZ10 token stream this fork's own
	// DecodeLZ10LZ11() already decodes -- but that function derives its
	// output size from a *24-bit* header field (max 16MB), and a real
	// romc stream can decompress past that (Kirby 64's real sample is a
	// full 32MB image). romc's own 4-byte header already carries the
	// real size as a plain byte count (units*4MB, not clamped to 24
	// bits), so this can't just synthesize a standard LZ10 header and
	// hand off to the shared decoder -- an earlier attempt at exactly
	// that silently truncated the reconstructed size to its low 24 bits
	// (33554432 -> 131072) before this was caught. Duplicated token-
	// stream loop instead, identical to DecodeLZ10LZ11's LZ10 branch,
	// parametrized on the real 32-bit out_len.
	u8 *out = MALLOC(out_len);
	if (!out) return ERR_CANT_CREATE;
	uint sp = 4, dp = 0;
	while ( dp < out_len )
	{
	    if ( sp >= src_size ) goto invalid;
	    u8 flags = src[sp++];
	    for ( uint bit = 0; bit < 8 && dp < out_len; bit++, flags <<= 1 )
	    {
		if ( !(flags & 0x80) )
		{
		    if ( sp >= src_size ) goto invalid;
		    out[dp++] = src[sp++];
		}
		else
		{
		    if ( sp+2 > src_size ) goto invalid;
		    const u8 a = src[sp++], b = src[sp++];
		    const uint len = (a>>4)+3;
		    const uint back = ((uint)(a&15)<<8 | b)+1;
		    if ( back > dp || len > out_len-dp ) goto invalid;
		    for ( uint k = 0; k < len; k++ )
			{ out[dp] = out[dp-back]; dp++; }
		}
	    }
	}
	*dest = out;
	*dest_size = out_len;
	return ERR_OK;
    invalid:
	FREE(out);
	return EINVAL;
    }

    // comp_type == 2 (romchu/Huffman) or anything else: not implemented.
    return EINVAL;
}
