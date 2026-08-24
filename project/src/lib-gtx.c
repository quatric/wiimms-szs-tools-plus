// GTX/GSH (Wii U "Gfx2" texture/shader container) -- see lib-gtx.h.
//
// The tiled-surface addressing here is a scoped port of AMD's addrlib as
// used by GX2 on Wii U hardware (fixed 2 pipes / 4 banks -- Wii U's Latte
// GPU never uses any other pipe/bank config, unlike desktop AMD parts, so
// the general parametrized addrlib is overkill; the bit shifts below bake
// those constants in directly, matching the reference implementation
// (aboood40091/GTX-Extractor's addrlib.py, itself derived from the real
// AMD/Cemu addrlib) exactly).
//
// The RGBA decoder supports linear modes 0/1, micro-tiled modes 2/3, and
// all 2D/2B macro modes 4-11. The latter include aspect ratios 1/2/4,
// bank-swapped addressing, and GX2 pipe/bank swizzle bits. The math is
// differential-tested against GTX-Extractor's independent addrlib port.
// The current image API remains intentionally level-0, single-sample and
// 2D; 3D tile modes 12-15, depth ordering, slices and mip output require a
// wider API rather than silently flattening them incorrectly.
//
// One real simplification found while porting: the reference's own
// deswizzle() is called with the FILE's own stored `pitch` field, not a
// freshly recomputed/aligned one -- and for every real BC-format sample
// checked, the stored pitch already equals what a full addrlib recompute
// would produce. So this port trusts the file's stored pitch directly
// rather than re-deriving alignment from scratch (which would have pulled
// in a much larger slice of addrlib's macro-tile-alignment maze for no
// verified benefit).

#include "lib-std.h"
#include "lib-gtx.h"
#include "lib-bntx.h"

static inline u32 grd32 ( const u8 *p )
    { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | (u32)p[3]; }

static inline void gwr32 ( u8 *p, u32 v )
    { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }

static inline uint div_round_up ( uint n, uint d ) { return d ? (n+d-1)/d : 0; }

#define GTX_MAX_OUTPUT (256u<<20)

//-----------------------------------------------------------------------------
///////////////		container parsing			///////////////
//-----------------------------------------------------------------------------

void ResetGTX ( gtx_t *gtx )
{
    if (!gtx) return;
    FREE(gtx->textures);
    memset(gtx,0,sizeof(*gtx));
}

// Block types: v6.0 uses 0x0A for the texture header (surfBlkType), v6.1/
// v7.x use 0x0B -- confirmed against the reference tool's own version gate.
enumError ScanGTX ( gtx_t *gtx, const u8 *data, uint size )
{
    if ( !gtx || !data || size < 32 || memcmp(data,"Gfx2",4) )
	return EINVAL;

    const u32 hdr_size  = grd32(data+4);
    const u32 vmajor    = grd32(data+8);
    const u32 vminor    = grd32(data+12);
    const u32 gpu_ver   = grd32(data+16);
    if ( hdr_size != 32 || gpu_ver != 2 )
	return EINVAL;

    uint surf_type;
    if ( vmajor == 6 && vminor == 0 )
	surf_type = 0x0A;
    else if ( vmajor == 6 || vmajor == 7 )
	surf_type = 0x0B;
    else
	return EINVAL;

    memset(gtx,0,sizeof(*gtx));
    gtx->data = data;
    gtx->size = size;

    gtx_texture_t *list = 0;
    uint n = 0, cap = 0;

    uint pos = hdr_size;
    while ( pos+32 <= size )
    {
	if ( memcmp(data+pos,"BLK{",4) )
	    break;
	const u32 blk_hdrsize = grd32(data+pos+4);
	const u32 blk_type    = grd32(data+pos+16);
	const u32 blk_dsize   = grd32(data+pos+20);
	if ( blk_hdrsize != 32 || (u64)pos+blk_hdrsize+blk_dsize > size )
	    break;
	const uint data_pos = pos+blk_hdrsize;

	if ( blk_type == surf_type )
	{
	    // GX2Texture: GX2Surface (16 u32) + 13 mip-offset u32 (unused,
	    // level 0 only) + viewFirstMip/NumMips/FirstSlice/NumSlices (4
	    // u32) + compSel (4 bytes) + texRegs (5 u32) = 156 bytes total;
	    // only the leading GX2Surface (64 bytes) is needed here.
	    if ( data_pos+64 > size )
		break;
	    const u32 dim      = grd32(data+data_pos+0);
	    const u32 width    = grd32(data+data_pos+4);
	    const u32 height   = grd32(data+data_pos+8);
	    const u32 depth    = grd32(data+data_pos+12);
	    const u32 num_mips = grd32(data+data_pos+16);
	    const u32 format   = grd32(data+data_pos+20);
	    const u32 aa       = grd32(data+data_pos+24);
	    const u32 use      = grd32(data+data_pos+28);
	    const u32 tile_mode= grd32(data+data_pos+48);
	    const u32 swizzle  = grd32(data+data_pos+52);
	    const u32 pitch    = grd32(data+data_pos+60);

	    if ( n >= cap )
	    {
		cap = cap ? cap*2 : 4;
		gtx_texture_t *grown = REALLOC(list,cap*sizeof(*list));
		if (!grown) { FREE(list); return ERR_CANT_CREATE; }
		list = grown;
	    }
	    memset(list+n,0,sizeof(*list));
	    list[n].dim = dim;
	    list[n].width = width;
	    list[n].height = height;
	    list[n].depth = depth ? depth : 1;
	    list[n].num_mips = num_mips;
	    list[n].format = format;
	    list[n].aa = aa;
	    list[n].use = use;
	    list[n].tile_mode = tile_mode;
	    list[n].swizzle = swizzle;
	    list[n].pitch = pitch;
	    n++;
	}
	else if ( blk_type == surf_type+1 && n > 0 )
	{
	    // image data block, immediately follows its texture header block
	    list[n-1].data = data+data_pos;
	    list[n-1].data_size = blk_dsize;
	}
	// mip data (surf_type+2) and everything else (shaders, padding,
	// end-of-file) are skipped -- level 0 only, per this file's scope.

	pos = data_pos+blk_dsize;
    }

    if (!n) { FREE(list); return EINVAL; }
    gtx->textures = list;
    gtx->n_textures = n;
    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		GX2 tiled-surface addressing		///////////////
//-----------------------------------------------------------------------------

static uint gx2_surface_thickness ( uint tile_mode )
{
    if ( tile_mode==3 || tile_mode==7 || tile_mode==11 || tile_mode==13 || tile_mode==15 )
	return 4;
    if ( tile_mode==16 || tile_mode==17 )
	return 8;
    return 1;
}

static bool gx2_is_thick_macro ( uint tile_mode )
{
    return tile_mode==7 || tile_mode==11 || tile_mode==13 || tile_mode==15;
}

static bool gx2_is_bank_swapped ( uint tile_mode )
{
    return tile_mode==8 || tile_mode==9 || tile_mode==10 || tile_mode==11
	|| tile_mode==14 || tile_mode==15;
}

// Macro-tile aspect ratio: 2D/2B_TILED_THIN2 (5/9) halve the tile width and
// double the height (ratio 2); THIN4 (6/10) do it by a factor of 4; every
// other macro tile mode (4/7/8/11, the THIN1/THICK pair) is aspect ratio 1.
// Port of aboood40091/BFRES-Tool's addrlib.py computeMacroTileAspectRatio()
// (itself a from-scratch Python reimplementation of the real AMD/GX2
// addrlib algorithm, GPL-3.0, github.com/aboood40091/BFRES-Tool,
// addrlib/addrlib.py) -- see the file header for the existing prior art
// this file already ports the same way.
static uint gx2_macro_tile_aspect ( uint tile_mode )
{
    if ( tile_mode==5 || tile_mode==9 )  return 2;
    if ( tile_mode==6 || tile_mode==10 ) return 4;
    return 1;
}

// Bank-swap order lookup used below, straight from the same reference.
static const uint gx2_bank_swap_order[8] = { 0,1,3,2,6,7,5,4 };

// Width (in macro tiles) at which the bank-swap XOR pattern below repeats,
// for the bank-swapped tile modes (8/9/10/11/14/15) only -- 0 for anything
// else (matches the reference's own "not bank swapped -> 0" early return).
// numSamples is hardcoded to 1: this decoder has no MSAA/depth-surface
// support, matching the rest of this file's single-sample-2D-texture scope.
// Port of the same addrlib.py's computeSurfaceBankSwappedWidth().
static uint gx2_bank_swapped_width ( uint tile_mode, uint bpp, uint pitch )
{
    if ( !gx2_is_bank_swapped(tile_mode) )
	return 0;

    const uint num_samples = 1;
    uint bytes_per_sample = 8*bpp;
    uint slices_per_tile = 1;
    if ( bytes_per_sample )
    {
	const uint samples_per_tile = 2048/bytes_per_sample;
	slices_per_tile = samples_per_tile ? num_samples/samples_per_tile : 0;
	if ( !slices_per_tile ) slices_per_tile = 1;
    }

    uint eff_samples = num_samples;
    if ( gx2_is_thick_macro(tile_mode) )
	eff_samples = 4;

    const uint bytes_per_tile_slice = eff_samples*bytes_per_sample/slices_per_tile;
    const uint factor = gx2_macro_tile_aspect(tile_mode);
    const uint swap_tiles = bpp ? (128/bpp > 1 ? 128/bpp : 1) : 1;
    const uint swap_width = swap_tiles*32;
    const uint height_bytes = eff_samples*factor*bpp*2/slices_per_tile;
    const uint swap_max = height_bytes ? 0x4000/height_bytes : 0;
    const uint swap_min = bytes_per_tile_slice ? 256/bytes_per_tile_slice : 0;

    uint inner = swap_width > swap_min ? swap_width : swap_min;
    uint bank_swap_width = inner < swap_max ? inner : swap_max;
    while ( pitch && bank_swap_width >= 2*pitch )
	bank_swap_width >>= 1;
    return bank_swap_width;
}

// Bit-permutation table for the pixel's position within its 8x8 micro-tile,
// keyed by bpp (bits per element -- 8/16/32/64/128, matching the reference's
// own bpp-keyed branch table exactly).
static uint gx2_pixel_index_in_microtile ( uint x, uint y, uint bpp )
{
    uint b0,b1,b2,b3,b4,b5;
    switch (bpp)
    {
	case 8:
	    b0=x&1; b1=(x>>1)&1; b2=(x>>2)&1; b3=(y>>1)&1; b4=y&1; b5=(y>>2)&1;
	    break;
	case 0x10:
	    b0=x&1; b1=(x>>1)&1; b2=(x>>2)&1; b3=y&1; b4=(y>>1)&1; b5=(y>>2)&1;
	    break;
	case 0x20: case 0x60:
	    b0=x&1; b1=(x>>1)&1; b2=y&1; b3=(x>>2)&1; b4=(y>>1)&1; b5=(y>>2)&1;
	    break;
	case 0x40:
	    b0=x&1; b1=y&1; b2=(x>>1)&1; b3=(x>>2)&1; b4=(y>>1)&1; b5=(y>>2)&1;
	    break;
	case 0x80:
	    b0=y&1; b1=x&1; b2=(x>>1)&1; b3=(x>>2)&1; b4=(y>>1)&1; b5=(y>>2)&1;
	    break;
	default:
	    b0=x&1; b1=(x>>1)&1; b2=y&1; b3=(x>>2)&1; b4=(y>>1)&1; b5=(y>>2)&1;
	    break;
    }
    return 32*b5 | 16*b4 | 8*b3 | 4*b2 | b0 | 2*b1;
}

static inline uint gx2_pipe_from_coord ( uint x, uint y )
    { return ((y>>3) ^ (x>>3)) & 1; }
static inline uint gx2_bank_from_coord ( uint x, uint y )
    { return (((y>>5)^(x>>3))&1) | 2*(((y>>4)^(x>>4))&1); }

// Address (in bytes) of element (x,y) within a micro-tiled (tileMode 2/3)
// surface. bpp is bits per element.
static uint gx2_addr_micro_tiled
(
    uint x, uint y, uint bpp, uint pitch, uint height, uint tile_mode
)
{
    const uint thickness = tile_mode==3 ? 4 : 1;
    const uint micro_tile_bytes = (64*thickness*bpp+7)/8;
    const uint tiles_per_row = pitch >> 3;
    const uint tile_x = x >> 3, tile_y = y >> 3;
    const uint micro_off = micro_tile_bytes * (tile_x + tile_y*tiles_per_row);
    (void)height;
    const uint pixel_idx = gx2_pixel_index_in_microtile(x,y,bpp);
    const uint pixel_off = (bpp*pixel_idx) >> 3;
    return pixel_off + micro_off;
}

// Address (packed pipe/bank/offset word, matching the reference's own
// packed return value -- see gtx_detile()'s use of it) of element (x,y)
// within a macro-tiled surface, for the full non-3D macro tile family:
// tile modes 4/5/6/7 (2D_TILED THIN1/THIN2/THIN4/THICK) and 8/9/10/11
// (2B_TILED, same four, bank-swapped). pipe_swizzle (0/1) and bank_swizzle
// (0-3) come from the GX2Surface's `swizzle` field, bits 8 and 9-10
// respectively (see ScanGTX / the `swizzle_` split in the reference's own
// swizzleSurf()).
//
// Port of aboood40091/BFRES-Tool's addrlib.py
// computeSurfaceAddrFromCoordMacroTiled() (GPL-3.0,
// github.com/aboood40091/BFRES-Tool, addrlib/addrlib.py -- itself a
// from-scratch Python reimplementation of the real AMD/GX2 addrlib
// algorithm; same lineage this file already credits for the tile-mode-4
// case). Scoped to numSamples=1, sampleSlice=0 (single-sample, single-slice
// 2D textures only, matching this decoder's overall scope) -- the
// reference's own sample-split branch for microTileBytes>2048 divides by
// zero for every input in that range (2048 // 0), so it's dead code in the
// reference too; the caller (gtx_detile) declines rather than hit it.
static uint gx2_addr_macro_tiled
(
    uint x, uint y, uint bpp, uint pitch, uint tile_mode,
    uint pipe_swizzle, uint bank_swizzle
)
{
    const uint thickness = gx2_surface_thickness(tile_mode);
    const uint num_samples = 1;

    const uint pixel_idx = gx2_pixel_index_in_microtile(x,y,bpp);
    const uint elem_off = (bpp*pixel_idx+7)/8;

    uint pipe = gx2_pipe_from_coord(x,y);
    uint bank = gx2_bank_from_coord(x,y);
    const uint swizzle_ = pipe_swizzle + 2*bank_swizzle;
    uint bank_pipe = ((pipe + 2*bank) ^ swizzle_) % 8; // sampleSlice=0 drops the 6*sampleSlice term
    pipe = bank_pipe % 2;
    bank = bank_pipe / 2;
    // sliceOffset = sliceBytes * (sampleSlice // thickness) is always 0
    // here since sampleSlice is always 0 (single-slice 2D textures).

    uint macro_tile_pitch = 32, macro_tile_height = 16;
    if ( tile_mode==5 || tile_mode==9 )       { macro_tile_pitch >>= 1; macro_tile_height <<= 1; }
    else if ( tile_mode==6 || tile_mode==10 ) { macro_tile_pitch >>= 2; macro_tile_height <<= 2; }

    const uint macro_tiles_per_row = pitch / macro_tile_pitch;
    const uint macro_tile_bytes = (num_samples*thickness*bpp*macro_tile_height*macro_tile_pitch+7)/8;
    const uint mx = x / macro_tile_pitch, my = y / macro_tile_height;
    uint macro_off = (mx + macro_tiles_per_row*my) * macro_tile_bytes;

    if ( gx2_is_bank_swapped(tile_mode) )
    {
	const uint bsw = gx2_bank_swapped_width(tile_mode,bpp,pitch);
	if (bsw)
	{
	    const uint swap_index = (macro_tile_pitch*mx/bsw) & 3;
	    bank ^= gx2_bank_swap_order[swap_index];
	}
    }

    const uint total = elem_off + (macro_off >> 3);
    return bank<<9 | pipe<<8 | (total & 255) | ((total & ~255u)<<3);
}

u64 GetGX2SurfaceOffset
(
    uint x, uint y, uint bpp, uint width, uint height,
    uint tile_mode, uint pitch, uint swizzle
)
{
    if ( !bpp || bpp%8 || !width || !height || !pitch || x>=width || y>=height )
	return ~(u64)0;

    const uint bytes = bpp/8;
    if ( tile_mode==0 || tile_mode==1 )
	return (u64)(y*pitch+x)*bytes;
    if ( tile_mode==2 || tile_mode==3 )
	return gx2_addr_micro_tiled(x,y,bpp,pitch,height,tile_mode);
    if ( tile_mode>=4 && tile_mode<=11 )
    {
	const uint micro_tile_bytes = (bpp*gx2_surface_thickness(tile_mode)*64+7)/8;
	if ( micro_tile_bytes > 2048 )
	    return ~(u64)0;
	return gx2_addr_macro_tiled(x,y,bpp,pitch,tile_mode,
		(swizzle>>8)&1,(swizzle>>9)&3);
    }
    return ~(u64)0;
}

// bpp (bits per element -- block-bits for BCn formats) per GX2SurfaceFormat.
// Only the formats this fork's pixel decoders below actually support.
static bool gx2_format_bpp ( uint format, uint *bpp, bool *is_bc, uint *bc_variant )
{
    *is_bc = false;
    switch ( format & 0x3F )
    {
	case 0x01: *bpp=8;   return true; // R8_UNORM
	case 0x07: *bpp=16;  return true; // R8_G8_UNORM
	case 0x08: *bpp=16;  return true; // R5_G6_B5_UNORM
	case 0x0a: *bpp=16;  return true; // R5_G5_B5_A1_UNORM
	case 0x0b: *bpp=16;  return true; // R4_G4_B4_A4_UNORM
	case 0x1a: *bpp=32;  return true; // R8_G8_B8_A8_UNORM(/SRGB)
	case 0x31: *bpp=64;  *is_bc=true; *bc_variant=1; return true; // BC1
	case 0x32: *bpp=128; *is_bc=true; *bc_variant=2; return true; // BC2
	case 0x33: *bpp=128; *is_bc=true; *bc_variant=3; return true; // BC3
	case 0x34: *bpp=64;  *is_bc=true; *bc_variant=4; return true; // BC4
	case 0x35: *bpp=128; *is_bc=true; *bc_variant=5; return true; // BC5
	default: return false;
    }
}

// Deswizzles one level-0 surface into a tightly packed, row-major buffer of
// DIV_ROUND_UP(width,blk)*DIV_ROUND_UP(height,blk)*(bpp/8) bytes (blk=4 for
// BCn formats, 1 otherwise) -- i.e. still block-compressed for BC formats,
// exactly like BntxDeswizzle's contract.
static enumError gtx_detile
(
    u8 **dest, const u8 *src, uint src_size,
    uint width, uint height, uint bpp, uint tile_mode, uint pitch, uint swizzle
)
{
    const bool is_bc = bpp==64 || bpp==128; // only BC formats reach 64/128bpp here
    const uint bw = is_bc ? div_round_up(width,4) : width;
    const uint bh = is_bc ? div_round_up(height,4) : height;
    const uint bytes = bpp/8;
    const u64 out_size = (u64)bw*bh*bytes;
    if ( !bw || !bh || out_size > GTX_MAX_OUTPUT )
	return EINVAL;

    u8 *out = MALLOC(out_size);
    if (!out) return ERR_CANT_CREATE;
    memset(out,0,out_size);

    for ( uint y = 0; y < bh; y++ )
    for ( uint x = 0; x < bw; x++ )
    {
	const u64 addr = GetGX2SurfaceOffset(x,y,bpp,bw,bh,tile_mode,pitch,swizzle);
	if ( addr == ~(u64)0 )
	    { FREE(out); return EINVAL; }

	if ( (u64)addr+bytes > src_size )
	    continue; // leave zero-filled, matches reference's own bounds behaviour
	memcpy(out+(y*bw+x)*bytes, src+(size_t)addr, bytes);
    }

    *dest = out;
    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		RGBA8 decode				///////////////
//-----------------------------------------------------------------------------

enumError DecodeGX2Surface_RGBA
(
    u8 **dest, uint *width, uint *height,
    uint dim, uint w, uint h, uint format, uint tile_mode, uint pitch,
    uint swizzle, const u8 *data, uint data_size
)
{
    if ( !dest || !width || !height || !data || !w || !h || dim > 1 )
	return EINVAL; // level 0, 2D textures only

    uint bpp, bc_variant = 0;
    bool is_bc;
    if ( !gx2_format_bpp(format,&bpp,&is_bc,&bc_variant) )
	return EINVAL;

    u8 *tiled = 0;
    enumError err = gtx_detile(&tiled,data,data_size,w,h,bpp,tile_mode,pitch,swizzle);
    if (err) return err;

    const u64 out_size = (u64)w*h*4;
    if ( out_size > GTX_MAX_OUTPUT ) { FREE(tiled); return ERR_INVALID_DATA; }
    u8 *rgba = MALLOC(out_size);
    if (!rgba) { FREE(tiled); return ERR_CANT_CREATE; }

    if (is_bc)
    {
	const uint bw = div_round_up(w,4), bh = div_round_up(h,4);
	const uint block_bytes = bpp/8;
	for ( uint by = 0; by < bh; by++ )
	for ( uint bx = 0; bx < bw; bx++ )
	{
	    u8 px[64];
	    const u8 *blk = tiled + (by*bw+bx)*block_bytes;
	    switch (bc_variant)
	    {
		case 1: decode_bc1_block(blk,px,true); break;
		case 2: decode_bc2_block(blk,px); break;
		case 3: decode_bc3_block(blk,px); break;
		case 4: decode_bc4_block(blk,px); break;
		case 5: decode_bc5_block(blk,px); break;
	    }
	    for ( uint py = 0; py < 4; py++ )
	    {
		const uint dy = by*4+py;
		if ( dy >= h ) break;
		for ( uint pxi = 0; pxi < 4; pxi++ )
		{
		    const uint dx = bx*4+pxi;
		    if ( dx >= w ) break;
		    memcpy(rgba+(dy*w+dx)*4, px+(py*4+pxi)*4, 4);
		}
	    }
	}
    }
    else
    {
	for ( uint y = 0; y < h; y++ )
	for ( uint x = 0; x < w; x++ )
	{
	    const u8 *s = tiled + (y*w+x)*(bpp/8);
	    u8 *d = rgba + (y*w+x)*4;
	    switch ( format & 0x3F )
	    {
		case 0x01: d[0]=d[1]=d[2]=s[0]; d[3]=255; break; // R8
		case 0x07: d[0]=s[0]; d[1]=s[1]; d[2]=0; d[3]=255; break; // R8_G8_UNORM: 2-channel RG, no alpha channel
		case 0x08: // R5G6B5
		{
		    const u16 v = (u16)s[0]<<8|s[1];
		    d[0]=(u8)(((v>>11)&0x1F)*255/31);
		    d[1]=(u8)(((v>>5)&0x3F)*255/63);
		    d[2]=(u8)((v&0x1F)*255/31);
		    d[3]=255;
		    break;
		}
		case 0x0a: // R5G5B5A1
		{
		    const u16 v = (u16)s[0]<<8|s[1];
		    d[0]=(u8)(((v>>11)&0x1F)*255/31);
		    d[1]=(u8)(((v>>6)&0x1F)*255/31);
		    d[2]=(u8)(((v>>1)&0x1F)*255/31);
		    d[3]=(v&1)?255:0;
		    break;
		}
		case 0x0b: // R4G4B4A4
		{
		    const u16 v = (u16)s[0]<<8|s[1];
		    d[0]=(u8)(((v>>12)&0xF)*17);
		    d[1]=(u8)(((v>>8)&0xF)*17);
		    d[2]=(u8)(((v>>4)&0xF)*17);
		    d[3]=(u8)((v&0xF)*17);
		    break;
		}
		case 0x1a: // R8G8B8A8
		    d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
		    break;
	    }
	}
    }

    FREE(tiled);
    *dest = rgba;
    *width = w;
    *height = h;
    return ERR_OK;
}

enumError DecodeGTX_RGBA
(
    u8 **dest, uint *width, uint *height,
    const gtx_t *gtx, uint index
)
{
    if ( !gtx || index >= gtx->n_textures )
	return EINVAL;
    const gtx_texture_t *t = gtx->textures+index;
    if ( !t->data )
	return EINVAL;
    return DecodeGX2Surface_RGBA(dest,width,height,
	t->dim,t->width,t->height,t->format,t->tile_mode,t->pitch,
	t->swizzle,t->data,t->data_size);
}

//-----------------------------------------------------------------------------
///////////////			format encoding			///////////////
//-----------------------------------------------------------------------------

// GX2Texture block payload size: GX2Surface (16 u32) + 13 mip-offset u32
// (unused, level 0 only) + viewFirstMip/NumMips/FirstSlice/NumSlices (4
// u32) + compSel (4 bytes) + texRegs (5 u32) -- matches ScanGTX's own
// comment on the layout it reads.
#define GTX_TEX_BLOCK_SIZE (64+13*4+4*4+4+5*4)
#define GTX_BLOCK_HDR_SIZE 32
#define GTX_FILE_HDR_SIZE  32

enumError EncodeGTX_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height
)
{
    if ( !dest || !dest_size || !rgba || !width || !height )
	return EINVAL;

    const uint bpp_bytes = 4;
    const u64 surf_size = (u64)width*height*bpp_bytes;
    if ( surf_size > GTX_MAX_OUTPUT )
	return EFBIG;

    u8 *swizzled = MALLOC((size_t)surf_size);
    if (!swizzled) return ERR_CANT_CREATE;
    // tile mode 1 (LINEAR_ALIGNED): plain row-major, pitch == width, the
    // same addressing gtx_detile() already uses for tile_mode 0/1 -- so
    // this is an exact inverse of the decode path, not an approximation.
    for ( uint y = 0; y < height; y++ )
	memcpy(swizzled+(size_t)y*width*bpp_bytes, rgba+(size_t)y*width*bpp_bytes, (size_t)width*bpp_bytes);

    const u64 total_size = (u64)GTX_FILE_HDR_SIZE
	+ GTX_BLOCK_HDR_SIZE + GTX_TEX_BLOCK_SIZE
	+ GTX_BLOCK_HDR_SIZE + surf_size;
    if ( total_size > GTX_MAX_OUTPUT )
    {
	FREE(swizzled);
	return EFBIG;
    }

    u8 *buf = CALLOC(1,(size_t)total_size);
    if (!buf)
    {
	FREE(swizzled);
	return ERR_CANT_CREATE;
    }

    // File header @0
    memcpy(buf,"Gfx2",4);
    gwr32(buf+4,GTX_FILE_HDR_SIZE);
    gwr32(buf+8,7);   // majorVersion
    gwr32(buf+12,3);  // minorVersion
    gwr32(buf+16,2);  // gpuVersion (GX2, matches ScanGTX's check)

    // GX2Texture header block
    u8 *blk1 = buf+GTX_FILE_HDR_SIZE;
    memcpy(blk1,"BLK{",4);
    gwr32(blk1+4,GTX_BLOCK_HDR_SIZE);
    gwr32(blk1+16,0x0B);              // surfBlkType (v6.1/v7.x texture header)
    gwr32(blk1+20,GTX_TEX_BLOCK_SIZE);

    u8 *surf = blk1+GTX_BLOCK_HDR_SIZE;
    gwr32(surf+0,1);                  // dim: GX2_SURFACE_DIM_TEXTURE_2D
    gwr32(surf+4,width);
    gwr32(surf+8,height);
    gwr32(surf+12,1);                 // depth
    gwr32(surf+16,1);                 // numMips
    gwr32(surf+20,0x1a);              // format: R8_G8_B8_A8_UNORM
    gwr32(surf+24,0);                 // aa
    gwr32(surf+28,1);                 // use: GX2_SURFACE_USE_TEXTURE
    gwr32(surf+32,(u32)surf_size);    // imageSize
    gwr32(surf+48,1);                 // tileMode: LINEAR_ALIGNED
    gwr32(surf+60,width);             // pitch (elements, matches decode's addressing)
    // compSel (identity R,G,B,A) right after mip offsets + view fields
    u8 *comp_sel = surf+64+13*4+4*4;
    comp_sel[0]=0; comp_sel[1]=1; comp_sel[2]=2; comp_sel[3]=3;

    // image data block
    u8 *blk2 = blk1+GTX_BLOCK_HDR_SIZE+GTX_TEX_BLOCK_SIZE;
    memcpy(blk2,"BLK{",4);
    gwr32(blk2+4,GTX_BLOCK_HDR_SIZE);
    gwr32(blk2+16,0x0C);              // surfBlkType+1: image data
    gwr32(blk2+20,(u32)surf_size);
    memcpy(blk2+GTX_BLOCK_HDR_SIZE,swizzled,(size_t)surf_size);

    FREE(swizzled);
    *dest = buf;
    *dest_size = (uint)total_size;
    return ERR_OK;
}
