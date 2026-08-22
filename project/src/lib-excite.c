// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-excite.h"
#include "lib-model-dae.h"
#include <math.h>

static inline u16 xrd_le16 ( const u8 *p ) { return (u16)p[1]<<8 | p[0]; }
static inline u16 xrd_be16 ( const u8 *p ) { return (u16)p[0]<<8 | p[1]; }

//-----------------------------------------------------------------------------
///////////////		.tex / .art GX texture recovery		///////////////
//-----------------------------------------------------------------------------
//
// Ported from ExciteExtract's classify_tex.py + gxtex.py (Python ground
// truth; see the Excite RE notes for the discovery process). Both games
// store raw GX (GameCube/Wii) texture data but never record the pixel
// FORMAT anywhere in the file -- the retail engine looked it up from the
// material instead. This file recovers it by brute-force decoding every
// plausible GX format and scoring which one produces a coherent image:
//
//   .tex: mip level 1 (when present) must be a correct 2x box-downsample of
//         level 0 for the true format, and looks like noise for every wrong
//         one ("mip_score").
//   .art/.img: no mip chain exists, so instead the tiled layout of GX pixel
//         data is exploited -- at the true width, pixel deltas measured
//         *across* 4/8-pixel tile boundaries are no larger than deltas
//         measured *inside* a tile; at a wrong width the tiles land in the
//         wrong place and boundary deltas spike ("seam_score").
//
// Neither heuristic is exact, so this is inherently best-effort: on some
// files it can pick a plausible-but-wrong format. That matches the ~2% of
// files ExciteExtract itself could not resolve.

typedef struct gxfmt_t
{
    u8   id;
    u8   bpp;      // bits per pixel
    u8   bw, bh;   // tile block size
}
gxfmt_t;

// Excludes paletted formats (C4/C8/C14X2): these files carry no TLUT, so a
// palette can never be recovered and the candidate is never worth trying.
#define GX_I4      0x0
#define GX_I8      0x1
#define GX_IA4     0x2
#define GX_IA8     0x3
#define GX_RGB565  0x4
#define GX_RGB5A3  0x5
#define GX_RGBA32  0x6
#define GX_CMPR    0xE

static const gxfmt_t gx_formats[] =
{
    { GX_I4,     4, 8, 8 },
    { GX_I8,     8, 8, 4 },
    { GX_IA4,    8, 8, 4 },
    { GX_IA8,   16, 4, 4 },
    { GX_RGB565,16, 4, 4 },
    { GX_RGB5A3,16, 4, 4 },
    { GX_RGBA32,32, 4, 4 },
    { GX_CMPR,   4, 8, 8 },
};
#define N_GX_FORMATS ( sizeof(gx_formats)/sizeof(gx_formats[0]) )

// Candidate try order copied from classify_tex.py's CAND tuple.
static const u8 gx_candidates[] = { GX_CMPR, GX_I8, GX_IA4, GX_IA8, GX_RGB5A3, GX_RGB565, GX_I4, GX_RGBA32 };
#define N_GX_CANDIDATES ( sizeof(gx_candidates)/sizeof(gx_candidates[0]) )

static const uint gx_dims[] = { 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
#define N_GX_DIMS ( sizeof(gx_dims)/sizeof(gx_dims[0]) )

static const gxfmt_t * find_gx_format ( u8 id )
{
    for ( uint i = 0; i < N_GX_FORMATS; i++ )
	if ( gx_formats[i].id == id )
	    return gx_formats+i;
    return 0;
}

static uint gx_level_size ( u8 fmt, uint w, uint h )
{
    const gxfmt_t *f = find_gx_format(fmt);
    if (!f) return 0;
    const uint tw = ( w + f->bw - 1 ) / f->bw * f->bw;
    const uint th = ( h + f->bh - 1 ) / f->bh * f->bh;
    return tw * th * f->bpp / 8;
}

static u64 gx_chain_size ( u8 fmt, uint w, uint h, uint levels )
{
    u64 tot = 0;
    for ( uint i = 0; i < levels; i++ )
    {
	const uint lw = w>>i ? w>>i : 1;
	const uint lh = h>>i ? h>>i : 1;
	tot += gx_level_size(fmt,lw,lh);
    }
    return tot;
}

static inline void rgb565_to_rgba ( u16 v, u8 *out )
{
    const uint r = (v>>11)&0x1f, g = (v>>5)&0x3f, b = v&0x1f;
    out[0] = r*255/31; out[1] = g*255/63; out[2] = b*255/31; out[3] = 255;
}

static inline void rgb5a3_to_rgba ( u16 v, u8 *out )
{
    if ( v & 0x8000 )
    {
	const uint r = (v>>10)&0x1f, g = (v>>5)&0x1f, b = v&0x1f;
	out[0] = r*255/31; out[1] = g*255/31; out[2] = b*255/31; out[3] = 255;
    }
    else
    {
	const uint a = (v>>12)&0x7, r = (v>>8)&0xf, g = (v>>4)&0xf, b = v&0xf;
	out[0] = r*17; out[1] = g*17; out[2] = b*17; out[3] = a*255/7;
    }
}

// Decode 'fmt' GX pixel data (big-endian, as stored on disc) to a freshly
// MALLOC'd width*height*4 RGBA8 buffer. Truncated source data just leaves
// the remainder of the buffer at its zero-initialised default, matching
// gxtex.py's early-return-on-underrun behaviour.
static u8 * gx_decode ( u8 fmt, uint w, uint h, const u8 *data, uint size )
{
    const gxfmt_t *gf = find_gx_format(fmt);
    if (!gf) return 0;
    u8 *out = CALLOC(1,(size_t)w*h*4);
    if (!out) return 0;

    #define PUT(x,y,r,g,b,a) do { \
	if ( (uint)(x) < w && (uint)(y) < h ) { \
	    u8 *o = out + ((size_t)(y)*w+(x))*4; \
	    o[0]=(r); o[1]=(g); o[2]=(b); o[3]=(a); \
	} } while(0)

    uint p = 0;
    for ( uint by = 0; by < h; by += gf->bh )
    for ( uint bx = 0; bx < w; bx += gf->bw )
    {
	switch (fmt)
	{
	  case GX_I4:
	    for ( uint y = 0; y < 8; y++ )
	    for ( uint x = 0; x < 8; x += 2 )
	    {
		if ( p >= size ) goto done;
		const u8 b = data[p++];
		const uint hi = b>>4, lo = b&0xf;
		PUT(bx+x,  by+y, hi*17,hi*17,hi*17,255);
		PUT(bx+x+1,by+y, lo*17,lo*17,lo*17,255);
	    }
	    break;

	  case GX_I8:
	    for ( uint y = 0; y < 4; y++ )
	    for ( uint x = 0; x < 8; x++ )
	    {
		if ( p >= size ) goto done;
		const u8 v = data[p++];
		PUT(bx+x,by+y,v,v,v,255);
	    }
	    break;

	  case GX_IA4:
	    for ( uint y = 0; y < 4; y++ )
	    for ( uint x = 0; x < 8; x++ )
	    {
		if ( p >= size ) goto done;
		const u8 b = data[p++];
		const uint i = (b&0xf)*17, a = (b>>4)*17;
		PUT(bx+x,by+y,i,i,i,a);
	    }
	    break;

	  case GX_IA8:
	    for ( uint y = 0; y < 4; y++ )
	    for ( uint x = 0; x < 4; x++ )
	    {
		if ( p+1 >= size ) goto done;
		const u16 v = xrd_be16(data+p); p += 2;
		const uint a = v>>8, i = v&0xff;
		PUT(bx+x,by+y,i,i,i,a);
	    }
	    break;

	  case GX_RGB565:
	    for ( uint y = 0; y < 4; y++ )
	    for ( uint x = 0; x < 4; x++ )
	    {
		if ( p+1 >= size ) goto done;
		u8 px[4]; rgb565_to_rgba(xrd_be16(data+p),px); p += 2;
		PUT(bx+x,by+y,px[0],px[1],px[2],px[3]);
	    }
	    break;

	  case GX_RGB5A3:
	    for ( uint y = 0; y < 4; y++ )
	    for ( uint x = 0; x < 4; x++ )
	    {
		if ( p+1 >= size ) goto done;
		u8 px[4]; rgb5a3_to_rgba(xrd_be16(data+p),px); p += 2;
		PUT(bx+x,by+y,px[0],px[1],px[2],px[3]);
	    }
	    break;

	  case GX_RGBA32:
	    {
		u8 a_[16], r_[16];
		for ( uint y = 0; y < 4; y++ )
		for ( uint x = 0; x < 4; x++ )
		{
		    if ( p+1 >= size ) goto done;
		    a_[y*4+x] = data[p]; r_[y*4+x] = data[p+1]; p += 2;
		}
		for ( uint y = 0; y < 4; y++ )
		for ( uint x = 0; x < 4; x++ )
		{
		    if ( p+1 >= size ) goto done;
		    const u8 g = data[p], b = data[p+1]; p += 2;
		    PUT(bx+x,by+y, r_[y*4+x], g, b, a_[y*4+x]);
		}
	    }
	    break;

	  case GX_CMPR:
	    for ( uint sy = 0; sy < 8; sy += 4 )
	    for ( uint sx = 0; sx < 8; sx += 4 )
	    {
		if ( p+7 >= size ) goto done;
		const u16 c0 = xrd_be16(data+p), c1 = xrd_be16(data+p+2);
		const u32 bits = (u32)data[p+4]<<24 | (u32)data[p+5]<<16 | (u32)data[p+6]<<8 | data[p+7];
		p += 8;
		u8 col[4][4];
		rgb565_to_rgba(c0,col[0]); rgb565_to_rgba(c1,col[1]);
		if ( c0 > c1 )
		{
		    for ( int k = 0; k < 3; k++ )
		    {
			col[2][k] = (u8)((col[0][k]*2+col[1][k])/3);
			col[3][k] = (u8)((col[0][k]+col[1][k]*2)/3);
		    }
		    col[2][3] = col[3][3] = 255;
		}
		else
		{
		    for ( int k = 0; k < 3; k++ )
			col[2][k] = (u8)((col[0][k]+col[1][k])/2);
		    col[2][3] = 255;
		    col[3][0] = col[3][1] = col[3][2] = col[3][3] = 0;
		}
		for ( uint y = 0; y < 4; y++ )
		for ( uint x = 0; x < 4; x++ )
		{
		    const uint idx = (bits >> (30 - (y*4+x)*2)) & 3;
		    PUT(bx+sx+x, by+sy+y, col[idx][0],col[idx][1],col[idx][2],col[idx][3]);
		}
	    }
	    break;
	}
    }
  done:
    #undef PUT
    return out;
}

// Distinct-value sample used to reject degenerate all-flat decodes (a wrong
// format can accidentally decode to a uniform colour, which trivially
// "passes" a naive smoothness test).
static uint count_distinct_sample ( const u8 *rgba, uint n_px )
{
    uint seen = 0;
    const uint step = n_px/4096 > 1 ? n_px/4096 : 1;
    u32 first = 0; bool have_first = false;
    for ( uint i = 0; i < n_px && seen < 3; i += step )
    {
	const u32 v = *(const u32*)(rgba + (size_t)i*4);
	if ( !have_first ) { first = v; have_first = true; seen = 1; }
	else if ( v != first ) seen++;
    }
    return seen;
}

// mip_score(): mean abs error between level0 box-downsampled 2x and the
// stored level1, sampled on a coarse grid (mirrors classify_tex.py exactly).
static double mip_score ( const u8 *data, uint size, u8 fmt, uint w, uint h )
{
    if ( w < 16 || h < 16 ) return -1;
    const uint L0 = gx_level_size(fmt,w,h);
    const uint L1 = gx_level_size(fmt,w/2,h/2);
    if ( !L0 || !L1 || (u64)L0+L1 > size ) return -1;

    u8 *a = gx_decode(fmt,w,h,data,L0);
    u8 *c = gx_decode(fmt,w/2,h/2,data+L0,L1);
    if ( !a || !c ) { FREE(a); FREE(c); return -1; }

    if ( count_distinct_sample(a,w*h) < 3 || count_distinct_sample(c,(w/2)*(h/2)) < 2 )
	{ FREE(a); FREE(c); return -1; }

    const uint hw = w/2, hh = h/2;
    u64 tot = 0; uint n = 0;
    const uint stepy = hh/32 > 1 ? hh/32 : 1;
    const uint stepx = hw/32 > 1 ? hw/32 : 1;
    for ( uint y = 0; y < hh; y += stepy )
    for ( uint x = 0; x < hw; x += stepx )
    {
	uint acc[4] = {0,0,0,0};
	for ( uint dy = 0; dy < 2; dy++ )
	for ( uint dx = 0; dx < 2; dx++ )
	{
	    const u8 *p = a + ((size_t)(y*2+dy)*w + (x*2+dx))*4;
	    for ( int k = 0; k < 4; k++ ) acc[k] += p[k];
	}
	const u8 *q = c + ((size_t)y*hw+x)*4;
	for ( int k = 0; k < 4; k++ )
	    tot += acc[k]/4 > q[k] ? acc[k]/4-q[k] : q[k]-acc[k]/4;
	n += 4;
    }
    FREE(a); FREE(c);
    return n ? (double)tot/n : -1;
}

// seam_score(): tile-boundary vs tile-interior pixel delta ratio, folded
// with the absolute interior delta so uniform noise doesn't masquerade as a
// perfect seam match (mirrors classify_tex.py exactly).
static double seam_score ( const u8 *px, uint w, uint h, uint bw, uint bh )
{
    u64 edge_sum = 0, inner_sum = 0; uint edge_n = 0, inner_n = 0;
    #define DELTA(x0,y0,x1,y1) ({ \
	const u8 *A = px + ((size_t)(y0)*w+(x0))*4, *B = px + ((size_t)(y1)*w+(x1))*4; \
	(uint)(abs(A[0]-B[0])+abs(A[1]-B[1])+abs(A[2]-B[2])+abs(A[3]-B[3])); })

    for ( uint y = 0; y < h; y++ )
    {
	for ( uint x = bw-1; x+1 < w; x += bw )
	    { edge_sum += DELTA(x,y,x+1,y); edge_n++; }
	for ( uint x = 0; x+1 < w; x += bw )
	    if ( (x+1) % bw )
		{ inner_sum += DELTA(x,y,x+1,y); inner_n++; }
    }
    for ( uint x = 0; x < w; x++ )
    {
	for ( uint y = bh-1; y+1 < h; y += bh )
	    { edge_sum += DELTA(x,y,x,y+1); edge_n++; }
	for ( uint y = 0; y+1 < h; y += bh )
	    if ( (y+1) % bh )
		{ inner_sum += DELTA(x,y,x,y+1); inner_n++; }
    }
    #undef DELTA
    if (!edge_n || !inner_n) return -1;
    const double e = (double)edge_sum/edge_n, i = (double)inner_sum/inner_n;
    return ( (e+1.0)/(i+1.0) ) * ( 1.0 + i/24.0 );
}

static double smoothness ( const u8 *px, uint w, uint h )
{
    u64 tot = 0; uint n = 0;
    const uint stepy = h/64 > 1 ? h/64 : 1;
    const uint stepx = w/64 > 1 ? w/64 : 1;
    for ( uint y = 0; y+1 < h; y += stepy )
    for ( uint x = 0; x+1 < w; x += stepx )
    {
	const u8 *a = px + ((size_t)y*w+x)*4;
	const u8 *r = px + ((size_t)y*w+x+1)*4;
	const u8 *d = px + ((size_t)(y+1)*w+x)*4;
	for ( int k = 0; k < 4; k++ )
	{
	    tot += abs(a[k]-r[k]);
	    tot += abs(a[k]-d[k]);
	}
	n += 8;
    }
    return n ? (double)tot/n : 1e18;
}

typedef struct tex_cand_t { u8 fmt; uint w, h; uint dl; } tex_cand_t;

// Locate the 24-byte footer at one of the four known offsets; returns true
// and fills *hoff/*w/*h on success. Mirrors classify_tex.py's find_footer().
static bool find_tex_footer ( const u8 *data, uint size, uint *hoff, uint *w, uint *h )
{
    static const int offs[] = { -64, -32, -128, -96 };
    for ( uint i = 0; i < 4; i++ )
    {
	if ( (int)size + offs[i] < 0 ) continue;
	const uint ho = size + offs[i];
	const u16 fw = xrd_le16(data+ho), fh = xrd_le16(data+ho+2);
	bool wok = false, hok = false;
	for ( uint d = 0; d < N_GX_DIMS; d++ )
	{
	    if ( gx_dims[d] == fw ) wok = true;
	    if ( gx_dims[d] == fh ) hok = true;
	}
	if ( wok && hok )
	{
	    // log2(width)-2, i.e. bit_length(width)-3
	    uint bl = 0; for ( u16 t = fw; t; t >>= 1 ) bl++;
	    if ( data[ho+4] == bl-3 )
	    {
		*hoff = ho; *w = fw; *h = fh;
		return true;
	    }
	}
    }
    return false;
}

// Shared classifier for both .tex (footer + mip-consistency) and .art/.img
// (no footer, seam-score only). 'has_footer' selects which of the three
// passes from classify_tex.py's classify() are attempted.
static enumError classify_excite_tex ( excite_tex_t *tex, const u8 *data, uint size, bool want_footer )
{
    memset(tex,0,sizeof(*tex));

    uint hoff=0, fw=0, fh=0;
    const bool have_footer = want_footer && find_tex_footer(data,size,&hoff,&fw,&fh);

    // Mirrors classify_tex.py's classify(): the footer is only a hint. When
    // it's absent, or its claimed height doesn't match the actual stored mip
    // chain (a real observed case), fall back to size-128 and let the
    // exact-mip-chain-size search below recover the true dimensions.
    const uint dl = have_footer ? hoff : size >= 128 ? size-128 : 0;
    if (!dl) return ERR_NOTHING_TO_DO;

    tex_cand_t cands[256]; uint n_cand = 0;
    #define ADD_CAND(f_,w_,h_,d_) do { \
	bool dup=false; \
	for (uint _i=0;_i<n_cand;_i++) \
	    if (cands[_i].fmt==(f_)&&cands[_i].w==(w_)&&cands[_i].h==(h_)&&cands[_i].dl==(d_)) {dup=true;break;} \
	if (!dup && n_cand < 256) cands[n_cand++] = (tex_cand_t){f_,w_,h_,d_}; \
    } while(0)

    if ( have_footer )
    {
	for ( uint c = 0; c < N_GX_CANDIDATES; c++ )
	{
	    const u8 fmt = gx_candidates[c];
	    if ( gx_level_size(fmt,fw,fh) <= hoff )
		ADD_CAND(fmt,fw,fh,hoff);
	}
    }
    // exact mip-chain-size search (covers footers whose claimed height
    // doesn't match the actual stored chain, per classify_tex.py's comment)
    for ( uint c = 0; c < N_GX_CANDIDATES; c++ )
    {
	const u8 fmt = gx_candidates[c];
	for ( uint wi = 0; wi < N_GX_DIMS; wi++ )
	for ( uint hi = 0; hi < N_GX_DIMS; hi++ )
	{
	    for ( uint lv = 1; lv <= 10; lv++ )
		if ( gx_chain_size(fmt,gx_dims[wi],gx_dims[hi],lv) == dl )
		{
		    ADD_CAND(fmt,gx_dims[wi],gx_dims[hi],dl);
		    break;
		}
	}
    }
    #undef ADD_CAND

    // pass 1: mip-chain consistency
    double best_score = 1e18; tex_cand_t best = {0,0,0,0}; bool have_best = false;
    for ( uint i = 0; i < n_cand; i++ )
    {
	const double m = mip_score(data,dl,cands[i].fmt,cands[i].w,cands[i].h);
	if ( m < 0 ) continue;
	if ( !have_best || m < best_score ) { best_score = m; best = cands[i]; have_best = true; }
    }
    if ( have_best && best_score < 12.0 )
    {
	u8 *rgba = gx_decode(best.fmt,best.w,best.h,data,best.dl);
	if (!rgba) return ERR_CANT_CREATE;
	tex->rgba = rgba; tex->width = best.w; tex->height = best.h;
	tex->gx_format = best.fmt; tex->score = (float)best_score;
	return ERR_OK;
    }

    // pass 2: tile-seam continuity (single-level images / no usable mips)
    have_best = false; best_score = 1e18;
    for ( uint i = 0; i < n_cand; i++ )
    {
	u8 *px = gx_decode(cands[i].fmt,cands[i].w,cands[i].h,data,cands[i].dl);
	if (!px) continue;
	if ( count_distinct_sample(px,cands[i].w*cands[i].h) < 3 ) { FREE(px); continue; }
	const gxfmt_t *gf = find_gx_format(cands[i].fmt);
	double s = seam_score(px,cands[i].w,cands[i].h,gf->bw,gf->bh);
	if ( s < 0 ) s = smoothness(px,cands[i].w,cands[i].h);
	const double ar = (double)( cands[i].w>cands[i].h ? cands[i].w : cands[i].h )
	                 / ( cands[i].w<cands[i].h ? cands[i].w : cands[i].h );
	if ( ar >= 8 ) s *= 1.0 + 0.35*(ar/8.0);
	FREE(px);
	if ( !have_best || s < best_score ) { best_score = s; best = cands[i]; have_best = true; }
    }
    if (have_best)
    {
	u8 *rgba = gx_decode(best.fmt,best.w,best.h,data,best.dl);
	if (!rgba) return ERR_CANT_CREATE;
	tex->rgba = rgba; tex->width = best.w; tex->height = best.h;
	tex->gx_format = best.fmt; tex->score = (float)best_score;
	return ERR_OK;
    }

    // pass 3: last resort, plain smoothness
    have_best = false; best_score = 1e18;
    for ( uint i = 0; i < n_cand; i++ )
    {
	u8 *px = gx_decode(cands[i].fmt,cands[i].w,cands[i].h,data,cands[i].dl);
	if (!px) continue;
	const double s = smoothness(px,cands[i].w,cands[i].h);
	FREE(px);
	if ( !have_best || s < best_score ) { best_score = s; best = cands[i]; have_best = true; }
    }
    if (!have_best) return ERR_NOTHING_TO_DO;

    u8 *rgba = gx_decode(best.fmt,best.w,best.h,data,best.dl);
    if (!rgba) return ERR_CANT_CREATE;
    tex->rgba = rgba; tex->width = best.w; tex->height = best.h;
    tex->gx_format = best.fmt; tex->score = (float)best_score;
    return ERR_OK;
}

enumError ScanTEX ( excite_tex_t *tex, const u8 *data, uint size )
{
    if ( !tex || !data || size < 64 ) return ERR_NOTHING_TO_DO;
    return classify_excite_tex(tex,data,size,true);
}

enumError ScanART ( excite_tex_t *tex, const u8 *data, uint size )
{
    if ( !tex || !data || size <= 128 ) return ERR_NOTHING_TO_DO;
    // Every real sample is exactly 2^k + 128 bytes with a zeroed footer.
    const uint dl = size - 128;
    bool pow2 = dl && !(dl & (dl-1));
    if (!pow2) return ERR_NOTHING_TO_DO;
    for ( uint i = size-128; i < size; i++ )
	if ( data[i] ) return ERR_NOTHING_TO_DO; // footer must be all-zero
    return classify_excite_tex(tex,data,size,false);
}

void ResetExciteTEX ( excite_tex_t *tex )
{
    if (!tex) return;
    FREE(tex->rgba);
    memset(tex,0,sizeof(*tex));
}

//-----------------------------------------------------------------------------
///////////////		.msh collision meshes				///////////////
//-----------------------------------------------------------------------------
//
// Headerless, magic-less: a flat array of little-endian float32 XYZ triples,
// file size an exact multiple of 12. The small samples (e.g. goalback.msh)
// are a plain sequential triangle soup -- every 3 consecutive positions form
// one triangle -- which is what's implemented here. Larger collision meshes
// (gpmesh.msh, rail2bp.msh) interleave small integer index-like fields
// between the position floats and are NOT correctly handled by this reader;
// they will decode as visual garbage (documented limitation, matches
// ExciteExtract's own README).

static inline float xrd_f32le ( const u8 *p )
{
    u32 v = (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0];
    float f; memcpy(&f,&v,4); return f;
}

enumError DecodeExciteMSH ( const u8 *data, uint size, ccp out_dae_path )
{
    if ( !data || !size || size % 12 ) return ERR_NOTHING_TO_DO;
    const uint n_verts = size/12;
    if ( n_verts < 3 ) return ERR_NOTHING_TO_DO;

    model_t model; memset(&model,0,sizeof(model));
    mesh_t mesh; memset(&mesh,0,sizeof(mesh));
    snprintf(mesh.name,sizeof(mesh.name),"collision");
    mesh.material_idx = -1;

    mesh.positions = MALLOC(n_verts*sizeof(vec3_t));
    mesh.num_positions = n_verts;
    for ( uint i = 0; i < n_verts; i++ )
    {
	mesh.positions[i].x = xrd_f32le(data+i*12);
	mesh.positions[i].y = xrd_f32le(data+i*12+4);
	mesh.positions[i].z = xrd_f32le(data+i*12+8);
    }

    const uint n_tris = n_verts/3;
    mesh.vertices = MALLOC(n_tris*3*sizeof(vertex_t));
    mesh.num_vertices = n_tris*3;
    for ( uint i = 0; i < n_tris*3; i++ )
    {
	vertex_t *v = mesh.vertices+i;
	v->position_idx = (int)i;
	v->normal_idx = v->texcoord_idx = v->matrix_idx = -1;
	v->color_idx[0] = v->color_idx[1] = -1;
	for ( int k = 0; k < 7; k++ ) v->extra_texcoord_idx[k] = -1;
    }

    model.meshes = &mesh;
    model.num_meshes = 1;

    const int rc = ExportModelToDAE(&model,out_dae_path);

    FREE(mesh.positions);
    FREE(mesh.vertices);
    return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

//-----------------------------------------------------------------------------
///////////////		.mod 3D models (Monster Games NDL3/NDL2)	///////////////
//-----------------------------------------------------------------------------
//
// Monster Games "3LDN"/"2LDN" ("NDL3"/"NDL2" read as a little-endian magic)
// model container -- Excite Truck's older "2LDN" revision and ExciteBots'
// "3LDN" revision share this same shape. Ported from this repo's companion
// ExciteExtract research tool (mod_parse.py/mod_export.py), which validated
// against the full retail corpus of both games: 135/135 Excite Truck and
// 193/203 ExciteBots real .mod files decode (the ExciteBots gap is exactly
// the intentionally-separate .msh collision-mesh files, not .mod failures).
// A prior pass through this same file only recognised 3/196 samples because
// it required the magic at file offset 0 and hardcoded a single 0x82-flags,
// one-tristrip-call shape; both restrictions were wrong, not narrow-but-
// correct -- most real files carry a small texture-filename table *before*
// the magic, and the geometry format is fully self-describing via the GX
// vertex-attribute-table (VAT) register writes embedded in the display list
// itself, so there is no need to hardcode flag values or index widths at all.
//
// Header (14 x u32le, magic at file offset 'm' -- may be > 0):
//   +0x00  "3LDN"/"2LDN" magic
//   +0x04  dl_end -- end offset (from 'm') of the display-list region. NDL2
//          stores this as a u16 with the high half filled with the 0xe3e3
//          filler byte pair, not a real u32; detect and mask that off.
//   +0x08  version constant (0x0e1e in both games; not relied upon)
//   +0x0c  format/flags constant (0x82 in most samples, but other real
//          values exist too -- irrelevant now that the VAT is read directly)
//   +0x18  f32 bounding-radius (not geometry data; not used)
//   +0x1c  vertex-position count
//   +0x20..0x30  more fields, still unmapped; not used
//
// The display list is genuine GX: 0x08 writes a CP register (used here only
// for VAT group A at address 0x70, which carries the position/texcoord
// element count, numeric format, and fixed-point shift -- see the GX
// hardware register reference), 0x10/0x61 write XF/BP registers (skipped),
// and 0x80..0xB8 are primitive draw calls (low 3 bits select vertex-format
// slot 0; the primitive kind is in bits 3-6: 0x80 quads, 0x88 quads2, 0x90
// triangles, 0x98 tristrip, 0xA0 fan, 0xA8 lines, 0xB0 linestrip, 0xB8
// points), each followed by a u16be vertex count and that many index tuples
// of a per-file, not-separately-stored width ("bytes per vertex", bpv):
// the first byte of each tuple is always the position index, the last byte
// (when bpv>1) is the texcoord index. bpv is recovered by brute force --
// try 1..8, keep whichever consumes the display list into a sequence of
// well-formed opcodes ending in the 0xe3 filler or end-of-buffer with no
// out-of-bounds reads; this self-validates because a wrong bpv drifts the
// byte stream into garbage opcodes almost immediately.
//
// The display list's start is found by its CP-register preamble signature
// (0x08 0x70 <4 bytes> 0x08 0x80 <4 bytes> 0x08 0x90) rather than trusting
// a version-specific size/offset field, since NDL2's 'dl_end' field shape
// differs from NDL3's.
//
// Geometry data begins at file offset 'm'+0x40: the position array (vertex-
// position-count entries, component count/format/fixed-point shift from
// the VAT), followed immediately by the texcoord array (component count/
// format/shift also from the VAT; its length isn't stored anywhere, so it
// is sized from the highest texcoord index actually used by the display
// list). Every real sample seen encodes only one UV channel -- multi-UV/
// vertex-colour/normal channels are not modelled here and any file needing
// them for its VAT-declared attributes beyond position+texcoord0 is not
// specially detected, since none of the validated corpus used them.
//-----------------------------------------------------------------------------

static inline u32 xrd_be32 ( const u8 *p )
{
    return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3];
}

static inline u32 xrd_le32 ( const u8 *p )
{
    return (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0];
}

static inline float xrd_f32be ( const u8 *p )
{
    const u32 v = xrd_be32(p);
    float f; memcpy(&f,&v,4); return f;
}

static inline s16 xrd_be16s ( const u8 *p ) { return (s16)xrd_be16(p); }

// One recognised GX draw call inside the display list: 'dl_off' is the byte
// offset (from the start of the display-list buffer) of its first index
// tuple, 'cnt' vertices wide, each tuple 'bpv' bytes (bpv lives on the
// caller, one value for the whole display list).
typedef struct mod_prim_t
{
    int  kind;    // MOD_QUADS..MOD_POINTS
    u16  cnt;
    uint dl_off;
}
mod_prim_t;

enum { MOD_QUADS, MOD_QUADS2, MOD_TRIANGLES, MOD_TRISTRIP, MOD_TRIFAN,
       MOD_LINES, MOD_LINESTRIP, MOD_POINTS };

// Walk 'dl' assuming 'bpv' index bytes per vertex. On success, returns a
// MALLOC'd array of the primitives found (caller FREEs) and the total
// vertex count summed across them (used by the caller to pick the winning
// bpv). Returns false -- freeing nothing -- on any malformed/out-of-bounds
// opcode, which is how a wrong bpv guess is rejected.
static bool mod_try_parse_dl ( const u8 *dl, uint n, uint bpv,
    mod_prim_t **out_prims, uint *out_nprims, uint *out_total,
    u32 vat[256], u8 vat_set[256] )
{
    uint cap = 8, np = 0, total = 0, p = 0;
    mod_prim_t *prims = MALLOC(cap*sizeof(*prims));
    memset(vat_set,0,256);

    while ( p < n )
    {
	const u8 op = dl[p];
	if ( op == 0x00 ) { p++; continue; }
	if ( op == 0xe3 ) break;

	if ( op == 0x08 )
	{
	    if ( p+6 > n ) goto fail;
	    vat[dl[p+1]] = xrd_be32(dl+p+2);
	    vat_set[dl[p+1]] = 1;
	    p += 6;
	    continue;
	}
	if ( op == 0x10 )
	{
	    if ( p+5 > n ) goto fail;
	    const u64 adv = 5 + ((u64)xrd_be16(dl+p+1)+1)*4;
	    if ( p+adv > n ) goto fail;
	    p += adv;
	    continue;
	}
	if ( op == 0x61 )
	{
	    if ( p+5 > n ) goto fail;
	    p += 5;
	    continue;
	}

	int kind;
	switch ( op & 0xf8 )
	{
	    case 0x80: kind = MOD_QUADS;      break;
	    case 0x88: kind = MOD_QUADS2;     break;
	    case 0x90: kind = MOD_TRIANGLES;  break;
	    case 0x98: kind = MOD_TRISTRIP;   break;
	    case 0xA0: kind = MOD_TRIFAN;     break;
	    case 0xA8: kind = MOD_LINES;      break;
	    case 0xB0: kind = MOD_LINESTRIP;  break;
	    case 0xB8: kind = MOD_POINTS;     break;
	    default: goto fail;
	}

	if ( p+3 > n ) goto fail;
	const u16 cnt = xrd_be16(dl+p+1);
	p += 3;
	const u64 need = (u64)cnt*bpv;
	if ( p+need > n ) goto fail;

	if ( np == cap ) { cap *= 2; prims = REALLOC(prims,cap*sizeof(*prims)); }
	prims[np].kind = kind;
	prims[np].cnt = cnt;
	prims[np].dl_off = p;
	np++;
	total += cnt;
	p += need;
    }

    if ( !np ) goto fail;
    *out_prims = prims; *out_nprims = np; *out_total = total;
    return true;

 fail:
    FREE(prims);
    return false;
}

// Read a fixed-point/float GX attribute array (big-endian, GX convention).
// fmt: 0=u8 1=s8 2=u16 3=s16 4=f32. Returns false (nothing allocated) if the
// array would run past 'size'.
static bool mod_read_attr ( const u8 *data, uint size, uint off,
    uint count, uint fmt, uint ncomp, uint shift, float **out )
{
    static const uint fmt_sz[5] = { 1,1,2,2,4 };
    if ( fmt > 4 ) return false;
    const uint sz = fmt_sz[fmt];
    if ( (u64)off + (u64)count*ncomp*sz > size ) return false;

    const float scale = fmt == 4 ? 1.0f : 1.0f / (float)(1u<<shift);
    float *out_arr = MALLOC(count*ncomp*sizeof(float));
    const u8 *p = data+off;
    for ( uint i = 0; i < count*ncomp; i++, p += sz )
    {
	s32 raw;
	switch (fmt)
	{
	    case 0: raw = p[0]; break;
	    case 1: raw = (s8)p[0]; break;
	    case 2: raw = xrd_be16(p); break;
	    case 3: raw = (s16)xrd_be16(p); break;
	    default: out_arr[i] = xrd_f32be(p); continue;
	}
	out_arr[i] = raw * scale;
    }
    *out = out_arr;
    return true;
}

enumError DecodeExciteMOD ( const u8 *data, uint size, ccp out_path )
{
    if ( !data || size < 0x40 ) return ERR_NOTHING_TO_DO;

    // The magic isn't always at offset 0 -- most real files carry a small
    // texture-filename table ahead of it.
    uint m = 0; bool found = false;
    for ( ; m+4 <= size; m++ )
    {
	if ( !memcmp(data+m,"3LDN",4) || !memcmp(data+m,"2LDN",4) ) { found = true; break; }
    }
    if ( !found || m+0x38 > size ) return ERR_NOTHING_TO_DO;

    u32 h[14];
    for ( int i = 0; i < 14; i++ ) h[i] = xrd_le32(data+m+i*4);
    u32 dl_end = h[1];
    if ( (dl_end>>16) == 0xe3e3 ) dl_end &= 0xffff; // NDL2 u16 quirk
    const u32 n_pos = h[7];
    if ( !n_pos || n_pos > 100000 ) return ERR_NOTHING_TO_DO;

    // Locate the display list by its CP-register preamble, not a version-
    // specific offset/size field.
    const uint hi = dl_end && m+dl_end <= size ? m+dl_end : size;
    uint dl_off = 0; bool sig_found = false;
    for ( uint q = m; q+14 <= hi; q++ )
    {
	if ( data[q]==0x08 && data[q+1]==0x70 && data[q+6]==0x08
	  && data[q+7]==0x80 && data[q+12]==0x08 && data[q+13]==0x90 )
	    { dl_off = q-m; sig_found = true; break; }
    }
    if ( !sig_found ) return ERR_NOTHING_TO_DO;

    const uint dl_start = m+dl_off;
    const uint dl_size = dl_end > dl_off && m+dl_end <= size ? dl_end-dl_off : size-dl_start;
    const u8 *dl = data+dl_start;

    // Brute-force the index width: the correct one is whichever fully,
    // validly parses the display list while covering the most vertices.
    mod_prim_t *best_prims = 0; uint best_np = 0, best_total = 0, best_bpv = 0;
    u32 vat[256]; u8 vat_set[256];
    u32 best_vat[256]; u8 best_vat_set[256];
    for ( uint bpv = 1; bpv <= 8; bpv++ )
    {
	mod_prim_t *prims; uint np, total;
	if ( !mod_try_parse_dl(dl,dl_size,bpv,&prims,&np,&total,vat,vat_set) )
	    continue;
	if ( total > best_total )
	{
	    FREE(best_prims);
	    best_prims = prims; best_np = np; best_total = total; best_bpv = bpv;
	    memcpy(best_vat,vat,sizeof(vat));
	    memcpy(best_vat_set,vat_set,sizeof(vat_set));
	}
	else
	    FREE(prims);
    }
    if ( !best_prims ) return ERR_NOTHING_TO_DO;
    if ( !best_vat_set[0x70] ) { FREE(best_prims); return ERR_NOTHING_TO_DO; }

    const u32 va = best_vat[0x70];
    const uint pos_elem  = (va>>0)&1, pos_fmt = (va>>1)&7, pos_shft = (va>>4)&0x1f;
    const uint tex_elem  = (va>>21)&1, tex_fmt = (va>>22)&7, tex_shft = (va>>25)&0x1f;
    const uint pos_n = pos_elem ? 3 : 2;
    const uint tex_n = tex_elem ? 2 : 1;

    static const uint fmt_sz[5] = { 1,1,2,2,4 };
    if ( pos_fmt > 4 ) { FREE(best_prims); return ERR_NOTHING_TO_DO; }
    const uint pos_off = m+0x40;
    const uint tex_off = pos_off + n_pos*pos_n*fmt_sz[pos_fmt];

    // Texcoord array length isn't stored -- size it from the highest index
    // any primitive actually uses (position index is always tuple byte 0,
    // texcoord index is the last tuple byte when bpv>1, else always 0).
    uint max_tex = 0;
    for ( uint i = 0; i < best_np; i++ )
    {
	const mod_prim_t *pr = best_prims+i;
	if ( best_bpv < 2 ) continue;
	for ( uint v = 0; v < pr->cnt; v++ )
	{
	    const u8 ti = dl[pr->dl_off + v*best_bpv + best_bpv-1];
	    if ( ti > max_tex ) max_tex = ti;
	}
    }
    const uint n_tex = max_tex+1;

    float *pos_f = 0, *tex_f = 0;
    if ( !mod_read_attr(data,size,pos_off,n_pos,pos_fmt,pos_n,pos_shft,&pos_f)
      || (tex_fmt <= 4 && !mod_read_attr(data,size,tex_off,n_tex,tex_fmt,tex_n,tex_shft,&tex_f)) )
    {
	FREE(best_prims); FREE(pos_f); FREE(tex_f);
	return ERR_NOTHING_TO_DO;
    }

    model_t model; memset(&model,0,sizeof(model));
    mesh_t mesh; memset(&mesh,0,sizeof(mesh));
    snprintf(mesh.name,sizeof(mesh.name),"mod");
    mesh.material_idx = -1;

    mesh.positions = MALLOC(n_pos*sizeof(vec3_t));
    mesh.num_positions = n_pos;
    for ( uint i = 0; i < n_pos; i++ )
    {
	mesh.positions[i].x = pos_f[i*pos_n];
	mesh.positions[i].y = pos_f[i*pos_n+1];
	mesh.positions[i].z = pos_n > 2 ? pos_f[i*pos_n+2] : 0.0f;
    }

    mesh.texcoords = MALLOC(n_tex*sizeof(vec2_t));
    mesh.num_texcoords = n_tex;
    for ( uint i = 0; i < n_tex; i++ )
    {
	mesh.texcoords[i].u = tex_f[i*tex_n];
	mesh.texcoords[i].v = tex_n > 1 ? tex_f[i*tex_n+1] : 0.0f;
    }

    uint tri_cap = best_total*2+8, tri_n = 0;
    vertex_t *tris = MALLOC(tri_cap*sizeof(vertex_t));

    #define MOD_CORNER(idxarr) do { \
	if ( tri_n == tri_cap ) { tri_cap *= 2; tris = REALLOC(tris,tri_cap*sizeof(vertex_t)); } \
	vertex_t *dv = tris + tri_n++; \
	const u8 pi = dl[pr->dl_off + (idxarr)*best_bpv]; \
	const u8 ti = best_bpv > 1 ? dl[pr->dl_off + (idxarr)*best_bpv + best_bpv-1] : 0; \
	dv->position_idx = pi < n_pos ? pi : 0; \
	dv->texcoord_idx = ti < n_tex ? ti : 0; \
	dv->normal_idx = dv->matrix_idx = -1; \
	dv->color_idx[0] = dv->color_idx[1] = -1; \
	for ( int e = 0; e < 7; e++ ) dv->extra_texcoord_idx[e] = -1; \
    } while(0)

    for ( uint i = 0; i < best_np; i++ )
    {
	const mod_prim_t *pr = best_prims+i;
	switch ( pr->kind )
	{
	    case MOD_TRISTRIP:
		for ( uint t = 0; t+2 < pr->cnt; t++ )
		{
		    if ( t & 1 ) { MOD_CORNER(t+1); MOD_CORNER(t); MOD_CORNER(t+2); }
		    else         { MOD_CORNER(t);   MOD_CORNER(t+1); MOD_CORNER(t+2); }
		}
		break;
	    case MOD_TRIFAN:
		for ( uint t = 1; t+1 < pr->cnt; t++ )
		    { MOD_CORNER(0); MOD_CORNER(t); MOD_CORNER(t+1); }
		break;
	    case MOD_TRIANGLES:
		for ( uint t = 0; t+2 < pr->cnt; t += 3 )
		    { MOD_CORNER(t); MOD_CORNER(t+1); MOD_CORNER(t+2); }
		break;
	    case MOD_QUADS:
	    case MOD_QUADS2:
		for ( uint t = 0; t+3 < pr->cnt; t += 4 )
		{
		    MOD_CORNER(t);   MOD_CORNER(t+1); MOD_CORNER(t+2);
		    MOD_CORNER(t);   MOD_CORNER(t+2); MOD_CORNER(t+3);
		}
		break;
	    default: break; // LINES/LINESTRIP/POINTS carry no surface geometry
	}
    }
    #undef MOD_CORNER

    mesh.vertices = tris;
    mesh.num_vertices = tri_n;

    enumError rc = ERR_NOTHING_TO_DO;
    if ( tri_n )
    {
	model.meshes = &mesh;
	model.num_meshes = 1;
	const uint path_len = strlen(out_path);
	const bool is_dae = path_len > 4 && !strcasecmp(out_path+path_len-4,".dae");
	rc = ( is_dae ? ExportModelToDAE(&model,out_path)
	              : ExportModelToGLB(&model,out_path) ) == 0
	     ? ERR_OK : ERR_CANT_CREATE;
    }

    FREE(best_prims);
    FREE(pos_f);
    FREE(tex_f);
    FREE(mesh.positions);
    FREE(mesh.texcoords);
    FREE(mesh.vertices);
    return rc;
}
