#include "lib-std.h"
#include "lib-nintendo.h"

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
        "BFLIM", "BCLIM", "BNR", "NCGR", "NCLR", "NCER", "NANR", "BRFNT", "BRFNA", "BRLAN", "BRLYT",
        "BFLAN", "BFLYT", "BCLAN", "BCLYT", "PLT0", "MSBT", "BCRES", "BFRES", "BNTX"
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
        // BNTX (Switch texture container). Detected but not yet decoded --
        // full support needs the Tegra block-linear GOB swizzle algorithm,
        // which this fork could not verify against a real sample or an
        // independent reference decoder, so it's deliberately left
        // unimplemented rather than guessed at (see lib-nintendo.h).
        if (!memcmp(d,"BNTX",4)) return make_info(NFMT_BNTX,false,false,0);
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
            const uint child = ((node+tree_base) & ~1u) - tree_base
                + 2 + 2*(entry&0x3f) + bit;
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
