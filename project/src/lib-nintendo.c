#include "lib-std.h"
#include "lib-nintendo.h"

#define NFMT_MAX_OUTPUT (512u<<20)

static inline u32 rd_be32 ( const u8 *p )
    { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static inline u32 rd_le32 ( const u8 *p )
    { return (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0]; }

ccp GetNintendoFormatName ( nfmt_type_t type )
{
    static const ccp tab[] = {
        "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "ASH0", "Yay0",
        "BFLIM", "BCLIM", "NCGR", "NCER", "NANR", "BRFNT", "BRFNA", "BRLAN", "BRLYT",
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
        // Camelot header: codec 1/2 plus a three-byte output size. The extension
        // check prevents random binary files from being called STPL.
        if ( (d[0] == 1 || d[0] == 2) && filename && strstr(filename,".stpl") )
            return make_info(NFMT_STPL,true,true,((u32)d[1]<<16)|((u32)d[2]<<8)|d[3]);
    }
    if ( size >= 0x28 && !memcmp(d+size-0x28,"FLIM",4) ) return make_info(NFMT_BFLIM,true,false,0);
    if ( size >= 4 && !memcmp(d,"CLIM",4) ) return make_info(NFMT_BCLIM,true,false,0);
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
