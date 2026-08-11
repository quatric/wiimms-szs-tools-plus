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
        "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "RL", "ASH0", "Yay0",
        "BFLIM", "BCLIM", "BNR", "NCGR", "NCER", "NANR", "BRFNT", "BRFNA", "BRLAN", "BRLYT",
        "BFLAN", "BFLYT", "BCLAN", "BCLYT", "MSBT", "BCRES", "BFRES"
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
        if (!memcmp(d,"MsgStdBn",4)) return make_info(NFMT_MSBT,true,false,0);
        if (!memcmp(d,"CGFX",4)) return make_info(NFMT_BCRES,true,false,0);
        if (!memcmp(d,"FRES",4)) return make_info(NFMT_BFRES,true,false,0);
        if ( (d[0] == 0x10 || d[0] == 0x11) && size >= 4 )
            return make_info(d[0] == 0x10 ? NFMT_LZ10 : NFMT_LZ11, false, true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
        if ( d[0] == 0x30 && size >= 4 )
            return make_info(NFMT_RL,false,true,
                (u32)d[1] | (u32)d[2]<<8 | (u32)d[3]<<16 );
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

static uint morton8 ( uint x, uint y )
{
    return (x&1) | (y&1)<<1 | (x&2)<<1 | (y&2)<<2 | (x&4)<<2 | (y&4)<<3;
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
    uint bpp;
    switch (fmt)
    {
        case 0: case 1: bpp = 1; break;
        case 5: case 7: case 8: bpp = 2; break;
        case 9: case 20: bpp = 4; break;
        default: return EINVAL;
    }
    if (!w || !h || w > 16384 || h > 16384 || data_size > src_size-0x28)
        return EINVAL;
    const uint tw = (w+7)&~7u, th = (h+7)&~7u;
    const u64 need = (u64)(tile_mode ? tw : w) * (tile_mode ? th : h) * bpp;
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
            const u8 *p = src + pos*bpp;
            u8 *d = rgba + 4*(y*w+x);
            if (fmt == 0 || fmt == 1)
                d[0] = d[1] = d[2] = d[3] = p[0];
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
