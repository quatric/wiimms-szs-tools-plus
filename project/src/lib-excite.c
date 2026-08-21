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
///////////////		.mod 3D models (single-object, textured quad)	///////////////
//-----------------------------------------------------------------------------
//
// Monster Games "3LDN" container, as RE'd across three prior-session and
// this-session investigations against 196 real ExciteBots samples. Layout
// of the sub-header (single-object case, object at file offset 0):
//
//   0x00  "3LDN" magic (4 bytes)
//   0x04  u32le sub-header size (only 0xA0 validated)
//   0x08  u32le per-object id/hash (irrelevant to geometry)
//   0x0c  u32le format/flags constant (only 0x82 validated -- other real
//         samples carry 0x8a and other values that were NOT reverse
//         engineered this session; those are declined)
//   0x18  f32le position/UV scale factor
//   0x1c  u32le position-vertex count
//   0x20  u32le a second count, meaning still not fully understood -- NOT
//         equal to the UV-vertex count in every sample seen (arrow files
//         say 6, sunflower2.mod says 12, but both still only carry 4 UV
//         pairs in the payload), so it is read but NOT relied upon here
//   0x24 / 0x2c / 0x30  three more u32le fields, still unmapped; not used
//
// Payload, immediately following the header at file offset 0x40:
//   - position-vertex-count * (s16be x, s16be y, s16be z), each component
//     scaled by the 0x18 float, i.e. float = raw * scale
//   - exactly 4 * (s16be u, s16be v) UV pairs immediately after, same scale
//   - 0xe3-fill padding (the same GX padding byte used elsewhere in this
//     file's DecodeExciteTEX()) up to whatever the display list's actual
//     start offset is
//   - a GX display list: for every sample validated this session, a single
//     GX_DRAW_TRIANGLESTRIP call (opcode byte 0x98 = GX_TRISTRIP | vtxfmt0,
//     see Dolphin's OpcodeDecoding.cpp / libogc GX enums for the primitive
//     opcode table -- low 3 bits select the vertex format slot, 0x80 quads,
//     0x90 triangles, 0x98 tristrip, 0xA0 fan, 0xA8 lines, 0xB0 linestrip,
//     0xB8 points) followed by a u16be vertex count, followed by that many
//     (u8 position_idx, u8 texcoord_idx) index pairs indexing the position
//     and UV arrays above respectively
//
// VALIDATION SCOPE (deliberately narrow -- see repo conventions on scope
// discipline): this decoder only accepts files where sub-header size==0xA0
// AND the 0x0c constant==0x82 AND the display list is exactly one
// GX_DRAW_TRIANGLESTRIP call with 1-byte position/texcoord indices found
// immediately after the 0xe3 padding run. Of the 196 real .mod samples
// available for testing, only 3 matched this exact shape byte-for-byte
// (arrow_obj.mod, arrow_point.mod, sunflower2.mod) and all 3 reconstruct
// into internally-consistent, non-degenerate geometry. Everything else --
// the 106/196 multi-object container files (a {u32,u32,8 zero bytes} table
// of embedded "3LDN" sub-objects found by a prior session), the ~90 single-
// object files using a different 0x0c constant or non-tristrip display
// lists, and any file with more than one draw call -- is intentionally
// UNSUPPORTED and returns ERR_NOTHING_TO_DO rather than risk emitting
// plausible-but-wrong geometry. Widening this requires either more RE
// (Ghidra against ExciteBots' main.dol GX vertex-format setup) or a larger
// corpus of independently-checkable samples than were available here.
//-----------------------------------------------------------------------------

static inline u32 xrd_le32 ( const u8 *p )
{
    return (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0];
}

static inline float xrd_f32le_p ( const u8 *p ) { return xrd_f32le(p); }

static inline s16 xrd_be16s ( const u8 *p ) { return (s16)xrd_be16(p); }

enumError DecodeExciteMOD ( const u8 *data, uint size, ccp out_dae_path )
{
    if ( !data || size < 0xA0+0x40 || memcmp(data,"3LDN",4) )
	return ERR_NOTHING_TO_DO;

    const u32 subsize = xrd_le32(data+0x04);
    const u32 flags   = xrd_le32(data+0x0c);
    if ( subsize != 0xA0 || flags != 0x82 )
	return ERR_NOTHING_TO_DO;

    const float scale = xrd_f32le_p(data+0x18);
    const u32 n_pos   = xrd_le32(data+0x1c);
    if ( !n_pos || n_pos > 256 )
	return ERR_NOTHING_TO_DO;

    const uint pos_off = 0x40;
    const uint pos_bytes = n_pos*6;
    const uint n_uv = 4; // validated constant, see comment above
    const uint uv_off = pos_off + pos_bytes;
    const uint uv_bytes = n_uv*4;
    const uint dl_search_off = uv_off + uv_bytes;

    if ( dl_search_off > size )
	return ERR_NOTHING_TO_DO;

    // Scan forward through the 0xe3 padding run (and any unmapped GX
    // register-setup preamble bytes) for the tristrip opcode byte. Bound
    // the search so we don't wander into unrelated data.
    uint p = dl_search_off;
    const uint scan_limit = size < dl_search_off+64 ? size : dl_search_off+64;
    while ( p < scan_limit && data[p] != 0x98 )
	p++;
    if ( p >= scan_limit || data[p] != 0x98 )
	return ERR_NOTHING_TO_DO;

    p++; // past opcode
    if ( p+2 > size )
	return ERR_NOTHING_TO_DO;
    const u16 n_strip = xrd_be16(data+p);
    p += 2;
    if ( !n_strip || n_strip < 3 || (u64)p + (u64)n_strip*2 > size )
	return ERR_NOTHING_TO_DO;

    // Validate every index is in-bounds before touching anything else.
    for ( uint i = 0; i < n_strip; i++ )
    {
	const u8 pi = data[p+i*2], ti = data[p+i*2+1];
	if ( pi >= n_pos || ti >= n_uv )
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
	mesh.positions[i].x = xrd_be16s(data+pos_off+i*6)   * scale;
	mesh.positions[i].y = xrd_be16s(data+pos_off+i*6+2) * scale;
	mesh.positions[i].z = xrd_be16s(data+pos_off+i*6+4) * scale;
    }

    mesh.texcoords = MALLOC(n_uv*sizeof(vec2_t));
    mesh.num_texcoords = n_uv;
    for ( uint i = 0; i < n_uv; i++ )
    {
	mesh.texcoords[i].u = xrd_be16s(data+uv_off+i*4)   * scale;
	mesh.texcoords[i].v = xrd_be16s(data+uv_off+i*4+2) * scale;
    }

    // Triangulate the GX triangle strip: vertices (0,1,2), (1,2,3) or
    // (2,1,3) alternating winding, (2,3,4), ...
    const uint n_tris = n_strip-2;
    mesh.vertices = MALLOC(n_tris*3*sizeof(vertex_t));
    mesh.num_vertices = n_tris*3;
    for ( uint t = 0; t < n_tris; t++ )
    {
	uint idx[3];
	if ( t & 1 )
	    idx[0]=t+1, idx[1]=t, idx[2]=t+2;
	else
	    idx[0]=t, idx[1]=t+1, idx[2]=t+2;

	for ( int k = 0; k < 3; k++ )
	{
	    vertex_t *v = mesh.vertices + t*3+k;
	    const uint si = idx[k];
	    v->position_idx = data[p+si*2];
	    v->texcoord_idx = data[p+si*2+1];
	    v->normal_idx = v->matrix_idx = -1;
	    v->color_idx[0] = v->color_idx[1] = -1;
	    for ( int e = 0; e < 7; e++ ) v->extra_texcoord_idx[e] = -1;
	}
    }

    model.meshes = &mesh;
    model.num_meshes = 1;

    const int rc = ExportModelToDAE(&model,out_dae_path);

    FREE(mesh.positions);
    FREE(mesh.texcoords);
    FREE(mesh.vertices);
    return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}
