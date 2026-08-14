#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"

#define NFMT_MAX_OUTPUT (512u<<20)

static inline u32 rd_be32 ( const u8 *p )
    { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static inline u32 rd_le32 ( const u8 *p )
    { return (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0]; }
static inline u16 rd_be16 ( const u8 *p )
    { return (u16)p[0]<<8 | p[1]; }
static inline u16 rd_le16 ( const u8 *p )
    { return (u16)p[1]<<8 | p[0]; }
static inline void wr_be16 ( u8 *p, u16 v ) { p[0] = v >> 8; p[1] = v; }
static inline void wr_le16 ( u8 *p, u16 v ) { p[0] = v; p[1] = v >> 8; }
static inline void wr_be32 ( u8 *p, u32 v )
    { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static inline void wr_le32 ( u8 *p, u32 v )
    { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

ccp GetNintendoFormatName ( nfmt_type_t type )
{
    static const ccp tab[] = {
        "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "HUFF4", "HUFF8", "RL", "ASH0", "Yay0", "LZH8",
        "BFLIM", "BCLIM", "BNR", "NCGR", "NCLR", "NCER", "NANR", "BRFNT", "BRFNA", "BCFNT", "BRLAN", "BRLYT",
        "BFLAN", "BFLYT", "BCLAN", "BCLYT", "PLT0", "MSBT", "BCRES", "BFRES", "BNTX", "GFA", "BCH", "QuickLZ",
        "PAC", "RNC", "PSDK"
    };
    return type < sizeof(tab)/sizeof(*tab) ? tab[type] : "UNKNOWN";
}

static nfmt_info_t make_info ( nfmt_type_t type, bool be, bool compressed, u32 size )
{
    nfmt_info_t inf = { type, be, compressed, size };
    return inf;
}

nfmt_info_t DetectNintendoFormat ( const void *vdata, uint size, ccp filename )
{
    const u8 *d = vdata;
    if ( !d || !size ) return make_info(NFMT_UNKNOWN,true,false,0);
    if ( size >= 4 )
    {
        const u32 magic = rd_be32(d);
        if (!memcmp(d,"TXTR",4)) return make_info(NFMT_DSB,true,false,0);
        if (magic == 0x0020af30) return make_info(NFMT_TPL,true,false,0);
        if (!memcmp(d,"SARC",4)) return make_info(NFMT_SARC, size >= 8 && d[6] == 0xfe, false, 0);
        if (!memcmp(d,"ASH0",4)) return make_info(NFMT_ASH0,true,true,size >= 8 ? rd_be32(d+4) : 0);
        if (!memcmp(d,"Yay0",4)) return make_info(NFMT_YAY0,true,true,size >= 8 ? rd_be32(d+4) : 0);
        if (!memcmp(d,"BNR1",4) || !memcmp(d,"BNR2",4)) return make_info(NFMT_BNR,true,false,0);
        if (!memcmp(d,"RGCN",4)) return make_info(NFMT_NCGR,true,false,0);
        if (!memcmp(d,"RLCN",4)) return make_info(NFMT_NCLR,true,false,0);
        if (!memcmp(d,"RECN",4)) return make_info(NFMT_NCER,true,false,0);
        if (!memcmp(d,"RNAN",4)) return make_info(NFMT_NANR,true,false,0);
        if (!memcmp(d,"RFNT",4)) return make_info(NFMT_BRFNT,true,false,0);
        if (!memcmp(d,"RFNA",4)) return make_info(NFMT_BRFNA,true,false,0);
        // BCFNT (3DS) and BFFNT (Wii U) share the exact same "CFNT" container
        // and, for the common fontType==1 case, the exact same TGLP glyph-sheet
        // layout as Wii's RFNT -- verified against NintyFont's from-source
        // CFNT/FINF/TGLP reader (hadashisora/NintyFont). Endianness is
        // determined per-file from the BOM at +4, not from the magic, since
        // 3DS files are little endian and Wii U ones are big endian.
        if (!memcmp(d,"CFNT",4)) return make_info(NFMT_BCFNT,true,false,0);
        if (!memcmp(d,"FFNT",4)) return make_info(NFMT_BCFNT,true,false,0); // Wii U: real, different magic, same family
        if (!memcmp(d,"RLAN",4)) return make_info(NFMT_BRLAN,true,false,0);
        if (!memcmp(d,"RLYT",4)) return make_info(NFMT_BRLYT,true,false,0);
        if (!memcmp(d,"FLAN",4)) return make_info(NFMT_BFLAN,true,false,0);
        if (!memcmp(d,"FLYT",4)) return make_info(NFMT_BFLYT,true,false,0);
        if (!memcmp(d,"CLAN",4)) return make_info(NFMT_BCLAN,true,false,0);
        if (!memcmp(d,"CLYT",4)) return make_info(NFMT_BCLYT,true,false,0);
        if (!memcmp(d,"PLT0",4)) return make_info(NFMT_PLT0,true,false,0);
        if (!memcmp(d,"MsgStdBn",8)) return make_info(NFMT_MSBT,true,false,0);
        if (!memcmp(d,"CGFX",4)) return make_info(NFMT_BCRES,true,false,0);
        if (!memcmp(d,"FRES",4)) return make_info(NFMT_BFRES,true,false,0);
        // BNTX (Switch texture container). Full pixel decode is implemented
        // in lib-bntx.c (DecodeBNTX_RGBA, wired into wimgt DECODE) -- see
        // that file's header comment for what's verified and how.
        if (!memcmp(d,"BNTX",4)) return make_info(NFMT_BNTX,false,false,0);
        // GFA: Good-Feel archive (Wario Land: Shake It!, Kirby's Epic Yarn)
        if (!memcmp(d,"GFAC",4)) return make_info(NFMT_GFA,false,true,0);
        // BCH: the 3DS CTR H3D container. Its magic is "BCH\0" -- it is a
        // different format from CGFX/BCRES, not a variant of it.
        if (!memcmp(d,"BCH\0",4)) return make_info(NFMT_BCH,false,false,0);
        // PAC: Brawl's flat archive ("ARC\0" magic, per BrawlLib's
        // ARCHeader.Tag). Uncompressed, no name table.
        if (!memcmp(d,"ARC\0",4)) return make_info(NFMT_PAC,false,false,0);

        // RNC (Rob Northen Compression, "RNC" + version 1..3) and PSDK
        // (Prosonic data, "PSDK") appear on GBA/DS homebrew and some
        // devkit-built payloads.  No decoder is implemented for either; they
        // are recognized (not guessed) so extraction can produce a clear
        // "unsupported codec" message instead of a random mis-parse.
        if ( size >= 4 && !memcmp(d,"RNC",3) && d[3] >= 1 && d[3] <= 3 )
            return make_info(NFMT_RNC,true,true,0);
        if ( size >= 4 && !memcmp(d,"PSDK",4) )
            return make_info(NFMT_PSDK,false,true,0);

        // Strong footer magics must be tested BEFORE the single-byte
        // compression heuristics below. BFLIM/BCLIM keep their magic in a
        // trailer, so their *payload* starts at offset 0 -- and compressed
        // texture data very often begins with 0x10/0x11/0x24/0x28/0x30/0x40,
        // exactly the bytes those heuristics key on. Testing the heuristics
        // first silently stole real BFLIMs (13 of 689 in a real corpus) and
        // reported them as LZ10/LZ11/LZH8 streams.
        if ( size >= 0x28 && !memcmp(d+size-0x28,"FLIM",4) )
            return make_info(NFMT_BFLIM,true,false,0);
        if ( size >= 0x28 && !memcmp(d+size-0x28,"CLIM",4) )
            return make_info(NFMT_BCLIM,true,false,0);

        // QuickLZ is checked before the single-byte heuristics: its test is
        // exact (the header's own recorded compressed length must equal the
        // buffer) whereas the tests below are one-byte guesses.
        if (IsQuickLZ(d,size))
            return make_info(NFMT_QLZ,false,true,0);
        if ( (d[0] == 0x10 || d[0] == 0x11) && size >= 4 )
            return make_info(d[0] == 0x10 ? NFMT_LZ10 : NFMT_LZ11, false, true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
        if ( (d[0] == 0x24 || d[0] == 0x28) && size >= 5 )
            return make_info(d[0] == 0x24 ? NFMT_HUFF4 : NFMT_HUFF8, false, true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
        if ( d[0] == 0x30 && size >= 4 )
            return make_info(NFMT_RL,false,true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
        // LZH8: 0x40 followed by a 24-bit LE size.  WarioWare Snapped wraps
        // the stream in a 4-byte LE size prefix, so 0x40 may sit at offset 4.
        if ( d[0] == 0x40 && size >= 4 )
            return make_info(NFMT_LZH8,false,true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
        if ( d[0] != 0x40 && size >= 8 && d[4] == 0x40 )
            return make_info(NFMT_LZH8,false,true,
                (u32)d[5] | (u32)d[6]<<8 | (u32)d[7]<<16 );
        // Camelot header: codec 1/2 plus a three-byte output size. The extension
        // check prevents random binary files from being called STPL.
        if ( (d[0] == 1 || d[0] == 2) && filename && strstr(filename,".stpl") )
            return make_info(NFMT_STPL,true,true,((u32)d[1]<<16)|((u32)d[2]<<8)|d[3]);
    }
    if ( size >= 0x28 && !memcmp(d+size-0x28,"FLIM",4) ) return make_info(NFMT_BFLIM,true,false,0);
    if ( size >= 0x28 && !memcmp(d+size-0x28,"CLIM",4) ) return make_info(NFMT_BCLIM,true,false,0);
    return make_info(NFMT_UNKNOWN,true,false,0);
}

static enumError alloc_output ( u8 **dest, uint *dest_size, u32 size )
{
    if ( !dest || !dest_size || !size || size > NFMT_MAX_OUTPUT ) return EFBIG;
    *dest = MALLOC(size);
    if (!*dest) return ERR_CANT_CREATE;
    *dest_size = size;
    return ERR_OK;
}

typedef struct ash_bits_t
{
    const u8 *src;
    uint size, pos, word, used;
}
ash_bits_t;

static bool ash_feed ( ash_bits_t *br )
{
    if (br->pos > br->size-4) return false;
    br->word = rd_be32(br->src+br->pos);
    br->pos += 4;
    br->used = 0;
    return true;
}

static bool ash_init ( ash_bits_t *br, const u8 *src, uint size, uint pos )
{
    if (!br || pos > size) return false;
    br->src = src; br->size = size; br->pos = pos; br->word = br->used = 0;
    return ash_feed(br);
}

static bool ash_read ( ash_bits_t *br, uint n, uint *value )
{
    if (!n || n > 24 || !value) return false;
    uint val = 0;
    while (n--)
    {
        val = val<<1 | br->word>>31;
        if (++br->used == 32)
        {
            if (!ash_feed(br)) return false;
        }
        else
            br->word <<= 1;
    }
    *value = val;
    return true;
}

static bool ash_tree
(
    ash_bits_t *br, uint width, uint *left, uint *right, uint *root
)
{
    const uint max = 1u << width, cap = 2*max-1;
    uint work[2*2048], work_used = 0, nodes = 0, next = max;
    for (;;)
    {
        uint bit, value;
        if (!ash_read(br,1,&bit)) return false;
        if (bit)
        {
            if (work_used+2 > sizeof(work)/sizeof(*work) || next >= cap)
                return false;
            work[work_used++] = next | 0x80000000u;
            work[work_used++] = next | 0x40000000u;
            nodes += 2; next++;
            continue;
        }
        if (!ash_read(br,width,&value) || value >= max) return false;
        *root = value;
        while (nodes)
        {
            const uint node = work[--work_used], index = node & 0x3fffffffu;
            if (index >= cap) return false;
            nodes--;
            if (node & 0x80000000u)
                { right[index] = *root; *root = index; }
            else
                { left[index] = *root; break; }
        }
        if (!nodes) return true;
    }
}

static bool ash_symbol
(
    ash_bits_t *br, uint root, uint max, const uint *left, const uint *right,
    uint *value
)
{
    uint sym = root;
    while (sym >= max)
    {
        uint bit;
        if (sym >= 2*max-1 || !ash_read(br,1,&bit)) return false;
        sym = bit ? right[sym] : left[sym];
    }
    *value = sym;
    return true;
}

enumError DecodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || src_size < 0x10 || memcmp(src,"ASH0",4)) return EINVAL;
    const uint out_size = rd_be32(src+4) & 0x00ffffff;
    const uint dist_start = rd_be32(src+8);
    if (!out_size || out_size > NFMT_MAX_OUTPUT || dist_start > src_size-4)
        return EINVAL;
    enumError err = alloc_output(dest,dest_size,out_size);
    if (err) return err;
    ash_bits_t syms, dists;
    const uint sym_max = 1u<<9, dist_max = 1u<<11;
    uint *sl = CALLOC(2*sym_max-1,sizeof(*sl)), *sr = CALLOC(2*sym_max-1,sizeof(*sr));
    uint *dl = CALLOC(2*dist_max-1,sizeof(*dl)), *dr = CALLOC(2*dist_max-1,sizeof(*dr));
    uint sym_root = 0, dist_root = 0;
    if (!sl || !sr || !dl || !dr || !ash_init(&syms,src,src_size,0x0c)
        || !ash_init(&dists,src,src_size,dist_start)
        || !ash_tree(&syms,9,sl,sr,&sym_root) || !ash_tree(&dists,11,dl,dr,&dist_root))
        goto invalid;
    for (uint pos = 0; pos < out_size; )
    {
        uint sym;
        if (!ash_symbol(&syms,sym_root,sym_max,sl,sr,&sym)) goto invalid;
        if (sym < 0x100)
            (*dest)[pos++] = sym;
        else
        {
            uint distance;
            const uint len = sym - 0x100 + 3;
            if (!ash_symbol(&dists,dist_root,dist_max,dl,dr,&distance)
                || distance >= pos || len > out_size-pos)
                goto invalid;
            for (uint n = 0; n < len; n++) (*dest)[pos+n] = (*dest)[pos-distance-1+n];
            pos += len;
        }
    }
    FREE(sl); FREE(sr); FREE(dl); FREE(dr);
    return ERR_OK;
invalid:
    FREE(sl); FREE(sr); FREE(dl); FREE(dr); FREE(*dest); *dest = 0; *dest_size = 0;
    return EINVAL;
}

typedef struct ash_writer_t { u8 *data; uint size, bitpos; } ash_writer_t;

static bool ash_write ( ash_writer_t *bw, uint value, uint n )
{
    if (!n || n > 24 || bw->bitpos > bw->size*8-n) return false;
    while (n--)
    {
        if (value & (1u<<n)) bw->data[bw->bitpos/8] |= 0x80 >> (bw->bitpos&7);
        bw->bitpos++;
    }
    return true;
}

static bool ash_write_symbol_tree ( ash_writer_t *bw, uint depth, uint value )
{
    if (depth == 9)
        return ash_write(bw,0,1) && ash_write(bw,value,9);
    return ash_write(bw,1,1)
        && ash_write_symbol_tree(bw,depth+1,value<<1)
        && ash_write_symbol_tree(bw,depth+1,value<<1|1);
}

enumError EncodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    // A full 9-bit literal tree keeps this initial encoder simple and fully
    // interoperable. A future optimiser can replace it with a frequency tree
    // without changing the decoder or on-disk framing.
    if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
        return EINVAL;
    const u64 sym_bits = 5631ull + 9ull*src_size;
    const u64 sym_size = (sym_bits+7)/8, dist_off = 12 + ((sym_size+3)&~3ull);
    const u64 total = dist_off + 4;
    if (total > NFMT_MAX_OUTPUT || total > UINT_MAX) return EFBIG;
    u8 *out = CALLOC(1,total);
    if (!out) return ERR_CANT_CREATE;
    memcpy(out,"ASH0",4);
    wr_be32(out+4,src_size); wr_be32(out+8,dist_off);
    ash_writer_t bw = { out+12, (uint)sym_size, 0 };
    if (!ash_write_symbol_tree(&bw,0,0)) goto invalid_ash_encode;
    for (uint i = 0; i < src_size; i++)
        if (!ash_write(&bw,src[i],9)) goto invalid_ash_encode;
    bw.data = out + dist_off; bw.size = 4; bw.bitpos = 0;
    if (!ash_write(&bw,0,1) || !ash_write(&bw,0,11)) goto invalid_ash_encode;
    *dest = out; *dest_size = total;
    return ERR_OK;
invalid_ash_encode:
    FREE(out);
    return EFBIG;
}

enumError DecodeCamelot ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || src_size < 5 || (src[0] != 1 && src[0] != 2)) return EINVAL;
    const u32 out_len = ((u32)src[1]<<16)|((u32)src[2]<<8)|src[3];
    enumError err = alloc_output(dest,dest_size,out_len);
    if (err) return err;
    uint sp = 4, dp = 0;
    while ( sp < src_size && dp < out_len )
    {
        const u8 flags = src[sp++];
        for ( uint bit = 0; bit < 8 && dp < out_len; bit++ )
        {
            if ( flags & (0x80 >> bit) )
            {
                if (sp + 2 > src_size) goto invalid;
                const u8 a = src[sp++], b = src[sp++];
                const uint back = ((uint)(a >> 4) << 8) | b;
                uint len = a & 15;
                if (!len)
                {
                    if (sp >= src_size) goto invalid;
                    len = src[sp++] + 17;
                }
                else
                    len++;
                if (!back || len > out_len - dp) goto invalid;

		// Camelot's window is zero-filled before the first output byte.
		// Early references in real STPL headers deliberately use that area.
                while (len--)
                {
                    (*dest)[dp] = back <= dp ? (*dest)[dp-back] : 0;
                    dp++;
                }
            }
            else
            {
                if (sp >= src_size) goto invalid;
                (*dest)[dp++] = src[sp++];
            }
        }
    }
    if (dp == out_len)
        return ERR_OK;
invalid:
    FREE(*dest); *dest = 0; *dest_size = 0; return EINVAL;
}

//-----------------------------------------------------------------------------
// RNC (Rob Northen Compression) decoder, RNC1/RNC2 methods.
//
// Faithful port of the unpack paths from the decompiled RNC ProPack tool
// (github.com/lab313ru/rnc_propack_source; verbatim mirror used as reference:
// huderlem/carrotcrazy/tools/rnc.c).  Layout of the 18-byte header:
//
//   +0x00  "RNC" + method byte (1=RNC1, 2=RNC2)
//   +0x04  BE32 unpacked size
//   +0x08  BE32 packed size (bytes of compressed data after this header)
//   +0x0C  BE16 CRC16 of the unpacked data
//   +0x0E  BE16 CRC16 of the packed data
//   +0x10  byte leeway, +0x11 byte chunk count
//   +0x12  start of the compressed stream
//
// The two flag bits at the head of the stream select the decode method and
// signal encryption; encrypted streams need a key we do not carry, so they
// are rejected before any output is touched.

static const u16 rnc_crc_table[] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

static const u8 rnc_match_count_bits[] = { 0x00, 0x0E, 0x08, 0x0A, 0x12, 0x13, 0x16 };
static const u8 rnc_match_count_nbits[] = { 0, 4, 4, 4, 5, 5, 5 };
static const u8 rnc_match_offset_bits[] = { 0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F, 0x28, 0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D };
static const u8 rnc_match_offset_nbits[] = { 1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6 };

typedef struct rnc_huftable_t
{
    u32 l1, l3;
    u16 l2;
    u16 bit_depth;
}
rnc_huftable_t;

typedef struct rnc_state_t
{
    const u8 *src;
    uint src_size;
    uint in_pos, processed, input_size, dict_size;
    u16 match_count, match_offset, bit_count, unpacked_crc_real;
    u32 bit_buffer;
    u8 *mem1, *decoded, *window, *pack_block;
    u8 *out;
}
rnc_state_t;

static u16 rnc_rotate_key ( u16 x )
{
    return (x & 1) ? (u16)(0x8000 | (x >> 1)) : (u16)(x >> 1);
}

static u8 rnc_read_source ( rnc_state_t *v )
{
    if ( v->pack_block == &v->mem1[0xFFFD] )
    {
        int left = (int)v->src_size - (int)v->in_pos;
        int n;
        if (left <= 0xFFFD) n = left; else n = 0xFFFD;
        v->pack_block = v->mem1;
        memcpy(v->pack_block, v->src + v->in_pos, n);
        v->in_pos += n;
        if (left - n > 2) left = 2; else left -= n;
        memcpy(v->pack_block + n, v->src + v->in_pos, left);
    }
    return *v->pack_block++;
}

static u32 rnc_input_bits_m2 ( rnc_state_t *v, int count )
{
    u32 bits = 0;
    while ( count-- > 0 )
    {
        if ( !v->bit_count )
        {
            v->bit_buffer = rnc_read_source(v);
            v->bit_count = 8;
        }
        bits <<= 1;
        if ( v->bit_buffer & 0x80 ) bits |= 1;
        v->bit_buffer <<= 1;
        v->bit_count--;
    }
    return bits;
}

static u32 rnc_input_bits_m1 ( rnc_state_t *v, int count )
{
    u32 bits = 0, prev_bits = 1;
    while ( count-- > 0 )
    {
        if ( !v->bit_count )
        {
            const u8 b1 = rnc_read_source(v), b2 = rnc_read_source(v);
            v->bit_buffer = ((u32)v->pack_block[1] << 24) | ((u32)v->pack_block[0] << 16)
                          | ((u32)b2 << 8) | b1;
            v->bit_count = 16;
        }
        if ( v->bit_buffer & 1 ) bits |= prev_bits;
        v->bit_buffer >>= 1;
        prev_bits <<= 1;
        v->bit_count--;
    }
    return bits;
}

static void rnc_write_decoded ( rnc_state_t *v, u8 b )
{
    if ( v->window == &v->decoded[0xFFFF] )
    {
        memcpy(v->out, &v->decoded[v->dict_size], 0xFFFF - v->dict_size);
        v->out += 0xFFFF - v->dict_size;
        memmove(v->decoded, &v->window[-(int)v->dict_size], v->dict_size);
        v->window = &v->decoded[v->dict_size];
    }
    *v->window++ = b;
    v->unpacked_crc_real = rnc_crc_table[(v->unpacked_crc_real ^ b) & 0xFF]
                         ^ (v->unpacked_crc_real >> 8);
}

static void rnc_decode_match_offset ( rnc_state_t *v )
{
    v->match_offset = 0;
    if ( rnc_input_bits_m2(v,1) )
    {
        v->match_offset = rnc_input_bits_m2(v,1);
        if ( rnc_input_bits_m2(v,1) )
        {
            v->match_offset = ((v->match_offset << 1) | rnc_input_bits_m2(v,1)) | 4;
            if ( !rnc_input_bits_m2(v,1) )
                v->match_offset = (v->match_offset << 1) | rnc_input_bits_m2(v,1);
        }
        else if ( !v->match_offset )
            v->match_offset = rnc_input_bits_m2(v,1) + 2;
    }
    v->match_offset = ((v->match_offset << 8) | rnc_read_source(v)) + 1;
}

static void rnc_decode_match_count ( rnc_state_t *v )
{
    v->match_count = rnc_input_bits_m2(v,1) + 4;
    if ( rnc_input_bits_m2(v,1) )
        v->match_count = ((v->match_count - 1) << 1) + rnc_input_bits_m2(v,1);
}

static void rnc_container_match ( rnc_state_t *v )
{
    const uint count = v->match_count;
    v->processed += count;
    uint i = count;
    while ( i-- > 0 )
        rnc_write_decoded(v, v->window[-v->match_offset]);
}

static void rnc_unpack_data_m2 ( rnc_state_t *v )
{
    while ( v->processed < v->input_size )
    {
        for (;;)
        {
            if ( !rnc_input_bits_m2(v,1) )
            {
                rnc_write_decoded(v, rnc_read_source(v));
                v->processed++;
            }
            else
            {
                if ( rnc_input_bits_m2(v,1) )
                {
                    if ( rnc_input_bits_m2(v,1) )
                    {
                        if ( rnc_input_bits_m2(v,1) )
                        {
                            v->match_count = rnc_read_source(v) + 8;
                            if ( v->match_count == 8 )
                            {
                                rnc_input_bits_m2(v,1);
                                break;
                            }
                        }
                        else
                            v->match_count = 3;
                        rnc_decode_match_offset(v);
                    }
                    else
                    {
                        v->match_count = 2;
                        v->match_offset = rnc_read_source(v) + 1;
                    }
                    rnc_container_match(v);
                }
                else
                {
                    rnc_decode_match_count(v);
                    if ( v->match_count != 9 )
                    {
                        rnc_decode_match_offset(v);
                        rnc_container_match(v);
                    }
                    else
                    {
                        uint data_length = (rnc_input_bits_m2(v,4) << 2) + 12;
                        v->processed += data_length;
                        while ( data_length-- > 0 )
                            rnc_write_decoded(v, rnc_read_source(v));
                    }
                }
            }
        }
    }
}

static void rnc_clear_table ( rnc_huftable_t *t, int count )
{
    for ( int i = 0; i < count; i++ )
    {
        t[i].l1 = 0; t[i].l2 = 0xFFFF; t[i].l3 = 0; t[i].bit_depth = 0;
    }
}

static u32 rnc_inverse_bits ( u32 value, int count )
{
    u32 out = 0;
    while ( count-- > 0 )
    {
        out <<= 1;
        if ( value & 1 ) out |= 1;
        value >>= 1;
    }
    return out;
}

static void rnc_proc_20 ( rnc_huftable_t *t, int count )
{
    u32 val = 0, div = 0x80000000;
    int depth = 1;
    while ( depth <= 16 )
    {
        for ( int i = 0; i < count; i++ )
        {
            if ( t[i].bit_depth == depth )
            {
                t[i].l3 = rnc_inverse_bits(val / div, depth);
                val += div;
            }
        }
        depth++;
        div >>= 1;
    }
}

static void rnc_make_huftable ( rnc_state_t *v, rnc_huftable_t *t, int count )
{
    rnc_clear_table(t, count);
    int leaf_nodes = (int)rnc_input_bits_m1(v,5);
    if ( leaf_nodes )
    {
        if ( leaf_nodes > 16 ) leaf_nodes = 16;
        for ( int i = 0; i < leaf_nodes; i++ )
            t[i].bit_depth = (u16)rnc_input_bits_m1(v,4);
        rnc_proc_20(t, leaf_nodes);
    }
}

static u32 rnc_decode_table_data ( rnc_state_t *v, rnc_huftable_t *t )
{
    for ( u32 i = 0; ; i++ )
    {
        if ( t[i].bit_depth
          && t[i].l3 == (v->bit_buffer & ((1u << t[i].bit_depth) - 1)) )
        {
            rnc_input_bits_m1(v, t[i].bit_depth);
            if ( i < 2 ) return i;
            return rnc_input_bits_m1(v, i - 1) | (1u << (i - 1));
        }
    }
}

static void rnc_unpack_data_m1 ( rnc_state_t *v )
{
    rnc_huftable_t raw[16], len[16], pos[16];
    while ( v->processed < v->input_size )
    {
        rnc_make_huftable(v, raw,16);
        rnc_make_huftable(v, len,16);
        rnc_make_huftable(v, pos,16);
        int subchunks = (int)rnc_input_bits_m1(v,16);
        while ( subchunks-- > 0 )
        {
            uint data_length = rnc_decode_table_data(v, raw);
            v->processed += data_length;
            if ( data_length )
            {
                while ( data_length-- > 0 )
                    rnc_write_decoded(v, rnc_read_source(v));
                v->bit_buffer =
                    ((((u32)v->pack_block[2] << 16) | ((u32)v->pack_block[1] << 8)
                        | v->pack_block[0]) << v->bit_count)
                  | (v->bit_buffer & ((1u << v->bit_count) - 1));
            }
            if ( subchunks )
            {
                v->match_offset = rnc_decode_table_data(v, len) + 1;
                v->match_count = rnc_decode_table_data(v, pos) + 2;
                rnc_container_match(v);
            }
        }
    }
}

enumError DecodeRNC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if ( !dest || !dest_size )
        return ERR_SEMANTIC;
    *dest = 0; *dest_size = 0;
    if ( !src || src_size < 0x14 || memcmp(src,"RNC",3) ) return EINVAL;

    const u8 method = src[3];
    if ( method != 1 && method != 2 ) return EINVAL;

    const u32 input_size = rd_be32(src+0x04);
    const u32 packed_size = rd_be32(src+0x08);
    if ( !input_size || input_size > NFMT_MAX_OUTPUT ) return EFBIG;
    if ( packed_size > src_size - 0x12 ) return EINVAL;

    u16 packed_crc = 0;
    for ( u32 i = 0; i < packed_size; i++ )
        packed_crc = rnc_crc_table[(packed_crc ^ src[0x12+i]) & 0xFF]
                    ^ (packed_crc >> 8);
    if ( packed_crc != rd_be16(src+0x0E) ) return EINVAL;

    enumError err = alloc_output(dest,dest_size,input_size);
    if (err) return err;

    rnc_state_t st;
    memset(&st,0,sizeof(st));
    st.src = src; st.src_size = src_size; st.in_pos = 0x12;
    st.input_size = input_size;
    st.dict_size = method == 1 ? 0x8000 : 0x1000;
    st.out = *dest;
    st.mem1 = MALLOC(0xFFFF+4);
    st.decoded = MALLOC(0xFFFF+4);
    if ( !st.mem1 || !st.decoded )
    {
        FREE(st.mem1); FREE(st.decoded);
        FREE(*dest); *dest = 0; *dest_size = 0;
        return ERR_CANT_CREATE;
    }
    st.pack_block = &st.mem1[0xFFFD];
    st.window = &st.decoded[st.dict_size];

    if ( method == 1 )
    {
        // flags: locked? + keyed?
        rnc_input_bits_m1(&st,1);
        if ( rnc_input_bits_m1(&st,1) )
        {
            FREE(st.mem1); FREE(st.decoded); FREE(*dest);
            *dest = 0; *dest_size = 0; return EINVAL;
        }
        rnc_unpack_data_m1(&st);
    }
    else
    {
        rnc_input_bits_m2(&st,1);
        if ( rnc_input_bits_m2(&st,1) )
        {
            FREE(st.mem1); FREE(st.decoded); FREE(*dest);
            *dest = 0; *dest_size = 0; return EINVAL;
        }
        rnc_unpack_data_m2(&st);
    }

    memcpy(st.out, &st.decoded[st.dict_size], st.window - &st.decoded[st.dict_size]);
    st.out += st.window - &st.decoded[st.dict_size];

    FREE(st.mem1); FREE(st.decoded);

    if ( st.unpacked_crc_real != rd_be16(src+0x0C) || st.out - *dest != input_size )
    {
        FREE(*dest); *dest = 0; *dest_size = 0; return EINVAL;
    }

    return ERR_OK;
}

enumError DecodeLZ10LZ11 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || src_size < 4 || (src[0] != 0x10 && src[0] != 0x11)) return EINVAL;
    const bool lz11 = src[0] == 0x11;
    const u32 out_len = (u32)src[1] | (u32)src[2]<<8 | (u32)src[3]<<16;
    enumError err = alloc_output(dest,dest_size,out_len); if (err) return err;
    uint sp=4, dp=0;
    while (dp < out_len)
    {
        if (sp >= src_size) goto invalid;
        u8 flags=src[sp++];
        for (uint bit=0; bit<8 && dp<out_len; bit++,flags<<=1)
            if (!(flags&0x80)) { if(sp>=src_size) goto invalid; (*dest)[dp++]=src[sp++]; }
            else {
                if (sp+2>src_size) goto invalid;
                u8 a=src[sp++], b=src[sp++]; uint len, back;
                if (!lz11) { len=(a>>4)+3; back=((a&15)<<8|b)+1; }
                else if (a>>4==0) { if(sp>=src_size) goto invalid; len=((a&15)<<4 | b>>4)+0x11; back=((b&15)<<8|src[sp++])+1; }
                else if (a>>4==1) { if(sp+2>src_size) goto invalid; len=((a&15)<<12 | b<<4 | src[sp]>>4)+0x111; const u8 c=src[sp++], d=src[sp++]; back=((c&15)<<8|d)+1; }
                else { len=(a>>4)+1; back=((a&15)<<8|b)+1; }
                if (back>dp || len>out_len-dp) goto invalid;
                while(len--) (*dest)[dp]=(*dest)[dp-back],dp++;
            }
    }
    return ERR_OK;
invalid: FREE(*dest); *dest=0; *dest_size=0; return EINVAL;
}

enumError EncodeLZ10LZ11
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11
)
{
    if (!dest || !dest_size || !src || !src_size || src_size > 0xffffff)
        return EINVAL;

    // Worst case is one flag byte per eight literals plus the four-byte
    // header.  A little extra also covers the final, partial group.
    const uint capacity = 4 + src_size + (src_size+7)/8;
    u8 *out = MALLOC(capacity);
    if (!out) return ERR_CANT_CREATE;
    out[0] = lz11 ? 0x11 : 0x10;
    out[1] = src_size;
    out[2] = src_size >> 8;
    out[3] = src_size >> 16;

    uint sp = 0, dp = 4;
    while (sp < src_size)
    {
        const uint flags_pos = dp++;
        u8 flags = 0;
        for (uint bit = 0; bit < 8 && sp < src_size; bit++)
        {
            uint best_len = 0, best_back = 0;
            const uint max_back = sp < 0x1000 ? sp : 0x1000;
            const uint max_len = lz11
                ? (src_size-sp < 16 ? src_size-sp : 16)
                : (src_size-sp < 18 ? src_size-sp : 18);
            // A backwards search is deliberately used: nearby matches tend
            // to give the same compact stream as Nintendo's common tools.
            for (uint back = 1; back <= max_back; back++)
            {
                uint len = 0;
                while (len < max_len && src[sp+len] == src[sp-back+len])
                    len++;
                if (len > best_len)
                {
                    best_len = len;
                    best_back = back;
                    if (len == max_len) break;
                }
            }
            if (best_len >= 3)
            {
                flags |= 0x80 >> bit;
                const uint disp = best_back - 1;
                if (lz11)
                {
                    // The regular LZ11 token represents lengths 3..16.
                    out[dp++] = (best_len-1) << 4 | (disp >> 8);
                    out[dp++] = disp;
                }
                else
                {
                    out[dp++] = (best_len-3) << 4 | (disp >> 8);
                    out[dp++] = disp;
                }
                sp += best_len;
            }
            else
                out[dp++] = src[sp++];
        }
        out[flags_pos] = flags;
    }
    *dest = out;
    *dest_size = dp;
    return ERR_OK;
}

enumError EncodeLZ10Raw ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    u8 *lz10 = 0;
    uint lz10_size = 0;
    enumError err = EncodeLZ10LZ11(&lz10, &lz10_size, src, src_size, false);
    if (!err)
    {
        if (lz10_size > 4)
        {
            *dest_size = lz10_size - 4;
            *dest = MALLOC(*dest_size);
            if (*dest) memcpy(*dest, lz10+4, *dest_size);
            else err = ERR_CANT_CREATE;
        }
        else err = ERR_INVALID_DATA;
        FREE(lz10);
    }
    return err;
}

enumError DecodeNintendoHuff ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!dest || !dest_size || !src || src_size < 9 || (src[0] != 0x24 && src[0] != 0x28))
        return EINVAL;
    const bool four_bit = src[0] == 0x24;
    u32 out_size = (u32)src[1] | (u32)src[2]<<8 | (u32)src[3]<<16;
    uint tree_off = 4;
    if (!out_size)
    {
        if (src_size < 13) return EINVAL;
        out_size = rd_le32(src+4);
        tree_off = 8;
    }
    const uint tree_size = 2u*(src[tree_off]+1);
    const uint tree_base = tree_off+1;
    if (!out_size || tree_size > src_size-tree_base || src_size-(tree_base+tree_size) < 4)
        return EINVAL;
    enumError err = alloc_output(dest,dest_size,out_size);
    if (err) return err;
    const u8 *tree = src+tree_base;
    const u8 *bits = tree+tree_size;
    uint bits_pos = 0, bits_left = 0, out_pos = 0;
    u32 word = 0;
    int half = -1;
    while (out_pos < out_size)
    {
        uint node = 0;
        u8 symbol = 0;
        for (;;)
        {
            if (node >= tree_size) { FREE(*dest); *dest = 0; return EINVAL; }
            if (!bits_left)
            {
                if (bits_pos > src_size-(bits-tree)-4) { FREE(*dest); *dest = 0; return EINVAL; }
                word = rd_le32(bits+bits_pos); bits_pos += 4; bits_left = 32;
            }
            const bool bit = (word >> (bits_left-1)) & 1;
            bits_left--;
            const u8 entry = tree[node];
            const uint child = (node & ~1u) + 2 + 2*(entry & 0x3f) + (bit ? 1 : 0);
            if (child >= tree_size) { FREE(*dest); *dest = 0; return EINVAL; }
            if (entry & (bit ? 0x40 : 0x80)) { symbol = tree[child]; break; }
            node = child;
        }
        if (!four_bit)
            (*dest)[out_pos++] = symbol;
        else if (half < 0)
            half = symbol << 4;
        else
        {
            (*dest)[out_pos++] = half | (symbol & 15);
            half = -1;
        }
    }
    if (half >= 0) { FREE(*dest); *dest = 0; return EINVAL; }
    return ERR_OK;
}

enumError DecodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || src_size < 4 || src[0] != 0x30) return EINVAL;
    const uint out_size = (uint)src[1] | (uint)src[2]<<8 | (uint)src[3]<<16;
    enumError err = alloc_output(dest,dest_size,out_size);
    if (err) return err;
    uint sp = 4, dp = 0;
    while (dp < out_size)
    {
        if (sp >= src_size) goto invalid_rl;
        const u8 control = src[sp++];
        const uint len = (control & 0x7f) + (control >> 7 ? 3 : 1);
        if (len > out_size-dp || sp + (control>>7 ? 1 : len) > src_size) goto invalid_rl;
        if (control >> 7) memset(*dest+dp,src[sp++],len);
        else { memcpy(*dest+dp,src+sp,len); sp += len; }
        dp += len;
    }
    return ERR_OK;
invalid_rl: FREE(*dest); *dest = 0; *dest_size = 0; return EINVAL;
}

enumError EncodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || !src_size || src_size > 0xffffff || !dest || !dest_size) return EINVAL;
    const uint cap = src_size + (src_size+127)/128 + 4;
    u8 *out = MALLOC(cap);
    if (!out) return ERR_CANT_CREATE;
    out[0] = 0x30; out[1] = src_size; out[2] = src_size>>8; out[3] = src_size>>16;
    uint sp = 0, dp = 4;
    while (sp < src_size)
    {
        uint run = 1;
        while (run < 130 && sp+run < src_size && src[sp+run] == src[sp]) run++;
        if (run >= 3) { out[dp++] = 0x80 | (run-3); out[dp++] = src[sp]; sp += run; continue; }
        const uint start = sp++;
        while (sp-start < 128 && sp < src_size)
        {
            run = 1;
            while (run < 3 && sp+run < src_size && src[sp+run] == src[sp]) run++;
            if (run >= 3) break;
            sp++;
        }
        const uint len = sp-start;
        out[dp++] = len-1; memcpy(out+dp,src+start,len); dp += len;
    }
    *dest = out; *dest_size = dp;
    return ERR_OK;
}

// BLZ ("backward LZSS"): used to compress a DS ROM's ARM9/ARM7 executable
// and overlay files, ported from CUE's reference blz.c
// (github.com/PeterLemon/Nintendo_DS_Compressors). Unlike this file's other
// LZ variants it has no magic/header at the *start* -- everything needed to
// decode is an 8-11 byte footer at the *end*, and the compressed span is
// itself byte-reversed (encode walks the source backward, building matches
// against what -- once reversed back -- reads as ordinary forward LZSS with
// a min match length of 3 stored as len-3 and a 12-bit back-reference).
// This means BLZ can't be identified by a header-magic table lookup the way
// the rest of DetectNintendoFormat() works: any file could coincidentally
// have a plausible-looking footer, so this decoder is only invoked where
// the caller already has other context that a file might be BLZ (an
// ndstool-staged arm9.bin/arm7.bin/overlay), not from generic dispatch.
enumError DecodeBLZ ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if ( !src || src_size < 4 ) return EINVAL;

    const u32 inc_len = rd_le32(src+src_size-4);
    if ( !inc_len )
    {
	// "not coded" marker: BLZ_Encode() writes this when compression
	// would have made the file bigger. Confirmed against the real
	// reference decoder rather than assumed: despite what the encoder
	// side suggests, "decoding" this case reproduces the *entire*
	// input verbatim, trailing 4-byte zero marker included, not the
	// marker-stripped plain content -- checked with `blz -d` on a
	// deliberately incompressible sample and diffed byte-for-byte.
	enumError err = alloc_output(dest,dest_size,src_size);
	if (err) return err;
	memcpy(*dest,src,src_size);
	return ERR_OK;
    }

    if ( src_size < 8 ) return EINVAL;
    const uint hdr_len = src[src_size-5];
    if ( hdr_len < 8 || hdr_len > 11 || src_size <= hdr_len ) return EINVAL;

    const u32 enc_len = rd_le32(src+src_size-8) & 0x00FFFFFF;
    if ( enc_len > src_size || enc_len < hdr_len ) return EINVAL;
    const u32 dec_len = (u32)src_size - enc_len;	// leading plain span
    const u32 pak_len = enc_len - hdr_len;		// compressed span
    if ( dec_len + pak_len > src_size ) return EINVAL;

    const u64 raw_len64 = (u64)dec_len + enc_len + inc_len;
    if ( raw_len64 > 64*1024*1024 ) return EINVAL;	// sanity cap
    const u32 raw_len = (u32)raw_len64;

    enumError err = alloc_output(dest,dest_size,raw_len);
    if (err) return err;
    u8 *raw = *dest;

    // Leading dec_len bytes are stored verbatim (not part of the
    // compressed span at all).
    memcpy(raw,src,dec_len);

    // Reverse a private copy of the compressed span so ordinary
    // forward-reading LZSS logic reproduces BLZ_Encode()'s backward walk.
    u8 *rev = MALLOC(pak_len?pak_len:1);
    for ( u32 i = 0; i < pak_len; i++ )
	rev[i] = src[dec_len+pak_len-1-i];

    u32 rp = 0, dp = dec_len;
    u8 flags = 0, mask = 0;
    bool bad = false;
    while ( dp < raw_len )
    {
	if ( !(mask >>= 1) )
	{
	    if ( rp >= pak_len ) break;
	    flags = rev[rp++];
	    mask = 0x80;
	}
	if ( !(flags & mask) )
	{
	    if ( rp >= pak_len ) { bad = true; break; }
	    raw[dp++] = rev[rp++];
	}
	else
	{
	    if ( rp+1 >= pak_len ) { bad = true; break; }
	    uint pos = (uint)rev[rp]<<8 | rev[rp+1]; rp += 2;
	    uint len = (pos>>12) + 3;
	    uint back = (pos&0xFFF) + 3;
	    if ( back > dp-dec_len || dp+len > raw_len ) { bad = true; break; }
	    while (len--) { raw[dp] = raw[dp-back]; dp++; }
	}
    }
    FREE(rev);

    if ( bad || dp != raw_len )
    {
	FREE(*dest); *dest = 0; *dest_size = 0;
	return EINVAL;
    }

    // Un-reverse the newly-decoded tail back to normal forward order (the
    // leading dec_len verbatim span was never reversed and stays as-is).
    for ( u32 i = dec_len, j = raw_len-1; i < j; i++, j-- )
    {
	u8 t = raw[i]; raw[i] = raw[j]; raw[j] = t;
    }

    return ERR_OK;
}

enumError DecodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!src || src_size < 16 || memcmp(src,"Yay0",4)) return EINVAL;
    const u32 out_len=rd_be32(src+4), link=rd_be32(src+8), chunk=rd_be32(src+12);
    enumError err=alloc_output(dest,dest_size,out_len); if(err) return err;
    uint mask=16, lp=link, cp=chunk, dp=0, bits=0; u32 code=0;
    while(dp<out_len) {
        if(!bits) { if(mask+4>src_size) goto invalid; code=rd_be32(src+mask); mask+=4; bits=32; }
        if(code&0x80000000) { if(cp>=src_size) goto invalid; (*dest)[dp++]=src[cp++]; }
        else { if(lp+2>src_size) goto invalid; u16 v=(u16)src[lp]<<8|src[lp+1]; lp+=2; uint len=v>>12, back=(v&0xfff)+1; if(!len) { if(cp>=src_size) goto invalid; len=src[cp++]+18; } else len+=2; if(back>dp || len>out_len-dp) goto invalid; while(len--) (*dest)[dp]=(*dest)[dp-back],dp++; }
        code<<=1; bits--;
    }
    return ERR_OK;
invalid: FREE(*dest); *dest=0; *dest_size=0; return EINVAL;
}

enumError EncodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!dest || !dest_size || !src || !src_size || src_size > UINT_MAX/2)
        return EINVAL;
    const uint max_masks = (src_size + 31) / 32 * 4;
    u8 *masks = CALLOC(1,max_masks);
    u8 *links = MALLOC(2*src_size);
    u8 *chunks = MALLOC(2*src_size);
    if (!masks || !links || !chunks)
    {
	FREE(masks); FREE(links); FREE(chunks); return ERR_CANT_CREATE;
    }
    uint sp = 0, mp = 0, lp = 0, cp = 0, bit = 0;
    u32 mask = 0;
    while (sp < src_size)
    {
	if (!bit) { mask = 0; mp += 4; }
	uint best_len = 0, best_back = 0;
	const uint max_back = sp < 0x1000 ? sp : 0x1000;
	const uint max_len = src_size-sp < 0x111 ? src_size-sp : 0x111;
	for (uint back = 1; back <= max_back; back++)
	{
	    uint len = 0;
	    while (len < max_len && src[sp+len] == src[sp-back+len]) len++;
	    if (len > best_len) { best_len = len; best_back = back; if (len == max_len) break; }
	}
	if (best_len >= 3)
	{
	    const uint disp = best_back - 1;
	    if (best_len >= 18)
	    {
		links[lp++] = disp >> 8;
		links[lp++] = disp;
		chunks[cp++] = best_len - 18;
	    }
	    else
	    {
		links[lp++] = (best_len-2) << 4 | (disp >> 8);
		links[lp++] = disp;
	    }
	    sp += best_len;
	}
	else
	{
	    mask |= 0x80000000u >> bit;
	    chunks[cp++] = src[sp++];
	}
	bit = (bit+1) & 31;
	if (!bit) wr_be32(masks+mp-4,mask);
    }
    if (bit) wr_be32(masks+mp-4,mask);
    const uint link_off = 16 + mp;
    const uint chunk_off = link_off + lp;
    if (chunk_off > UINT_MAX-cp)
    {
	FREE(masks); FREE(links); FREE(chunks); return EFBIG;
    }
    const uint total = chunk_off + cp;
    u8 *out = MALLOC(total);
    if (!out) { FREE(masks); FREE(links); FREE(chunks); return ERR_CANT_CREATE; }
    memcpy(out,"Yay0",4);
    wr_be32(out+4,src_size); wr_be32(out+8,link_off); wr_be32(out+12,chunk_off);
    memcpy(out+16,masks,mp); memcpy(out+link_off,links,lp); memcpy(out+chunk_off,chunks,cp);
    FREE(masks); FREE(links); FREE(chunks);
    *dest = out; *dest_size = total;
    return ERR_OK;
}

//
// ============================================================
//  LZH8  (0x40)  --  buffer-based port of hcs's public-domain
//  compressor / decompressor.  Unlike the standalone wlzh8
//  tool the port returns enumError instead of calling exit().
// ============================================================

#define LZH8_LENBITS   9
#define LZH8_DISPBITS  5
#define LZH8_LENCNT   (1u << LZH8_LENBITS)
#define LZH8_DISPCNT  (1u << LZH8_DISPBITS)

enumError DecodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!dest || !dest_size || !src || src_size < 8)
        return EINVAL;

    uint input_offset = 0;
    u8 pool = 0;
    int bits_left = 0;

    // Read header; accept the WarioWare Snapped 4-byte LE size prefix.
    if (input_offset+4 > src_size) return EINVAL;
    u32 header = rd_le32(src+input_offset);
    input_offset += 4;
    if ((header & 0xFF) != 0x40)
    {
        if (input_offset+4 > src_size) return EINVAL;
        const u32 next_header = rd_le32(src+input_offset);
        if ((next_header & 0xFF) != 0x40) return EINVAL;
        header = next_header;
        input_offset += 4;
    }
    u64 uncompressed_length = header >> 8;
    if (!uncompressed_length)
    {
        if (input_offset+4 > src_size) return EINVAL;
        uncompressed_length = rd_le32(src+input_offset);
        input_offset += 4;
    }
    enumError err = alloc_output(dest,dest_size,uncompressed_length);
    if (err) return err;

    u16 length_decode_table[LZH8_LENCNT*2];
    u8  displen_decode_table[LZH8_DISPCNT*2];

    // MSB-first bit reader over SRC; returns false on end-of-input.
    #define LZH8_READ_BITS(n,out) \
        do { uint _n = (n), _got = 0; u32 _v = 0; \
            while (_got < _n) \
            { \
                if (!bits_left) \
                { \
                    if (input_offset >= src_size) goto invalid; \
                    pool = src[input_offset++]; bits_left = 8; \
                } \
                const uint _take = (uint)bits_left < _n - _got ? bits_left : _n - _got; \
                _v = _v << _take | (pool >> (bits_left - _take)) & ((1u<<_take)-1); \
                bits_left -= _take; _got += _take; \
            } \
            *(out) = _v; \
        } while (0)

    // Backreference length decode table (9-bit entries).
    if (input_offset+2 > src_size) goto invalid;
    const u32 length_table_bytes = (rd_le16(src+input_offset)+1)*4;
    input_offset += 2;
    const u32 length_start = input_offset-2;
    {
        uint i = 1;
        bits_left = 0;
        while (input_offset - length_start < length_table_bytes && i < LZH8_LENCNT*2)
        {
            u32 v;
            LZH8_READ_BITS(LZH8_LENBITS,&v);
            length_decode_table[i++] = v;
        }
        input_offset = length_start + length_table_bytes;
        if (input_offset > src_size) goto invalid;
        bits_left = 0;
    }

    // Displacement length decode table (5-bit entries).
    if (input_offset+1 > src_size) goto invalid;
    const u32 displen_table_bytes = (src[input_offset]+1)*4;
    input_offset ++;
    const u32 displen_start = input_offset-1;
    {
        uint i = 1;
        bits_left = 0;
        while (input_offset - displen_start < displen_table_bytes && i < LZH8_DISPCNT*2)
        {
            u32 v;
            LZH8_READ_BITS(LZH8_DISPBITS,&v);
            displen_decode_table[i++] = v;
        }
        input_offset = displen_start + displen_table_bytes;
        if (input_offset > src_size) goto invalid;
        bits_left = 0;
    }

    u8 *out = *dest;
    u64 bytes_decoded = 0;
    while (bytes_decoded < uncompressed_length)
    {
        u32 length_table_offset = 1;
        for (;;)
        {
            u32 next_child;
            LZH8_READ_BITS(1,&next_child);
            const u32 node_payload = length_decode_table[length_table_offset] & 0x7F;
            const u32 next_offset =
                (length_table_offset/2*2) + (node_payload+1)*2 + next_child;
            if (next_offset >= LZH8_LENCNT*2) goto invalid;
            if (length_decode_table[length_table_offset] & (0x100u >> next_child))
            {
                u16 length = length_decode_table[next_offset];
                if (length < 0x100)
                {
                    if (bytes_decoded >= uncompressed_length) goto invalid;
                    out[bytes_decoded++] = length;
                }
                else
                {
                    length = (length & 0xFF) + 3;
                    u32 displen_table_offset = 1;
                    for (;;)
                    {
                        u32 dchild;
                        LZH8_READ_BITS(1,&dchild);
                        const u32 dpayload = displen_decode_table[displen_table_offset] & 0x7;
                        const u32 doffset =
                            (displen_table_offset/2*2) + (dpayload+1)*2 + dchild;
                        if (doffset >= LZH8_DISPCNT*2) goto invalid;
                        if (displen_decode_table[displen_table_offset] & (0x10u >> dchild))
                        {
                            u16 displen = displen_decode_table[doffset];
                            u32 displacement = 0;
                            if (displen)
                            {
                                displacement = 1;
                                for (u32 i = displen-1; i; i--)
                                {
                                    u32 bit;
                                    LZH8_READ_BITS(1,&bit);
                                    displacement = displacement*2 | bit;
                                }
                            }
                            if (displacement + 1 > bytes_decoded) goto invalid;
                            const u64 start = bytes_decoded;
                            for (; bytes_decoded < uncompressed_length && bytes_decoded < start+length; bytes_decoded++)
                                out[bytes_decoded] = out[bytes_decoded - displacement - 1];
                            break;
                        }
                        displen_table_offset = doffset;
                    }
                }
                break;
            }
            length_table_offset = next_offset;
        }
    }

    *dest_size = uncompressed_length;
    return ERR_OK;

invalid:
    FREE(*dest);
    *dest = 0;
    *dest_size = 0;
    return EINVAL;
}

// ============================================================
//  LZH8 encoder
// ============================================================

struct lzh8_symbol
{
    uint8_t  is_reference;
    uint8_t  length_or_literal;
    uint16_t offset;
};

struct lzh8_huff_node
{
    int lchild, rchild;
    uint16_t leaf;
    uint16_t subtree_size;
};

struct lzh8_table_ctrl
{
    int node_idx;
    bool placed : 1;
};

struct lzh8_huff_symbol
{
    uint16_t key_len;
    uint32_t key_bits;
};

static uint LZH8_displen_length ( uint16_t displacement )
{
    uint bits = 0;
    while (displacement) { displacement >>= 1; bits++; }
    return bits;
}

// Growable output buffer written at absolute offsets, mirroring the
// seek()-style put_*_seek() helpers of the original tool.
typedef struct lzh8_wr_t
{
    u8 *data;
    uint size, cap;
}
lzh8_wr_t;

static enumError lzh8_wr_ensure ( lzh8_wr_t *w, uint need )
{
    if (need <= w->cap)
    {
        if (w->size < need) w->size = need;
        return ERR_OK;
    }
    uint cap = w->cap ? w->cap*2 : 0x4000;
    if (cap < need) cap = (need + 0xfff) & ~0xfffu;
    u8 *nd = REALLOC(w->data,cap);
    if (!nd) return ERR_CANT_CREATE;
    w->data = nd;
    w->cap = cap;
    if (w->size < need) w->size = need;
    return ERR_OK;
}

static enumError lzh8_wr_byte ( lzh8_wr_t *w, uint off, u8 v )
{
    enumError err = lzh8_wr_ensure(w,off+1);
    if (err) return err;
    w->data[off] = v;
    return ERR_OK;
}

static enumError lzh8_wr_16_le ( lzh8_wr_t *w, uint off, u16 v )
{
    enumError err = lzh8_wr_ensure(w,off+2);
    if (err) return err;
    w->data[off] = v; w->data[off+1] = v >> 8;
    return ERR_OK;
}

static enumError lzh8_wr_32_le ( lzh8_wr_t *w, uint off, u32 v )
{
    enumError err = lzh8_wr_ensure(w,off+4);
    if (err) return err;
    w->data[off]=v; w->data[off+1]=v>>8; w->data[off+2]=v>>16; w->data[off+3]=v>>24;
    return ERR_OK;
}

static enumError lzh8_wr_32_be ( lzh8_wr_t *w, uint off, u32 v )
{
    enumError err = lzh8_wr_ensure(w,off+4);
    if (err) return err;
    w->data[off]=v>>24; w->data[off+1]=v>>16; w->data[off+2]=v>>8; w->data[off+3]=v;
    return ERR_OK;
}

static enumError lzh8_flush_bits ( lzh8_wr_t *w, uint *offset_p, u32 *pool_p, int *written_p )
{
    if (*written_p)
    {
        enumError err = lzh8_wr_32_be(w,*offset_p,*pool_p);
        if (err) return err;
        *written_p = 0;
        *pool_p = 0;
        *offset_p += 4;
    }
    return ERR_OK;
}

static enumError lzh8_write_bits
(
    lzh8_wr_t *w, uint *offset_p, u32 *pool_p, int *written_p,
    u32 bits_to_write, int bit_count
)
{
    int produced = 0;
    while (produced < bit_count)
    {
        if (32 == *written_p)
        {
            enumError err = lzh8_flush_bits(w,offset_p,pool_p,written_p);
            if (err) return err;
        }
        int this_round;
        if (*written_p + (bit_count - produced) <= 32)
            this_round = bit_count - produced;
        else
            this_round = 32 - *written_p;
        const u32 selected = (bits_to_write >> (bit_count - this_round - produced))
            & ((1u << this_round) - 1);
        *pool_p |= selected << (32 - this_round - *written_p);
        *written_p += this_round;
        produced += this_round;
    }
    return ERR_OK;
}

static uint lzh8_hash ( const u8 *p, int len, int hash_size )
{
    int key = 0;
    for (int i=0; i<len; i++) key = ((key<<5) ^ p[i]) % hash_size;
    return key;
}

// LZSS with hashing; STRICT mode reproduces Nintendo's exact output.
static enumError LZH8_LZSS_compress
(
    const u8 *input_data, uint input_length,
    struct lzh8_symbol **lzss_stream_p, uint *lzss_length_p
)
{
    const int min_length = 3;
    const int max_length = (1 << 8) - 1 + 3;
    const uint max_window_size = (1u << 15);

    struct lzss_hash_node
    {
        long offset;
        struct lzss_hash_node *next_node, *prev_node;
    };
    const int hash_size = 1024;

    struct lzss_hash_node *hash_queue = CALLOC(max_window_size,sizeof(struct lzss_hash_node));
    struct lzss_hash_node *hash_table = CALLOC(hash_size,sizeof(struct lzss_hash_node));
    if (!hash_queue || !hash_table) { FREE(hash_queue); FREE(hash_table); return ERR_CANT_CREATE; }
    for (int i=0; i<hash_size; i++)
    {
        hash_table[i].next_node = NULL;
        hash_table[i].prev_node = NULL;
        hash_queue[i].offset = -2;
    }
    for (uint i=0; i<max_window_size; i++)
    {
        hash_queue[i].next_node = NULL;
        hash_queue[i].prev_node = NULL;
        hash_queue[i].offset = -1;
    }

    struct lzh8_symbol *lzss_stream = NULL;
    uint lzss_length = 0, capacity = 0;

    uint bytes_done = 0;
    uint window_size = 0;
    uint hash_queue_head = 0, hash_queue_tail = 0;

    for (; bytes_done < input_length; )
    {
        int longest_match = 0;
        long longest_match_offset = 0;

        const uint next_input_offset = bytes_done + max_length < input_length
            ? bytes_done + max_length : input_length;

        if (bytes_done + min_length <= input_length)
        {
            const uint input_key = lzh8_hash(&input_data[bytes_done],min_length,hash_size);
            for (struct lzss_hash_node *cur = hash_table[input_key].next_node;
                 cur; cur = cur->next_node)
            {
                if (cur->offset == bytes_done - 1) continue;  // POLICY
                uint match_length = 0;
                for (uint i=0; bytes_done+i < next_input_offset &&
                        input_data[bytes_done+i] == input_data[cur->offset+i]; i++)
                    match_length = i+1;
                if (match_length > (uint)longest_match)
                {
                    longest_match = match_length;
                    longest_match_offset = cur->offset;
                }
            }
        }

        if (lzss_length >= capacity)
        {
            capacity = capacity ? capacity*2 : 0x800;
            struct lzh8_symbol *ns = REALLOC(lzss_stream,capacity*sizeof(*lzss_stream));
            if (!ns) { FREE(hash_queue); FREE(hash_table); FREE(lzss_stream); return ERR_CANT_CREATE; }
            lzss_stream = ns;
        }

        uint bytes_in_this_symbol;
        if (longest_match < min_length)
        {
            lzss_stream[lzss_length].is_reference = 0;
            lzss_stream[lzss_length].length_or_literal = input_data[bytes_done];
            lzss_length++;
            bytes_in_this_symbol = 1;
        }
        else
        {
            lzss_stream[lzss_length].is_reference = 1;
            lzss_stream[lzss_length].length_or_literal = longest_match - 3;
            lzss_stream[lzss_length].offset = bytes_done - longest_match_offset - 1;
            lzss_length++;
            bytes_in_this_symbol = longest_match;
        }

        for (uint i=0; i<bytes_in_this_symbol; i++, bytes_done++)
        {
            if (window_size == max_window_size)
            {
                struct lzss_hash_node *old_node = &hash_queue[hash_queue_head];
                old_node->prev_node->next_node = NULL;
                hash_queue_head = (hash_queue_head+1) % max_window_size;
                old_node->offset = -1;
                window_size--;
            }
            if (input_length - bytes_done >= min_length)
            {
                struct lzss_hash_node *new_node = &hash_queue[hash_queue_tail];
                const uint hash_key = lzh8_hash(&input_data[bytes_done],min_length,hash_size);
                new_node->next_node = hash_table[hash_key].next_node;
                new_node->prev_node = &hash_table[hash_key];
                hash_table[hash_key].next_node = new_node;
                if (new_node->next_node) new_node->next_node->prev_node = new_node;
                new_node->offset = bytes_done;
                hash_queue_tail = (hash_queue_tail+1) % max_window_size;
                window_size++;
            }
        }
    }

    *lzss_stream_p = lzss_stream;
    *lzss_length_p = lzss_length;
    FREE(hash_queue);
    FREE(hash_table);
    return ERR_OK;
}

static int LZH8_Huff_build_tree
(
    int *node_remains, long *freq,
    struct lzh8_huff_node *node_array, int symbol_count
)
{
    int nodes_left = 0;
    int next_new_node_idx = symbol_count;
    for (int i=0; i<symbol_count; i++)
    {
        if (0 != freq[i]) { node_remains[i] = 1; nodes_left++; }
        else node_remains[i] = 0;
        node_array[i].lchild = -1;
        node_array[i].rchild = -1;
        node_array[i].leaf = i;
        node_array[i].subtree_size = 0;
    }
    for (int i=symbol_count; i < symbol_count*2-1; i++) node_remains[i] = 0;

    int root_idx = 0;
    if (0 == nodes_left) return -1;

    if (1 == nodes_left)
    {
        int i;
        for (i=0; i<symbol_count; i++) if (node_remains[i]) break;
        node_array[next_new_node_idx].lchild = i;
        node_array[next_new_node_idx].rchild = i;
        node_array[next_new_node_idx].subtree_size = 1;
        root_idx = next_new_node_idx;
    }

    for (; nodes_left > 1; nodes_left--)
    {
        int smallest_idx = -1, next_smallest_idx = -1;
        {
            long smallest = -1, next_smallest = -1;
            for (int i=0; i<next_new_node_idx; i++)
            {
                if (node_remains[i])
                {
                    if (freq[i] < smallest || -1 == smallest)
                    {
                        next_smallest = smallest;
                        next_smallest_idx = smallest_idx;
                        smallest = freq[i];
                        smallest_idx = i;
                    }
                    else if (freq[i] < next_smallest || -1 == next_smallest)
                    {
                        next_smallest = freq[i];
                        next_smallest_idx = i;
                    }
                }
            }
        }
        struct lzh8_huff_node sum_node;
        sum_node.lchild = smallest_idx;
        sum_node.rchild = next_smallest_idx;
        sum_node.leaf = 0;
        sum_node.subtree_size = node_array[smallest_idx].subtree_size
            + node_array[next_smallest_idx].subtree_size + 1;
        const long total_freq = freq[smallest_idx] + freq[next_smallest_idx];
        const int sum_node_idx = next_new_node_idx;
        freq[sum_node_idx] = total_freq;
        node_remains[sum_node_idx] = 1;
        node_remains[smallest_idx] = 0;
        node_remains[next_smallest_idx] = 0;
        node_array[sum_node_idx] = sum_node;
        root_idx = sum_node_idx;
        next_new_node_idx++;
    }
    return root_idx;
}

static void LZH8_Huff_compute_prefix
(
    const struct lzh8_huff_node *node_array, int root_idx,
    struct lzh8_huff_symbol *sym_array, u32 key_bits, int key_len
)
{
    const struct lzh8_huff_node *root = &node_array[root_idx];
    if (-1 == root_idx) return;
    if (-1 != root->lchild)
    {
        key_len++;
        LZH8_Huff_compute_prefix(node_array,root->lchild,sym_array,key_bits<<1,key_len);
        LZH8_Huff_compute_prefix(node_array,root->rchild,sym_array,(key_bits<<1)|1,key_len);
    }
    else
    {
        sym_array[root->leaf].key_len = key_len;
        sym_array[root->leaf].key_bits = key_bits;
    }
}

static bool LZH8_Huff_could_satisfy
(
    const struct lzh8_table_ctrl *ctrl, int table_idx,
    uint16_t proposed_size, int proposed_idx, const int offset_bits
)
{
    (void)proposed_idx;
    const int max_offset = 1 << offset_bits;
    for (unsigned int i=0; i<table_idx; i++)
    {
        if (!ctrl[i].placed)
        {
            const uint16_t dest_offset = table_idx/2 + proposed_size;
            if (max_offset >= dest_offset - i/2) proposed_size++;
            else return false;
        }
    }
    return true;
}

static void LZH8_Huff_flatten_single
(
    const struct lzh8_huff_node *node_array, struct lzh8_table_ctrl *ctrl,
    u16 *tree_table, const int offset_bits, unsigned int parent_idx,
    unsigned int *table_idx_p, unsigned int *outstanding_p
)
{
    u8 leaf_flags = 0;
    const struct lzh8_huff_node *parent_node = &node_array[ctrl[parent_idx].node_idx];
    if (node_array[parent_node->lchild].lchild != -1)
    {
        tree_table[*table_idx_p] = 0;
        ctrl[*table_idx_p].placed = false;
        ctrl[*table_idx_p].node_idx = parent_node->lchild;
        (*outstanding_p)++;
    }
    else
    {
        tree_table[*table_idx_p] = node_array[parent_node->lchild].leaf;
        ctrl[*table_idx_p].placed = true;
        leaf_flags |= 2;
    }
    (*table_idx_p)++;
    if (node_array[parent_node->rchild].lchild != -1)
    {
        tree_table[*table_idx_p] = 0;
        ctrl[*table_idx_p].placed = false;
        ctrl[*table_idx_p].node_idx = parent_node->rchild;
        (*outstanding_p)++;
    }
    else
    {
        tree_table[*table_idx_p] = node_array[parent_node->rchild].leaf;
        ctrl[*table_idx_p].placed = true;
        leaf_flags |= 1;
    }
    (*table_idx_p)++;
    const u16 offset = (((*table_idx_p) - 2) - parent_idx/2*2)/2 - 1;
    tree_table[parent_idx] = (leaf_flags << offset_bits) | offset;
    ctrl[parent_idx].placed = true;
    (*outstanding_p)--;
}

static uint LZH8_Huff_flatten_tree
(
    const struct lzh8_huff_node *node_array, u16 *tree_table,
    int root_idx, const int offset_bits
)
{
    if (-1 == root_idx) return 0;
    struct lzh8_table_ctrl *ctrl = MALLOC((root_idx+2)*sizeof(*ctrl));
    if (!ctrl) return UINT_MAX;

    unsigned int outstanding_nodes = 1;
    ctrl[0].placed = true;
    ctrl[1].node_idx = root_idx;
    ctrl[1].placed = false;
    unsigned int table_idx = 2;

    while (0 < outstanding_nodes)
    {
        uint16_t fitting_idx = table_idx;
        for (int i = table_idx-1; i >= 0; i--)
        {
            if (!ctrl[i].placed)
            {
                const struct lzh8_huff_node *candidate = &node_array[ctrl[i].node_idx];
                if (candidate->subtree_size + outstanding_nodes <= (1u << offset_bits)
                    && LZH8_Huff_could_satisfy(ctrl,table_idx,candidate->subtree_size,i,offset_bits))
                {
                    fitting_idx = i;
                    break;
                }
            }
        }

        if (fitting_idx != table_idx)
        {
            unsigned int i = table_idx;
            LZH8_Huff_flatten_single(node_array,ctrl,tree_table,offset_bits,
                fitting_idx,&table_idx,&outstanding_nodes);
            for (; i < table_idx; i++)
            {
                if (!ctrl[i].placed)
                    LZH8_Huff_flatten_single(node_array,ctrl,tree_table,offset_bits,
                        i,&table_idx,&outstanding_nodes);
            }
        }
        else
        {
            for (unsigned int i=0; i<table_idx; i+=2)
            {
                unsigned int node_to_break = table_idx;
                if (!ctrl[i+0].placed)
                {
                    if (!ctrl[i+1].placed
                        && node_array[ctrl[i+1].node_idx].subtree_size >
                           node_array[ctrl[i+0].node_idx].subtree_size)
                        node_to_break = i+1;
                    else node_to_break = i+0;
                }
                else if (!ctrl[i+1].placed) node_to_break = i+1;
                if (node_to_break != table_idx)
                {
                    LZH8_Huff_flatten_single(node_array,ctrl,tree_table,offset_bits,
                        node_to_break,&table_idx,&outstanding_nodes);
                    break;
                }
            }
        }
    }
    FREE(ctrl);
    return table_idx;
}

static enumError LZH8_Huff_produce_encodings
(
    const struct lzh8_symbol *lzss_stream, uint lzss_length,
    struct lzh8_huff_symbol *back_litlen, struct lzh8_huff_symbol *back_displen,
    uint *output_offset_p, lzh8_wr_t *w
)
{
    long length_freq[LZH8_LENCNT*2-1] = {0};
    long displen_freq[LZH8_DISPCNT*2-1] = {0};
    for (uint i=0; i<lzss_length; i++)
    {
        length_freq[(lzss_stream[i].is_reference << 8) | lzss_stream[i].length_or_literal]++;
        if (lzss_stream[i].is_reference)
            displen_freq[LZH8_displen_length(lzss_stream[i].offset)]++;
    }

    #define LZH8_WRITE_BITS(bits,count) \
        do { enumError e = lzh8_write_bits(w,output_offset_p,&lzh8_bit_pool,&lzh8_bits_written,bits,count); \
             if (e) return e; } while (0)

    // Length/literal tree
    {
        int node_remains[LZH8_LENCNT*2-1];
        struct lzh8_huff_node node_array[LZH8_LENCNT*2-1];
        const int root_idx = LZH8_Huff_build_tree(node_remains,length_freq,node_array,LZH8_LENCNT);
        LZH8_Huff_compute_prefix(node_array,root_idx,back_litlen,0,0);

        u16 tree_table[LZH8_LENCNT*2] = {0};
        const uint table_size = LZH8_Huff_flatten_tree(node_array,tree_table,root_idx,LZH8_LENBITS-2);
        if (table_size == UINT_MAX) return ERR_CANT_CREATE;

        const uint start = *output_offset_p;
        u32 lzh8_bit_pool = 0;
        int lzh8_bits_written = 16;   // leave space for the size field
        for (uint i=1; i<table_size; i++) LZH8_WRITE_BITS(tree_table[i],LZH8_LENBITS);
        { enumError e = lzh8_flush_bits(w,output_offset_p,&lzh8_bit_pool,&lzh8_bits_written); if (e) return e; }
        const uint table_bytes = (*output_offset_p - start)/4 - 1;
        { enumError e = lzh8_wr_16_le(w,start,table_bytes); if (e) return e; }
    }

    // Displacement length tree
    {
        int node_remains[LZH8_DISPCNT*2-1];
        struct lzh8_huff_node node_array[LZH8_DISPCNT*2-1];
        const int root_idx = LZH8_Huff_build_tree(node_remains,displen_freq,node_array,LZH8_DISPCNT);
        LZH8_Huff_compute_prefix(node_array,root_idx,back_displen,0,0);

        u16 tree_table[LZH8_DISPCNT*2] = {0};
        const uint table_size = LZH8_Huff_flatten_tree(node_array,tree_table,root_idx,LZH8_DISPBITS-2);
        if (table_size == UINT_MAX) return ERR_CANT_CREATE;

        const uint start = *output_offset_p;
        u32 lzh8_bit_pool = 0;
        int lzh8_bits_written = 8;    // leave space for the size field
        for (uint i=1; i<table_size; i++) LZH8_WRITE_BITS(tree_table[i],LZH8_DISPBITS);
        { enumError e = lzh8_flush_bits(w,output_offset_p,&lzh8_bit_pool,&lzh8_bits_written); if (e) return e; }
        const uint table_bytes = (*output_offset_p - start)/4 - 1;
        { enumError e = lzh8_wr_byte(w,start,table_bytes); if (e) return e; }
    }

    #undef LZH8_WRITE_BITS
    return ERR_OK;
}

enumError EncodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size )
{
    if (!dest || !dest_size || !src || !src_size)
        return EINVAL;

    lzh8_wr_t w = {0};
    uint output_offset = 0;

    // Step 0: header
    enumError err;
    if (src_size < 0x1000000)
    {
        err = lzh8_wr_32_le(&w,0,(((u32)src_size) << 8) | 0x40);
        if (err) return err;
        output_offset = 4;
    }
    else
    {
        err = lzh8_wr_32_le(&w,0,0x40);
        if (!err) err = lzh8_wr_32_le(&w,4,src_size);
        if (err) return err;
        output_offset = 8;
    }

    // Step 1: LZSS
    struct lzh8_symbol *lzss_stream = NULL;
    uint lzss_length = 0;
    err = LZH8_LZSS_compress(src,src_size,&lzss_stream,&lzss_length);
    if (err) { FREE(lzss_stream); FREE(w.data); return err; }

    // Step 2: build Huffman codes and write the flattened trees
    struct lzh8_huff_symbol back_litlen[LZH8_LENCNT];
    struct lzh8_huff_symbol back_displen[LZH8_DISPCNT];
    err = LZH8_Huff_produce_encodings(lzss_stream,lzss_length,
        back_litlen,back_displen,&output_offset,&w);
    if (err) { FREE(lzss_stream); FREE(w.data); return err; }

    // Step 3: encoded symbol stream
    {
        u32 bit_pool = 0;
        int bits_written = 0;
        for (uint i=0; i<lzss_length; i++)
        {
            const struct lzh8_huff_symbol litlen =
                back_litlen[(lzss_stream[i].is_reference << 8) | lzss_stream[i].length_or_literal];
            err = lzh8_write_bits(&w,&output_offset,&bit_pool,&bits_written,litlen.key_bits,litlen.key_len);
            if (!err && lzss_stream[i].is_reference)
            {
                const uint displen_length = LZH8_displen_length(lzss_stream[i].offset);
                const struct lzh8_huff_symbol displen_sym = back_displen[displen_length];
                err = lzh8_write_bits(&w,&output_offset,&bit_pool,&bits_written,displen_sym.key_bits,displen_sym.key_len);
                if (!err && lzss_stream[i].offset > 1)
                    err = lzh8_write_bits(&w,&output_offset,&bit_pool,&bits_written,lzss_stream[i].offset,displen_length-1);
            }
            if (err) { FREE(lzss_stream); FREE(w.data); return err; }
        }
        err = lzh8_flush_bits(&w,&output_offset,&bit_pool,&bits_written);
        if (err) { FREE(lzss_stream); FREE(w.data); return err; }
    }

    FREE(lzss_stream);
    *dest = w.data;
    *dest_size = w.size;
    return ERR_OK;
}

static inline u8 expand5 ( u8 value )
    { return value << 3 | value >> 2; }

enumError DecodeDSB_RGBA
(
    u8 **dest, uint *width, uint *height,
    const u8 *src, uint src_size
)
{
    if (!dest || !width || !height || !src || src_size <= 0x60
        || memcmp(src,"TXTR",4))
        return EINVAL;

    // AC:WW's menu TXTR variant stores 32 little-endian RGB555 entries at
    // 0x20 and one A3I5 byte per pixel at 0x60.  The payload is square.
    const uint pixel_count = src_size - 0x60;
    uint side = 1;
    while ( side <= pixel_count / side && side * side < pixel_count )
        side++;
    if ( side * side != pixel_count || side > 1024 )
        return EINVAL;

    u8 *rgba = MALLOC(pixel_count * 4);
    if (!rgba)
        return ERR_CANT_CREATE;

    const u8 *texel = src + 0x60;
    for ( uint i = 0; i < pixel_count; i++ )
    {
        const u8 value = texel[i];
        const uint poff = 0x20 + (value & 0x1f) * 2;
        const u16 color = src[poff] | (u16)src[poff+1] << 8;
        rgba[4*i+0] = expand5(color & 0x1f);
        rgba[4*i+1] = expand5(color >> 5 & 0x1f);
        rgba[4*i+2] = expand5(color >> 10 & 0x1f);
        rgba[4*i+3] = (value >> 5) * 255 / 7;
    }

    *dest = rgba;
    *width = *height = side;
    return ERR_OK;
}

enumError EncodeDSB_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height
)
{
    if (!dest || !dest_size || !rgba || width != 128 || height != 128)
        return EINVAL;

    const uint pixels = width * height;
    const uint total = 0x60 + pixels;
    u8 *out = CALLOC(1,total);
    if (!out) return ERR_CANT_CREATE;

    static const u8 header[0x20] = {
        'T','X','T','R', 0x10,0x44,0x60,0x00, 0x60,0x00,0x10,0x20,
        0x00,0x01,0x60,0x00
    };
    memcpy(out,header,sizeof(header));

    u16 palette[32] = {0};
    uint n_pal = 1;
    for (uint px = 0; px < pixels && n_pal < 32; px++)
    {
        const u8 *p = rgba + 4*px;
        const u16 c = (u16)(p[0]>>3) | (u16)(p[1]>>3)<<5 | (u16)(p[2]>>3)<<10;
        uint pi;
        for (pi = 0; pi < n_pal && palette[pi] != c; pi++) {}
        if (pi == n_pal) palette[n_pal++] = c;
    }
    for (uint pi = 0; pi < 32; pi++)
    {
        out[0x20+2*pi] = palette[pi];
        out[0x21+2*pi] = palette[pi] >> 8;
    }
    for (uint px = 0; px < pixels; px++)
    {
        const u8 *p = rgba + 4*px;
        const int r = p[0] >> 3, g = p[1] >> 3, b = p[2] >> 3;
        uint best = 0, best_dist = UINT_MAX;
        for (uint pi = 0; pi < n_pal; pi++)
        {
            const int dr = r - (palette[pi] & 31);
            const int dg = g - (palette[pi] >> 5 & 31);
            const int db = b - (palette[pi] >> 10 & 31);
            const uint dist = dr*dr + dg*dg + db*db;
            if (dist < best_dist) { best = pi; best_dist = dist; }
        }
        out[0x60+px] = ((p[3] * 7 + 127) / 255) << 5 | best;
    }
    *dest = out;
    *dest_size = total;
    return ERR_OK;
}

enumError DecodeBNR_RGBA ( u8 **dest, const u8 *src, uint src_size )
{
    if (!dest || !src || src_size < 0x20 + 96*32*2
        || (memcmp(src,"BNR1",4) && memcmp(src,"BNR2",4)))
        return EINVAL;
    u8 *rgba = MALLOC(96*32*4);
    if (!rgba) return ERR_CANT_CREATE;
    const u8 *pixels = src + 0x20;
    for (uint by = 0; by < 32/4; by++)
        for (uint bx = 0; bx < 96/4; bx++)
            for (uint y = 0; y < 4; y++)
                for (uint x = 0; x < 4; x++)
                {
                    const uint pi = 16*(by*(96/4)+bx) + 4*y + x;
                    const u16 c = rd_be16(pixels+2*pi);
                    u8 *d = rgba + 4*((4*by+y)*96 + 4*bx+x);
                    if (c & 0x8000)
                    {
                        d[0] = expand5(c >> 10 & 31);
                        d[1] = expand5(c >> 5 & 31);
                        d[2] = expand5(c & 31);
                        d[3] = 255;
                    }
                    else
                    {
                        d[0] = (c >> 8 & 15) * 17;
                        d[1] = (c >> 4 & 15) * 17;
                        d[2] = (c & 15) * 17;
                        d[3] = (c >> 12 & 7) * 255 / 7;
                    }
                }
    *dest = rgba;
    return ERR_OK;
}

enumError EncodeBNR_RGBA ( u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height )
{
    if (!dest || !dest_size || !rgba || width != 96 || height != 32)
        return EINVAL;
    // BNR1 is 0x1960 bytes: 0x20-byte header, 96x32 RGB5A3 icon, and six
    // zero-filled Shift-JIS title fields.  Empty fields are legal and make a
    // useful canonical banner when the input is a PNG rather than a BNR file.
    const uint size = 0x1960;
    u8 *out = CALLOC(1,size);
    if (!out) return ERR_CANT_CREATE;
    memcpy(out,"BNR1",4);
    u8 *pixels = out + 0x20;
    for (uint by = 0; by < 32/4; by++)
        for (uint bx = 0; bx < 96/4; bx++)
            for (uint y = 0; y < 4; y++)
                for (uint x = 0; x < 4; x++)
                {
                    const u8 *s = rgba + 4*((4*by+y)*96 + 4*bx+x);
                    u16 c;
                    if (s[3] >= 224)
                        c = 0x8000 | (u16)(s[0]>>3)<<10 | (u16)(s[1]>>3)<<5 | (s[2]>>3);
                    else
                        c = (u16)((s[3]*7+127)/255)<<12 | (u16)(s[0]>>4)<<8
                            | (u16)(s[1]>>4)<<4 | (s[2]>>4);
                    const uint pi = 16*(by*(96/4)+bx) + 4*y + x;
                    wr_be16(pixels+2*pi,c);
                }
    *dest = out;
    *dest_size = size;
    return ERR_OK;
}

enumError DecodeNCGR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
)
{
    if (!dest || !width || !height || !src || src_size < 0x30
        || memcmp(src,"RGCN",4) || memcmp(src+0x10,"RAHC",4))
        return EINVAL;
    // Nitro's RAHC header stores the data byte count and an offset relative
    // to RAHC+8.  The normal resource layout has data at RAHC+0x20.
    const uint depth = rd_le32(src+0x10+0x0c);
    const uint data_size = rd_le32(src+0x10+0x18);
    const uint data_off = 8 + rd_le32(src+0x10+0x1c);
    const uint bpt = depth == 3 ? 32 : depth == 4 ? 64 : 0;
    if (!bpt || !data_size || data_size % bpt || data_off > src_size-0x10
        || data_size > src_size-(0x10+data_off)) return EINVAL;
    const uint n_tiles = data_size/bpt, cols = n_tiles < 16 ? n_tiles : 16;
    const uint rows = (n_tiles+15)/16, w = 8*cols, h = 8*rows;
    if (!w || !h || (u64)w*h > NFMT_MAX_OUTPUT/4) return EFBIG;
    u8 *out = CALLOC(1,w*h*4);
    if (!out) return ERR_CANT_CREATE;
    const u8 *tiles = src+0x10+data_off;
    for (uint tile = 0; tile < n_tiles; tile++)
        for (uint y = 0; y < 8; y++)
            for (uint x = 0; x < 8; x++)
            {
                const uint pos = tile*bpt + (depth == 3 ? 4*y+x/2 : 8*y+x);
                const u8 index = depth == 3 ? (tiles[pos] >> (4*(x&1))) & 15 : tiles[pos];
                u8 *p = out + 4*((tile/16*8+y)*w + tile%16*8+x);
                p[0] = p[1] = p[2] = depth == 3 ? index*17 : index;
                p[3] = index ? 255 : 0;
            }
    *dest = out; *width = w; *height = h;
    return ERR_OK;
}

enumError DecodeNCLR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
)
{
    if (!dest || !width || !height || !src || src_size < 0x28
        || memcmp(src,"RLCN",4) || memcmp(src+0x10,"TTLP",4))
        return EINVAL;

    // TTLP's data offset is relative to TTLP+8.  It is normally 0x10,
    // yielding palette data at file offset 0x28.
    const uint depth = rd_le32(src+0x18);
    const uint data_size = rd_le32(src+0x20);
    const uint data_off = 0x18 + rd_le32(src+0x24);
    if ((depth != 3 && depth != 4) || !data_size || data_size & 1
        || data_off > src_size || data_size > src_size-data_off)
        return EINVAL;
    const uint entries = data_size/2;
    const uint max_entries = depth == 3 ? 16 : 256;
    if (!entries || entries > max_entries) return EINVAL;

    const uint cell = 8, cols = 16, rows = (entries+cols-1)/cols;
    const uint w = cols*cell, h = rows*cell;
    if ((u64)w*h > NFMT_MAX_OUTPUT/4) return EFBIG;
    u8 *out = MALLOC(w*h*4);
    if (!out) return ERR_CANT_CREATE;

    for (uint entry = 0; entry < entries; entry++)
    {
        const u16 c = rd_le16(src+data_off+2*entry);
        const u8 r = (c & 31) * 255 / 31;
        const u8 g = ((c >> 5) & 31) * 255 / 31;
        const u8 b = ((c >> 10) & 31) * 255 / 31;
        for (uint y = 0; y < cell; y++)
            for (uint x = 0; x < cell; x++)
            {
                u8 *p = out + 4*((entry/cols*cell+y)*w + entry%cols*cell+x);
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            }
    }
    *dest = out; *width = w; *height = h;
    return ERR_OK;
}

enumError ScanNCER ( nintendo_ncer_t *ncer, const u8 *data, uint size )
{
    if (!ncer || !data || size < 0x30 || memcmp(data,"RECN",4)
        || memcmp(data+0x10,"KBEC",4))
        return EINVAL;
    const u8 *kbec = data+0x10;
    const uint chunk_size = rd_le32(kbec+4);
    const uint n_cells = rd_le16(kbec+8);
    const uint entry_kind = rd_le16(kbec+10);
    const uint cell_size = entry_kind == 0 ? 8 : entry_kind == 1 ? 16 : 0;
    const uint cell_off = 8 + rd_le32(kbec+12);
    if (!chunk_size || chunk_size > size-0x10 || !n_cells || !cell_size
        || cell_off > chunk_size || n_cells > (chunk_size-cell_off)/cell_size)
        return EINVAL;
    const uint objects_off = cell_off + n_cells*cell_size;
    if (objects_off > chunk_size) return EINVAL;
    memset(ncer,0,sizeof(*ncer));
    ncer->data = data; ncer->size = size; ncer->n_cells = n_cells;
    ncer->cell_size = cell_size; ncer->cells = kbec+cell_off;
    ncer->objects = kbec+objects_off; ncer->objects_size = chunk_size-objects_off;
    for (uint i = 0; i < n_cells; i++)
    {
        const u8 *cell = ncer->cells+i*cell_size;
        const uint n_obj = rd_le16(cell);
        const uint obj_off = rd_le32(cell+4);
        if (obj_off > ncer->objects_size || n_obj > (ncer->objects_size-obj_off)/6)
            return EINVAL;
    }
    return ERR_OK;
}

enumError GetNCERCell
(
    const nintendo_ncer_t *ncer, uint index, uint *n_objects,
    const u8 **oam_records
)
{
    if (!ncer || !n_objects || !oam_records || index >= ncer->n_cells)
        return EINVAL;
    const u8 *cell = ncer->cells + index*ncer->cell_size;
    const uint count = rd_le16(cell);
    const uint off = rd_le32(cell+4);
    if (off > ncer->objects_size || count > (ncer->objects_size-off)/6)
        return EINVAL;
    *n_objects = count;
    *oam_records = ncer->objects+off;
    return ERR_OK;
}

enumError ScanNANR ( nintendo_nanr_t *nanr, const u8 *data, uint size )
{
    if (!nanr || !data || size < 0x38 || memcmp(data,"RNAN",4)
        || memcmp(data+0x10,"KNBA",4))
        return EINVAL;
    const u8 *knba = data+0x10;
    const uint chunk_size = rd_le32(knba+4);
    const uint n_anims = rd_le16(knba+8), n_frames = rd_le16(knba+10);
    const uint anim_off = 8 + rd_le32(knba+12);
    const uint frame_off = 8 + rd_le32(knba+16);
    const uint data_off = 8 + rd_le32(knba+20);
    if (!chunk_size || chunk_size > size-0x10 || !n_anims || !n_frames
        || anim_off > chunk_size || n_anims > (chunk_size-anim_off)/16
        || frame_off > chunk_size || n_frames > (chunk_size-frame_off)/8
        || data_off > chunk_size)
        return EINVAL;
    memset(nanr,0,sizeof(*nanr));
    nanr->data = data; nanr->size = size; nanr->n_animations = n_anims;
    nanr->n_frames = n_frames; nanr->animations = knba+anim_off;
    nanr->frames = knba+frame_off; nanr->frames_size = n_frames*8;
    nanr->frame_data = knba+data_off; nanr->frame_data_size = chunk_size-data_off;
    for (uint i = 0; i < n_anims; i++)
    {
        const u8 *anim = nanr->animations + 16*i;
        const uint count = rd_le32(anim);
        const uint off = rd_le32(anim+12);
        if (!count || off > nanr->frames_size || count > (nanr->frames_size-off)/8)
            return EINVAL;
    }
    if (nanr->frame_data_size < 2) return EINVAL;
    for (uint i = 0; i < n_frames; i++)
    {
        const u8 *frame = nanr->frames+8*i;
        if (rd_le32(frame) > nanr->frame_data_size-2) return EINVAL;
    }
    return ERR_OK;
}

enumError GetNANRAnimation
(
    const nintendo_nanr_t *nanr, uint index, uint *n_frames,
    const u8 **frame_records
)
{
    if (!nanr || !n_frames || !frame_records || index >= nanr->n_animations)
        return EINVAL;
    const u8 *anim = nanr->animations+16*index;
    const uint count = rd_le32(anim), off = rd_le32(anim+12);
    if (!count || off > nanr->frames_size || count > (nanr->frames_size-off)/8)
        return EINVAL;
    *n_frames = count;
    *frame_records = nanr->frames+off;
    return ERR_OK;
}

static uint morton8 ( uint x, uint y )
{
    return (x&1) | (y&1)<<1 | (x&2)<<1 | (y&2)<<2 | (x&4)<<2 | (y&4)<<3;
}

// ETC1/ETC1A4 4x4 block decoder. The bit layout (base colors, table
// selection, per-pixel modifier index) was verified pixel-for-pixel
// against the independent `texture2ddecoder` reference decoder (both
// individual and differential color modes, both flip orientations) using
// real BFLIM sample data before this was written -- see commit message.
// Byte layout within the 16-byte ETC1A4 block (alpha first, then color)
// matches the documented Ohana3DS convention. The alpha nibble-to-pixel
// order and the block-to-tile arrangement for images larger than one
// 8x8-pixel tile are NOT independently verified (no oracle covers those);
// they follow the same tiling convention already used and verified for
// this codebase's other BFLIM pixel formats.
static const int16_t etc1_mod_table[8][4] =
{
    {-8,-2,2,8}, {-17,-5,5,17}, {-29,-9,9,29}, {-42,-13,13,42},
    {-60,-18,18,60}, {-80,-24,24,80}, {-106,-33,33,106}, {-183,-47,47,183}
};

static inline u8 etc1_clamp255 ( int v ) { return v<0?0:v>255?255:(u8)v; }

// data = 8 bytes ETC1 color block. out = 4x4 RGBA (row-major, 64 bytes).
static void decode_etc1_block ( const u8 data[8], u8 *out )
{
    u64 v = 0;
    for ( int i = 0; i < 8; i++ ) v = (v<<8)|data[i];

    const int diffbit = (v>>33)&1;
    const int flipbit = (v>>32)&1;
    const int table1 = (v>>37)&7;
    const int table2 = (v>>34)&7;
    int r1,g1,b1,r2,g2,b2;

    if (!diffbit)
    {
        const int B1=(v>>60)&0xF, B2=(v>>56)&0xF, G1=(v>>52)&0xF, G2=(v>>48)&0xF,
		  R1=(v>>44)&0xF, R2=(v>>40)&0xF;
        r1=(R1<<4)|R1; g1=(G1<<4)|G1; b1=(B1<<4)|B1;
        r2=(R2<<4)|R2; g2=(G2<<4)|G2; b2=(B2<<4)|B2;
    }
    else
    {
        const int B1=(v>>59)&0x1F, dB2=(v>>56)&7;
        const int G1=(v>>51)&0x1F, dG2=(v>>48)&7;
        const int R1=(v>>43)&0x1F, dR2=(v>>40)&7;
        const int sR2 = R1 + ( dR2&4 ? dR2-8 : dR2 );
        const int sG2 = G1 + ( dG2&4 ? dG2-8 : dG2 );
        const int sB2 = B1 + ( dB2&4 ? dB2-8 : dB2 );
        r1=(R1<<3)|(R1>>2); g1=(G1<<3)|(G1>>2); b1=(B1<<3)|(B1>>2);
        r2=(sR2<<3)|(sR2>>2); g2=(sG2<<3)|(sG2>>2); b2=(sB2<<3)|(sB2>>2);
    }

    const u32 low = (u32)(v & 0xFFFFFFFF);
    const u16 msb_plane = (low>>16)&0xFFFF;
    const u16 lsb_plane = low&0xFFFF;

    for ( int x = 0; x < 4; x++ )
    for ( int y = 0; y < 4; y++ )
    {
        const int p = x*4+y; // column-major pixel numbering
        const int msb = (msb_plane>>p)&1;
        const int lsb = (lsb_plane>>p)&1;
        const int sub = flipbit ? (y<2?0:1) : (x<2?0:1);
        const int table = sub==0 ? table1 : table2;
        const int R = sub==0 ? r1 : r2, G = sub==0 ? g1 : g2, B = sub==0 ? b1 : b2;
        int mod;
        if (msb && lsb)        mod = etc1_mod_table[table][0];
        else if (msb && !lsb)  mod = etc1_mod_table[table][1];
        else if (!msb && !lsb) mod = etc1_mod_table[table][2];
        else                   mod = etc1_mod_table[table][3];
        u8 *o = out + 4*(y*4+x);
        o[0] = etc1_clamp255(R+mod); o[1] = etc1_clamp255(G+mod); o[2] = etc1_clamp255(B+mod); o[3] = 255;
    }
}

// Decodes a plain ETC1 (BFLIM fmt 10, no alpha block -- opaque) tiled
// texture into RGBA8. Same block/tile arrangement as decode_etc1a4_tiled,
// just an 8-byte color-only block instead of 16 bytes.
static enumError decode_etc1_tiled ( u8 *rgba, const u8 *src, uint w, uint h, uint data_size )
{
    const uint tw = (w+7)&~7u, th = (h+7)&~7u;
    const uint bw = (tw+3)/4, bh = (th+3)/4;
    if ( (u64)bw*bh*8 > data_size )
        return EINVAL;
    for ( uint by = 0; by < bh; by++ )
    for ( uint bx = 0; bx < bw; bx++ )
    {
        const uint tile_idx = (by/2)*(bw/2) + bx/2;
        const uint local = (bx&1) | (by&1)<<1;
        const uint block_idx = tile_idx*4 + local;
        u8 px[64];
        decode_etc1_block(src + (u64)block_idx*8,px);
        for ( int ly = 0; ly < 4; ly++ )
        for ( int lx = 0; lx < 4; lx++ )
        {
            const uint x = bx*4+lx, y = by*4+ly;
            if ( x >= w || y >= h ) continue;
            memcpy(rgba + 4*(y*w+x), px + 4*(ly*4+lx), 4);
        }
    }
    return ERR_OK;
}

// Decodes an ETC1A4 (BFLIM fmt 11) tiled texture into RGBA8. Block
// arrangement follows the same 8x8-tile Morton scheme as this file's other
// tiled BFLIM formats (morton8), applied at 4x4-block granularity.
static enumError decode_etc1a4_tiled ( u8 *rgba, const u8 *src, uint w, uint h, uint data_size )
{
    const uint tw = (w+7)&~7u, th = (h+7)&~7u;
    const uint bw = (tw+3)/4, bh = (th+3)/4; // blocks across/down (tile-padded)
    if ( (u64)bw*bh*16 > data_size )
        return EINVAL;
    for ( uint by = 0; by < bh; by++ )
    for ( uint bx = 0; bx < bw; bx++ )
    {
        const uint tile_idx = (by/2)*(bw/2) + bx/2;
        const uint local = (bx&1) | (by&1)<<1;
        const uint block_idx = tile_idx*4 + local;
        const u8 *block = src + (u64)block_idx*16;
        u8 alpha4[8], color8[8];
        memcpy(alpha4,block,8);
        memcpy(color8,block+8,8);
        u8 px[64];
        decode_etc1_block(color8,px);
        for ( int ly = 0; ly < 4; ly++ )
        for ( int lx = 0; lx < 4; lx++ )
        {
            const uint x = bx*4+lx, y = by*4+ly;
            if ( x >= w || y >= h ) continue;
            const int p = lx*4+ly; // same column-major numbering as color
            const u8 nib = (p&1) ? (alpha4[p>>1]>>4) : (alpha4[p>>1]&0xF);
            u8 *d = rgba + 4*(y*w+x);
            const u8 *s = px + 4*(ly*4+lx);
            d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=(u8)(nib*17);
        }
    }
    return ERR_OK;
}

enumError DecodeFLIM_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
)
{
    if (!dest || !width || !height || !src || src_size < 0x28)
        return EINVAL;
    const u8 *foot = src + src_size - 0x28;
    if ( (memcmp(foot,"FLIM",4) && memcmp(foot,"CLIM",4))
        || (foot[4] != 0xfe || foot[5] != 0xff)
        && (foot[4] != 0xff || foot[5] != 0xfe) )
        return EINVAL;
    const bool be = foot[4] == 0xfe;
    u16 (*r16)(const u8*) = be ? rd_be16 : rd_le16;
    u32 (*r32)(const u8*) = be ? rd_be32 : rd_le32;
    if ( r16(foot+6) != 0x14 || memcmp(foot+0x14,"imag",4)
        || r32(foot+0x18) != 0x10 )
        return EINVAL;
    const uint w = r16(foot+0x1c), h = r16(foot+0x1e);
    const uint fmt = foot[0x22], tile_mode = foot[0x23] & 31;
    const uint data_size = r32(src+src_size-4);

    if ( fmt == 10 || fmt == 11 ) // ETC1 (fmt 10, opaque) / ETC1A4 (fmt 11): block-compressed
    {
        if (!w || !h || w > 16384 || h > 16384 || data_size > src_size-0x28)
            return EINVAL;
        if ( (u64)w*h > NFMT_MAX_OUTPUT/4 )
            return EINVAL;
        u8 *rgba = MALLOC(w*h*4);
        if (!rgba) return ERR_CANT_CREATE;
        enumError err = fmt == 11
            ? decode_etc1a4_tiled(rgba,src,w,h,data_size)
            : decode_etc1_tiled(rgba,src,w,h,data_size);
        if (err) { FREE(rgba); return err; }
        *dest = rgba;
        *width = w;
        *height = h;
        return ERR_OK;
    }

    const bool nibble_fmt = fmt == 12 || fmt == 13; // L4 / A4: 4 bits/pixel
    uint bpp;
    switch (fmt)
    {
        case 0: case 1: case 2: bpp = 1; break;
        case 12: case 13: bpp = 0; break; // nibble_fmt: handled separately below
        case 3: case 5: case 7: case 8: bpp = 2; break;
        case 9: case 20: bpp = 4; break;
        default: return EINVAL;
    }
    if (!w || !h || w > 16384 || h > 16384 || data_size > src_size-0x28)
        return EINVAL;
    const uint tw = (w+7)&~7u, th = (h+7)&~7u;
    const u64 need = nibble_fmt
	? ( (u64)(tile_mode ? tw : w) * (tile_mode ? th : h) + 1 ) / 2
	: (u64)(tile_mode ? tw : w) * (tile_mode ? th : h) * bpp;
    if (need > data_size || (u64)w*h > NFMT_MAX_OUTPUT/4)
        return EINVAL;
    u8 *rgba = MALLOC(w*h*4);
    if (!rgba) return ERR_CANT_CREATE;
    for (uint y = 0; y < h; y++)
        for (uint x = 0; x < w; x++)
        {
            uint pos;
            if (tile_mode)
                pos = ( (y/8)*(tw/8) + x/8 ) * 64 + morton8(x&7,y&7);
            else
                pos = y*w + x;
            u8 *d = rgba + 4*(y*w+x);
            if (nibble_fmt)
            {
                const u8 byte = src[pos>>1];
                const u8 nib = (pos&1) ? (byte>>4) : (byte&0xF);
                const u8 v = (u8)(nib*17);
                d[0] = d[1] = d[2] = d[3] = v;
                continue;
            }
            const u8 *p = src + pos*bpp;
            if (fmt == 0 || fmt == 1)
                d[0] = d[1] = d[2] = d[3] = p[0];
            else if (fmt == 2) // LA4: low nibble = luminance, high nibble = alpha
            {
                d[0] = d[1] = d[2] = (u8)((p[0]&0xF)*17);
                d[3] = (u8)((p[0]>>4)*17);
            }
            else if (fmt == 3) // LA8: byte0 = luminance, byte1 = alpha
            {
                d[0] = d[1] = d[2] = p[0];
                d[3] = p[1];
            }
            else if (fmt == 5)
            {
                const u16 c = r16(p);
                d[0] = expand5(c>>11); d[1] = (c>>5 & 63)*255/63;
                d[2] = expand5(c); d[3] = 255;
            }
            else if (fmt == 7)
            {
                const u16 c = r16(p);
                d[0] = expand5(c>>11); d[1] = expand5(c>>6); d[2] = expand5(c>>1);
                d[3] = c&1 ? 255 : 0;
            }
            else if (fmt == 8)
            {
                const u16 c = r16(p);
                d[0] = (c>>12)*17; d[1] = (c>>8&15)*17;
                d[2] = (c>>4&15)*17; d[3] = (c&15)*17;
            }
            else // CTR/GX2 RGBA8 byte storage is A,B,G,R.
                { d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0]; }
        }
    *dest = rgba;
    *width = w;
    *height = h;
    return ERR_OK;
}

enumError EncodeFLIM_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
    bool bclim
)
{
    if (!dest || !dest_size || !rgba || !width || !height
        || width > 16384 || height > 16384)
        return EINVAL;
    const uint tw = (width+7)&~7u, th = (height+7)&~7u;
    const u64 pixels = (u64)tw*th;
    if (pixels > (NFMT_MAX_OUTPUT-0x28)/4) return EFBIG;
    const uint image_size = 4*pixels, total = image_size + 0x28;
    u8 *out = CALLOC(1,total);
    if (!out) return ERR_CANT_CREATE;
    for (uint y = 0; y < height; y++)
        for (uint x = 0; x < width; x++)
        {
            const uint pos = ( (y/8)*(tw/8) + x/8 ) * 64 + morton8(x&7,y&7);
            const u8 *s = rgba + 4*(y*width+x);
            u8 *d = out + 4*pos;
            d[0] = s[3]; d[1] = s[2]; d[2] = s[1]; d[3] = s[0]; // A,B,G,R
        }
    u8 *foot = out + image_size;
    memcpy(foot,bclim ? "CLIM" : "FLIM",4);
    foot[4] = 0xff; foot[5] = 0xfe; // little endian BOM
    wr_le16(foot+6,0x14);
    wr_le32(foot+8,0x00020002); // BFLIM v2.2, accepted by CTR readers
    wr_le32(foot+0x0c,total);
    wr_le16(foot+0x10,1);
    memcpy(foot+0x14,"imag",4);
    wr_le32(foot+0x18,0x10);
    wr_le16(foot+0x1c,width); wr_le16(foot+0x1e,height);
    wr_le16(foot+0x20,1);
    foot[0x22] = 9; // RGBA8
    foot[0x23] = 1; // 8x8 Morton tiles
    wr_le32(foot+0x24,image_size);
    *dest = out;
    *dest_size = total;
    return ERR_OK;
}

static inline u16 sarc16 ( const nintendo_sarc_t *s, const u8 *p )
    { return s->big_endian ? rd_be16(p) : rd_le16(p); }
static inline u32 sarc32 ( const nintendo_sarc_t *s, const u8 *p )
    { return s->big_endian ? rd_be32(p) : rd_le32(p); }

enumError ScanSARC ( nintendo_sarc_t *sarc, const u8 *data, uint size )
{
    if (!sarc || !data || size < 0x20 || memcmp(data,"SARC",4))
        return EINVAL;
    memset(sarc,0,sizeof(*sarc));
    sarc->data = data;
    sarc->size = size;
    // The BOM is stored in the file's byte order, independently of host CPU.
    if (data[6] == 0xfe && data[7] == 0xff) sarc->big_endian = true;
    else if (data[6] == 0xff && data[7] == 0xfe) sarc->big_endian = false;
    else return EINVAL;
    const uint header_size = sarc16(sarc,data+4);
    const uint file_size = sarc32(sarc,data+8);
    sarc->data_offset = sarc32(sarc,data+0x0c);
    if (header_size < 0x14 || header_size > size || file_size > size
        || sarc->data_offset > file_size || header_size + 12 > file_size
        || memcmp(data+header_size,"SFAT",4))
        return EINVAL;
    const uint sfat_size = sarc16(sarc,data+header_size+4);
    sarc->n_entries = sarc16(sarc,data+header_size+6);
    if (sfat_size < 12 || sarc->n_entries > (file_size-header_size-12)/16
        || header_size + sfat_size + 16*sarc->n_entries + 8 > file_size)
        return EINVAL;
    sarc->entries_offset = header_size + sfat_size;
    sarc->sfnt_offset = sarc->entries_offset + 16*sarc->n_entries;
    if (memcmp(data+sarc->sfnt_offset,"SFNT",4)
        || sarc16(sarc,data+sarc->sfnt_offset+4) < 8)
        return EINVAL;
    return ERR_OK;
}

enumError GetSARCEntry
(
    const nintendo_sarc_t *sarc, uint index, ccp *name,
    const u8 **data, uint *size
)
{
    if (!sarc || !sarc->data || index >= sarc->n_entries)
        return EINVAL;
    const u8 *node = sarc->data + sarc->entries_offset + 16*index;
    const u32 attr = sarc32(sarc,node+4);
    const uint begin = sarc32(sarc,node+8), end = sarc32(sarc,node+12);
    if (begin > end || end > sarc->size-sarc->data_offset)
        return EINVAL;
    if (name)
    {
        if (!(attr >> 24)) return EINVAL;
        const uint noff = sarc->sfnt_offset + 8 + 4*(attr & 0x00ffffff);
        if (noff >= sarc->size || !memchr(sarc->data+noff,0,sarc->size-noff))
            return EINVAL;
        *name = (ccp)sarc->data + noff;
    }
    if (data) *data = sarc->data + sarc->data_offset + begin;
    if (size) *size = end - begin;
    return ERR_OK;
}

typedef struct sarc_sort_t
{
    const nintendo_sarc_entry_t *entry;
    u32 hash;
}
sarc_sort_t;

static u32 hash_sarc_name ( ccp name )
{
    u32 hash = 0;
    while (*name)
        hash = hash * 0x65 + (u8)*name++;
    return hash;
}

static int cmp_sarc_entry ( const void *a, const void *b )
{
    const sarc_sort_t *sa = a, *sb = b;
    if (sa->hash != sb->hash) return sa->hash < sb->hash ? -1 : 1;
    return strcmp(sa->entry->name,sb->entry->name);
}

enumError CreateSARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
    uint n_entries, bool big_endian
)
{
    if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xffff)
        return EINVAL;
    sarc_sort_t *sorted = CALLOC(n_entries,sizeof(*sorted));
    if (!sorted) return ERR_CANT_CREATE;
    uint names_size = 8, data_size = 0;
    for (uint i = 0; i < n_entries; i++)
    {
        if (!entries[i].name || !*entries[i].name || !entries[i].data)
            { FREE(sorted); return EINVAL; }
        const size_t name_len = strlen(entries[i].name) + 1;
        if (name_len > UINT_MAX || names_size > UINT_MAX - ((name_len+3)&~3u)
            || data_size > UINT_MAX - entries[i].size)
            { FREE(sorted); return EFBIG; }
        sorted[i].entry = entries + i;
        sorted[i].hash = hash_sarc_name(entries[i].name);
        names_size += (name_len+3) & ~3u;
        data_size += entries[i].size;
    }
    qsort(sorted,n_entries,sizeof(*sorted),cmp_sarc_entry);
    if (names_size > UINT_MAX - (0x20 + 16*n_entries))
        { FREE(sorted); return EFBIG; }
    const uint tables_size = 0x20 + 16*n_entries + names_size;
    const uint data_offset = (tables_size + 0xff) & ~0xffu;
    if (tables_size > UINT_MAX-0xff || data_offset > UINT_MAX-data_size)
        { FREE(sorted); return EFBIG; }
    const uint total = data_offset + data_size;
    u8 *out = CALLOC(1,total);
    if (!out) { FREE(sorted); return ERR_CANT_CREATE; }
    void (*w16)(u8*,u16) = big_endian ? wr_be16 : wr_le16;
    void (*w32)(u8*,u32) = big_endian ? wr_be32 : wr_le32;
    memcpy(out,"SARC",4);
    w16(out+4,0x14);
    // Store the same BOM value in the file's byte order: FE FF means big,
    // FF FE means little in the raw byte stream.
    w16(out+6,0xfeff);
    w32(out+8,total);
    w32(out+0x0c,data_offset);
    w16(out+0x10,0x0100);
    memcpy(out+0x14,"SFAT",4);
    w16(out+0x18,12); w16(out+0x1a,n_entries); w32(out+0x1c,0x65);
    const uint sfnt = 0x20 + 16*n_entries;
    memcpy(out+sfnt,"SFNT",4); w16(out+sfnt+4,8);
    uint name_pos = sfnt + 8, data_pos = data_offset;
    for (uint i = 0; i < n_entries; i++)
    {
        const nintendo_sarc_entry_t *entry = sorted[i].entry;
        u8 *node = out + 0x20 + 16*i;
        w32(node,sorted[i].hash);
        w32(node+4,0x01000000 | ((name_pos-(sfnt+8))/4));
        w32(node+8,data_pos-data_offset);
        memcpy(out+name_pos,entry->name,strlen(entry->name)+1);
        name_pos += (strlen(entry->name)+1+3) & ~3u;
        memcpy(out+data_pos,entry->data,entry->size);
        data_pos += entry->size;
        w32(node+12,data_pos-data_offset);
    }
    FREE(sorted);
    *dest = out;
    *dest_size = total;
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		GFA (Good-Feel archive) support		///////////////
///////////////////////////////////////////////////////////////////////////////

// Raw LZ10: identical token format to the standard Nintendo LZ10 stream, but
// without the 4-byte (0x10 + 24-bit size) header, so the caller supplies the
// output size. This is what GFCP compression types 2 and 3 use.
enumError DecodeLZ10Raw ( u8 *dest, uint dest_size, const u8 *src, uint src_size )
{
    if ( !dest || !src ) return EINVAL;
    uint sp = 0, dp = 0;
    while ( dp < dest_size )
    {
	if ( sp >= src_size ) return EINVAL;
	u8 flags = src[sp++];
	for ( uint bit = 0; bit < 8 && dp < dest_size; bit++, flags <<= 1 )
	{
	    if (!(flags & 0x80))
	    {
		if ( sp >= src_size ) return EINVAL;
		dest[dp++] = src[sp++];
	    }
	    else
	    {
		if ( sp+2 > src_size ) return EINVAL;
		const u8 a = src[sp++], b = src[sp++];
		const uint len = (a>>4)+3, back = ((a&15)<<8|b)+1;
		if ( back > dp || len > dest_size-dp ) return EINVAL;
		for ( uint i = 0; i < len; i++, dp++ )
		    dest[dp] = dest[dp-back];
	    }
	}
    }
    return ERR_OK;
}

// Byte Pair Encoding, GFCP compression type 1. Each block starts with a pair
// table: a control byte >= 0x80 means (byte-0x7F) literals follow, otherwise
// it introduces (byte+1) expansions for one key. Expanded bytes are pushed
// through a stack so nested pairs resolve recursively.
// Ported from QuickBMS's "BPE" comtype (compression/bpe.c, itself credited
// to Philip Gage's classic compress.c, C Users Journal Feb 1994) -- this is
// what aluigi's kirby_epic_yarn.bms uses for GFCP zip-mode 1. The encoder
// side (filewrite() in that source) is the only place this table encoding
// is actually documented, so the decoder here is derived by inverting it.
//
// The pair table for c=0..255 is written as a run-length stream where each
// marker byte is EITHER:
//   >127  a run of (marker-127) literal positions (table[c]==c, no bytes
//         follow for them) -- but the run always stops one short of a real
//         pair, and that ONE pair entry is written immediately after with
//         no marker of its own (the encoder's `len=0; ...; c==256?break:`
//         reset before falling into the shared write loop, which then
//         executes exactly once).
//   <=127 a run of (marker+1) consecutive table entries, each written as
//         1 byte (still literal, table[c]==c by coincidence) or 2 bytes
//         (a real pair, left+right).
// Getting the ">127 run implies exactly one trailing pair entry, not a
// fresh marker" part wrong is what silently desynced the whole stream on
// every previously-untested real sample (verified against retail Kirby's
// Epic Yarn GFA data, which round-trips byte-exact with this version but
// not the naive "each marker is independent" reading).
enumError DecodeBPE ( u8 *dest, uint dest_size, const u8 *src, uint src_size )
{
    if ( !dest || !src ) return EINVAL;
    uint sp = 0, dp = 0;

    while ( dp < dest_size )
    {
	u8 table[256][2];
	for ( uint i = 0; i < 256; i++ )
	{
	    table[i][0] = (u8)i;
	    table[i][1] = 0;
	}
	// A byte is "paired" when table[i][1] is used; track that separately
	// so a legitimate 0 expansion byte is not mistaken for "unpaired".
	bool paired[256];
	memset(paired,0,sizeof(paired));

	// pair table
	uint c = 0;
	while ( c < 256 )
	{
	    if ( sp >= src_size ) return EINVAL;
	    uint marker = src[sp++];

	    uint entries;
	    if ( marker > 127 )
	    {
		c += marker - 127; // these stay literal, no bytes for them
		if ( c == 256 )
		    break;
		entries = 1; // the pair that terminated the literal run
	    }
	    else
		entries = marker + 1;

	    for ( uint i = 0; i < entries && c < 256; i++, c++ )
	    {
		if ( sp >= src_size ) return EINVAL;
		const u8 lc = src[sp++];
		table[c][0] = lc;
		if ( lc != (u8)c )
		{
		    if ( sp >= src_size ) return EINVAL;
		    table[c][1] = src[sp++];
		    paired[c] = true;
		}
	    }
	}

	if ( sp+2 > src_size ) return EINVAL;
	uint block_len = (uint)src[sp]<<8 | src[sp+1];
	sp += 2;

	// Pair codes are assigned strictly downward from 255 and a code can
	// only reference lower codes, so a chain can nest at most 256 deep;
	// 128 was too small for real data -- found on a retail sample
	// (z100_tutorial01.gfa) that needs depth 139, one of 13 real Kirby's
	// Epic Yarn archives that silently failed to decode until this was
	// sized to the actual worst case instead of a guessed round number.
	u8 stack[256];
	uint sn = 0;
	while ( block_len || sn )
	{
	    u8 b;
	    if (sn)
		b = stack[--sn];
	    else
	    {
		if ( sp >= src_size ) return EINVAL;
		b = src[sp++];
		block_len--;
	    }

	    if (paired[b])
	    {
		if ( sn+2 > sizeof(stack) ) return EINVAL;
		stack[sn++] = table[b][1];
		stack[sn++] = table[b][0];
	    }
	    else
	    {
		if ( dp >= dest_size ) return EINVAL;
		dest[dp++] = b;
	    }
	}
    }
    return ERR_OK;
}

void ResetGFA ( gfa_t *gfa )
{
    if (!gfa) return;
    FREE(gfa->blob);
    FREE(gfa->entries);
    FREE(gfa->names);
    memset(gfa,0,sizeof(*gfa));
}

enumError ScanGFA ( gfa_t *gfa, const u8 *data, uint size )
{
    if ( !gfa || !data || size < 0x1c || memcmp(data,"GFAC",4) )
	return EINVAL;
    memset(gfa,0,sizeof(*gfa));

    const u32 info_off  = rd_le32(data+0x0c);
    const u32 data_off  = rd_le32(data+0x14);
    const u32 data_size = rd_le32(data+0x18);

    if ( info_off+4 > size || data_off+16 > size )
	return EINVAL;
    if ( (u64)data_off + data_size > size )
	return EINVAL;

    const u32 n = rd_le32(data+info_off);
    if ( !n || n > 0x100000 || (u64)info_off+4 + (u64)n*16 > size )
	return EINVAL;

    // GFCP payload
    const u8 *gfcp = data + data_off;
    if ( memcmp(gfcp,"GFCP",4) )
	return EINVAL;
    const u32 zip     = rd_le32(gfcp+8);
    const u32 out_len = rd_le32(gfcp+12);
    const u32 zsize   = rd_le32(gfcp+16);
    if ( !out_len || out_len > NFMT_MAX_OUTPUT )
	return EFBIG;
    if ( (u64)20 + zsize > data_size )
	return EINVAL;

    u8 *blob = MALLOC(out_len);
    if (!blob) return ERR_CANT_CREATE;
    enumError err;
    switch (zip)
    {
	case 1:  err = DecodeBPE(blob,out_len,gfcp+20,zsize); break;
	case 2:
	case 3:  err = DecodeLZ10Raw(blob,out_len,gfcp+20,zsize); break;
	default: err = EINVAL; break;
    }
    if (err) { FREE(blob); return err; }

    // entry table
    gfa_entry_t *entries = CALLOC(n,sizeof(*entries));
    char *names = CALLOC(1,size); // names live inside the source file
    if ( !entries || !names )
    {
	FREE(blob); FREE(entries); FREE(names);
	return ERR_CANT_CREATE;
    }
    uint name_pos = 0;

    const u8 *rec = data + info_off + 4;
    for ( uint i = 0; i < n; i++, rec += 16 )
    {
	u32 name_off = rd_le32(rec+4) & 0x00ffffff;
	const u32 fsize = rd_le32(rec+8);
	u32 offset = rd_le32(rec+12);

	if ( name_off >= size ) { name_off = 0; }
	// copy the NUL-terminated name out of the source buffer
	entries[i].name = names+name_pos;
	if (name_off)
	{
	    uint j = name_off;
	    while ( j < size && data[j] && name_pos+1 < size )
		names[name_pos++] = (char)data[j++];
	}
	names[name_pos++] = 0;

	entries[i].size = fsize;
	entries[i].offset = offset >= data_off ? offset - data_off : offset;
	if ( fsize && ( entries[i].offset > out_len || fsize > out_len - entries[i].offset ) )
	{
	    // Out-of-range member: clamp to empty rather than reading past the blob.
	    entries[i].size = 0;
	    entries[i].offset = 0;
	}
    }

    gfa->blob = blob;
    gfa->blob_size = out_len;
    gfa->entries = entries;
    gfa->n_entries = n;
    gfa->names = names;
    gfa->compression = zip;
    return ERR_OK;
}

enumError CreateGFA
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
)
{
    if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
        return EINVAL;
        
    uint payload_size = 0;
    uint names_size = 0;
    for (uint i = 0; i < n_entries; i++)
    {
        if (!entries[i].name) return EINVAL;
        names_size += strlen(entries[i].name) + 1;
        payload_size += entries[i].size;
    }
    
    u8 *payload = CALLOC(1, payload_size ? payload_size : 1);
    if (!payload) return ERR_CANT_CREATE;
    
    uint current_offset = 0;
    for (uint i = 0; i < n_entries; i++)
    {
        if (entries[i].size)
        {
            memcpy(payload + current_offset, entries[i].data, entries[i].size);
            current_offset += entries[i].size;
        }
    }
    
    u8 *zdata = 0;
    uint zsize = 0;
    enumError err = EncodeLZ10Raw(&zdata, &zsize, payload, payload_size);
    FREE(payload);
    if (err) return err;
    
    const uint info_off = 0x20;
    const uint names_off = info_off + 4 + 16 * n_entries;
    uint data_off = names_off + names_size;
    data_off = (data_off + 3) & ~3u;
    
    const uint data_size = 20 + zsize;
    const uint total_size = data_off + data_size;
    
    u8 *out = CALLOC(1, total_size);
    if (!out) { FREE(zdata); return ERR_CANT_CREATE; }
    
    memcpy(out, "GFAC", 4);
    wr_le32(out + 0x0c, info_off);
    wr_le32(out + 0x14, data_off);
    wr_le32(out + 0x18, data_size);
    
    wr_le32(out + info_off, n_entries);
    
    uint name_pos = 0;
    current_offset = 0;
    for (uint i = 0; i < n_entries; i++)
    {
        u8 *rec = out + info_off + 4 + 16 * i;
        wr_le32(rec + 4, names_off + name_pos);
        wr_le32(rec + 8, entries[i].size);
        wr_le32(rec + 12, current_offset);
        
        size_t nlen = strlen(entries[i].name) + 1;
        memcpy(out + names_off + name_pos, entries[i].name, nlen);
        name_pos += nlen;
        current_offset += entries[i].size;
    }
    
    u8 *gfcp = out + data_off;
    memcpy(gfcp, "GFCP", 4);
    wr_le32(gfcp + 8, 3);
    wr_le32(gfcp + 12, payload_size);
    wr_le32(gfcp + 16, zsize);
    memcpy(gfcp + 20, zdata, zsize);
    FREE(zdata);

    *dest = out;
    *dest_size = total_size;
    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		PAC (Brawl "ARC\0" archive) support	///////////////
//-----------------------------------------------------------------------------

void ResetPAC ( pac_t *pac )
{
    if (!pac) return;
    FREE(pac->entries);
    memset(pac,0,sizeof(*pac));
}

// ARCHeader (BrawlLib SSBB/Types/ARC.cs): tag(4)="ARC\0", _version(ushort,
// native -- both bytes 0x01 so the file's byte order never actually matters
// here), _numFiles(bushort, big-endian) at 0x06, two reserved u32 at
// 0x08/0x0c, then a 48-byte fixed name buffer at 0x10. Struct size 0x40; the
// first ARCFileHeader follows immediately.
//
// ARCFileHeader is 0x20 bytes: bshort type(0), bshort index(2), bint
// size(4), byte groupIndex(8), byte padding(9), bshort redirectIndex(10),
// then 20 bytes of reserved bint padding out to 0x20. Its data starts right
// after (offset+0x20) and the *next* header is
// round_up(data_offset + size, 0x20) -- BrawlLib computes this by aligning
// the raw data-end pointer to the header struct's own size, which happens
// to also be 32, i.e. plain 32-byte alignment from the start of the file
// (0x40 is itself 32-aligned, so relative and absolute alignment coincide).
//
// Verified field-by-field against a real retail file (SSSG2 Ultimate's
// FitIke.pac, 552416 bytes): tag="ARC\0", numFiles=2, name="FitPeach" (the
// dogfooded template name BrawlLib's tools left behind -- harmless, it's
// unused metadata), first entry type=1 (MiscData) size=0x00021ff1=139249,
// whose data (at header+0x20=0x60) begins with what looks like a
// SakuraiArchive block header, consistent with Brawl's per-character
// "moveset" data always being MiscData entry 0.
enumError ScanPAC ( pac_t *pac, const u8 *data, uint size )
{
    if ( !pac || !data || size < 0x40 || memcmp(data,"ARC\0",4) )
	return EINVAL;
    if ( data[4] != 1 || data[5] != 1 )
	return EINVAL; // version must be 0x0101

    const uint n = rd_be16(data+6);
    if ( !n || n > 0x10000 )
	return EINVAL;

    memset(pac,0,sizeof(*pac));
    pac->data = data;
    pac->size = size;
    memcpy(pac->name,data+0x10,sizeof(pac->name)-1);
    pac->name[sizeof(pac->name)-1] = 0;

    pac_entry_t *entries = CALLOC(n,sizeof(*entries));
    if (!entries) return ERR_CANT_CREATE;

    uint off = 0x40, i;
    for ( i = 0; i < n; i++ )
    {
	if ( off+0x20 > size )
	    break;
	const u8 *h = data+off;
	const u16 type  = rd_be16(h);
	const u16 index = rd_be16(h+2);
	const u32 fsize = rd_be32(h+4);
	const u8  group = h[8];
	const s16 redirect = (s16)rd_be16(h+10);

	const u32 data_off = off+0x20;
	if ( (u64)data_off + fsize > size )
	    break;

	entries[i].type = type;
	entries[i].index = index;
	entries[i].group_index = group;
	entries[i].redirect_index = redirect;
	entries[i].size = fsize;
	entries[i].data = data+data_off;

	const u64 next = ((u64)data_off + fsize + 0x1f) & ~(u64)0x1f;
	if ( next <= off || next > size )
	{
	    i++;
	    break;
	}
	off = (uint)next;
    }

    if (!i) { FREE(entries); return EINVAL; }
    pac->entries = entries;
    pac->n_entries = i;
    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		DARC (3DS "darc" archive) support	///////////////
//-----------------------------------------------------------------------------

void ResetDARC ( darc_t *darc )
{
    if (!darc) return;
    if (darc->entries)
        for ( uint i = 0; i < darc->n_entries; i++ )
            FREE(darc->entries[i].name);
    FREE(darc->entries);
    memset(darc,0,sizeof(*darc));
}

// NUL-terminated UTF-16LE -> malloc'd UTF-8. DARC names are game asset/path
// components (western titles observed so far are plain ASCII), so this only
// needs to be correct, not fast; surrogate pairs are handled for
// completeness even though no real sample has needed one yet.
static char * darc_utf16le_to_utf8 ( const u8 *p, const u8 *end )
{
    uint cap = 4, len = 0;
    char *out = MALLOC(cap);
    if (!out) return 0;
    while ( p+2 <= end )
    {
        const u16 u = rd_le16(p);
        p += 2;
        if (!u) break;
        u32 cp = u;
        if ( u >= 0xD800 && u <= 0xDBFF && p+2 <= end )
        {
            const u16 lo = rd_le16(p);
            if ( lo >= 0xDC00 && lo <= 0xDFFF )
                { cp = 0x10000 + ((u-0xD800)<<10) + (lo-0xDC00); p += 2; }
            else
                cp = 0xFFFD;
        }
        else if ( u >= 0xD800 && u <= 0xDFFF )
            cp = 0xFFFD;

        char enc[4];
        uint n;
        if (cp < 0x80) { enc[0] = (char)cp; n = 1; }
        else if (cp < 0x800)
            { enc[0] = 0xC0|(cp>>6); enc[1] = 0x80|(cp&0x3f); n = 2; }
        else if (cp < 0x10000)
            { enc[0] = 0xE0|(cp>>12); enc[1] = 0x80|((cp>>6)&0x3f);
              enc[2] = 0x80|(cp&0x3f); n = 3; }
        else
            { enc[0] = 0xF0|(cp>>18); enc[1] = 0x80|((cp>>12)&0x3f);
              enc[2] = 0x80|((cp>>6)&0x3f); enc[3] = 0x80|(cp&0x3f); n = 4; }

        if ( len+n+1 > cap )
        {
            cap = (len+n+1)*2;
            char *grown = REALLOC(out,cap);
            if (!grown) { FREE(out); return 0; }
            out = grown;
        }
        memcpy(out+len,enc,n);
        len += n;
    }
    out[len] = 0;
    return out;
}

// Header/entry layout: see the long comment above darc_t in lib-nintendo.h.
// Verified byte-for-byte against a real retail 3DS sample (Tomodachi Life,
// CTR-P-EC6E, romfs/layout/*.bin) as well as GBATEK, 3dbrew, and Tyulis/
// 3DSkit's reference unpacker.
enumError ScanDARC ( darc_t *darc, const u8 *data, uint size )
{
    if ( !darc || !data || size < 0x1c || memcmp(data,"darc",4) )
        return EINVAL;
    if ( rd_le16(data+4) != 0xfeff )
        return EINVAL; // only little-endian DARC has ever been observed

    const uint header_size = rd_le16(data+6);
    const uint file_size    = rd_le32(data+0xc);
    const uint table_offset = rd_le32(data+0x10);
    const uint table_size   = rd_le32(data+0x14);
    if ( header_size < 0x1c || file_size > size
        || table_offset < header_size || table_size < 12
        || (u64)table_offset + table_size > size )
        return EINVAL;

    const uint n = table_size/12; // upper bound; real count comes from entry 0 below
    if ( !n || (u64)table_offset + 12 > size )
        return EINVAL;

    const u32 e0 = rd_le32(data+table_offset);
    if ( !(e0 & 0x01000000) ) // entry 0 must be the root directory
        return EINVAL;
    const uint n_entries = rd_le32(data+table_offset+8); // root's end-index
    if ( !n_entries || n_entries > n || (u64)table_offset + (u64)n_entries*12 > size )
        return EINVAL;

    const u8 *name_area = data + table_offset + n_entries*12;
    const u8 *name_area_end = data + (table_offset+table_size <= size ? table_offset+table_size : size);

    darc_entry_t *entries = CALLOC(n_entries,sizeof(*entries));
    if (!entries) return ERR_CANT_CREATE;

    for ( uint i = 0; i < n_entries; i++ )
    {
        const u8 *e = data + table_offset + i*12;
        const u32 f0 = rd_le32(e);
        const u32 f1 = rd_le32(e+4);
        const u32 f2 = rd_le32(e+8);
        const bool is_dir = (f0 & 0x01000000) != 0;
        const uint name_off = f0 & 0xffffff;

        entries[i].is_dir = is_dir;
        entries[i].parent_or_offset = f1;
        entries[i].end_or_size = f2;

        if ( name_area + name_off < name_area_end )
            entries[i].name = darc_utf16le_to_utf8(name_area+name_off,name_area_end);
        if (!entries[i].name)
            entries[i].name = STRDUP("");

        if ( !is_dir && ( (u64)f1 + f2 > size ) )
        {
            for ( uint k = 0; k <= i; k++ ) FREE(entries[k].name);
            FREE(entries);
            return EINVAL; // a file's data must stay inside the archive
        }
    }

    darc->data = data;
    darc->size = size;
    darc->entries = entries;
    darc->n_entries = n_entries;
    return ERR_OK;
}
