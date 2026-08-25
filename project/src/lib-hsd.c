
/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "sysdolphin" (HSD) .dat archive support                 *
 *                                                                         *
 *   The container/relocation layout implemented here is ported from        *
 *   Ploaj/HSDLib (https://github.com/Ploaj/HSDLib), MIT licensed:          *
 *	HSDRaw/HSDRawFile.cs        -- header + relocation/root parsing     *
 *	HSDRaw/Common/HSD_TOBJ.cs   -- HSD_Image / HSD_Tlut field layout    *
 *   cross-checked against real retail bytes (see comments below).          *
 *                                                                         *
 ***************************************************************************/

#include "lib-hsd.h"
#include "lib-image.h"
#include "lib-model-dae.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    container			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool check_hsd_header
	( const u8 *data, uint size, uint *ret_reloc_off,
	  uint *ret_n_reloc, uint *ret_n_root, uint *ret_n_ref )
{
    if ( !data || size < 0x40 )
	return false;

    // HSDRawFile.cs Open(): fsize, relocOffset+0x20, relocCount, rootCount, refCount
    const u32 fsize	= be32(data);
    const u32 reloc_off	= be32(data+0x04) + HSD_DATA_BASE;
    const u32 n_reloc	= be32(data+0x08);
    const u32 n_root	= be32(data+0x0c);
    const u32 n_ref	= be32(data+0x10);

    if ( fsize != size )
	return false;
    if ( reloc_off < HSD_DATA_BASE || reloc_off > size )
	return false;
    if ( n_root < 1 || n_root > 0x10000 || n_ref > 0x10000 )
	return false;

    // relocation table + the (offset,name offset) pairs must fit
    const u64 need = (u64)reloc_off + 4*(u64)n_reloc + 8*((u64)n_root+n_ref);
    if ( need > size )
	return false;

    // The version tag is 4 printable ASCII chars, e.g. "001B" -- but retail
    // files do ship it all-zero (Melee's MnSlChr.usd), so only reject bytes
    // that are neither NUL nor printable.
    for ( uint i = 0x14; i < 0x18; i++ )
	if ( data[i] && ( data[i] < 0x20 || data[i] > 0x7e ) )
	    return false;

    if (ret_reloc_off) *ret_reloc_off = reloc_off;
    if (ret_n_reloc)   *ret_n_reloc   = n_reloc;
    if (ret_n_root)    *ret_n_root    = n_root;
    if (ret_n_ref)     *ret_n_ref     = n_ref;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

bool IsHSD ( const u8 *data, uint size )
{
    return check_hsd_header(data,size,0,0,0,0);
}

///////////////////////////////////////////////////////////////////////////////

static int cmp_u32 ( const void *a, const void *b )
{
    const u32 va = *(const u32*)a, vb = *(const u32*)b;
    return va < vb ? -1 : va > vb;
}

bool ScanHSD ( hsd_t *hsd, const u8 *data, uint size )
{
    DASSERT(hsd);
    memset(hsd,0,sizeof(*hsd));

    uint reloc_off, n_reloc, n_root, n_ref;
    if (!check_hsd_header(data,size,&reloc_off,&n_reloc,&n_root,&n_ref))
	return false;

    hsd->data		= data;
    hsd->size		= size;
    hsd->fsize		= be32(data);
    hsd->reloc_off	= reloc_off;
    hsd->n_reloc	= n_reloc;
    hsd->n_root		= n_root;
    hsd->n_ref		= n_ref;
    memcpy(hsd->version,data+0x14,4);
    hsd->version[4]	= 0;

    if (!n_reloc)
	return true;

    hsd->rel_src  = CALLOC(n_reloc,sizeof(*hsd->rel_src));
    hsd->rel_dest = CALLOC(n_reloc,sizeof(*hsd->rel_dest));
    hsd->target   = CALLOC(n_reloc,sizeof(*hsd->target));

    // Each relocation entry names a location holding a 0x20-relative pointer;
    // both the location and the value it holds get the data base added.
    for ( uint i = 0; i < n_reloc; i++ )
    {
	const s32 loc = (s32)be32(data+reloc_off+4*i) + HSD_DATA_BASE;
	if ( loc < HSD_DATA_BASE || (u32)loc+4 > reloc_off )
	    continue;
	const s32 dest = (s32)be32(data+loc) + HSD_DATA_BASE;
	if ( dest < HSD_DATA_BASE || (u32)dest >= reloc_off )
	    continue; // includes HSDLib's "alternate null pointer" case

	hsd->rel_src [hsd->n_rel] = loc;
	hsd->rel_dest[hsd->n_rel] = dest;
	hsd->target  [hsd->n_rel] = dest;
	hsd->n_rel++;
    }

    // sorted unique target list -> lets us bound a pointed-to buffer by the
    // start of the next pointed-to object (buffers are never overlapped)
    qsort(hsd->target,hsd->n_rel,sizeof(*hsd->target),cmp_u32);
    uint n = 0;
    for ( uint i = 0; i < hsd->n_rel; i++ )
	if ( !n || hsd->target[i] != hsd->target[n-1] )
	    hsd->target[n++] = hsd->target[i];
    hsd->n_target = n;

    return true;
}

///////////////////////////////////////////////////////////////////////////////

void ResetHSD ( hsd_t *hsd )
{
    if (hsd)
    {
	FREE(hsd->rel_src);
	FREE(hsd->rel_dest);
	FREE(hsd->target);
	memset(hsd,0,sizeof(*hsd));
    }
}

///////////////////////////////////////////////////////////////////////////////

// pointer stored at the absolute location 'loc', or 0 if that location is not
// relocated (== a null pointer)
static u32 get_ptr ( const hsd_t *hsd, u32 loc )
{
    for ( uint i = 0; i < hsd->n_rel; i++ )
	if ( hsd->rel_src[i] == loc )
	    return hsd->rel_dest[i];
    return 0;
}

// size available at pointed-to offset 'off': up to the next pointed-to
// object, or up to the relocation table if it is the last one
static u32 get_blob_size ( const hsd_t *hsd, u32 off )
{
    uint lo = 0, hi = hsd->n_target;
    while ( lo < hi )
    {
	const uint mid = (lo+hi)/2;
	if ( hsd->target[mid] <= off )
	    lo = mid+1;
	else
	    hi = mid;
    }
    const u32 end = lo < hsd->n_target ? hsd->target[lo] : hsd->reloc_off;
    return end > off ? end - off : 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    textures			///////////////
///////////////////////////////////////////////////////////////////////////////
//
// HSD_Image (HSD_TOBJ.cs class HSD_Image):
//	+0x00 ptr  pixel data
//	+0x04 u16  width
//	+0x06 u16  height
//	+0x08 u32  GXTexFmt
//
// The GX texture format numbering is identical to this project's own
// image_format_t (IMG_I4=0 .. IMG_CMPR=0x0e), so the raw value is used as-is
// and the existing GameCube/Wii pixel decoders in lib-image1.c do the work.
//
// Melee stores the format as a plain big-endian int32 at +0x08.  The Wii
// channel "TV no Tomo" uses a compacted variant of the same struct that packs
// the format into the *high* byte at +0x08 with a mipmap-level count in the
// next byte (verified on every .dat of the TV no Tomo channel: the low three
// bytes are always 0x01 0x00 0x00 there while the high byte is a valid GX
// format that exactly predicts the pixel buffer's real size).  Both spellings
// are accepted; the size check below rejects a wrong guess.
//
// HSD_Tlut (HSD_TOBJ.cs class HSD_Tlut):
//	+0x00 ptr  palette data
//	+0x04 u32  GXTlutFmt (0=IA8, 1=RGB565, 2=RGB5A3, == palette_format_t)
//	+0x08 u32  GXTlut
//	+0x0C u16  color count
//
///////////////////////////////////////////////////////////////////////////////

#define HSD_COPY_SLACK 64	// zero padding, see ExportHSDTextures()

typedef struct hsd_tex_t
{
    u32			off;		// offset of the HSD_Image struct
    u32			data_off;	// offset of the pixel data
    uint		width;
    uint		height;
    image_format_t	iform;
    uint		img_size;	// decoded-from size in bytes

    u32			pal_off;	// 0 or offset of the palette data
    palette_format_t	pform;
    uint		n_pal;
}
hsd_tex_t;

//-----------------------------------------------------------------------------

static bool is_gx_format ( u32 fmt )
{
    switch (fmt)
    {
	case IMG_I4: case IMG_I8: case IMG_IA4: case IMG_IA8:
	case IMG_RGB565: case IMG_RGB5A3: case IMG_RGBA32:
	case IMG_C4: case IMG_C8: case IMG_C14X2: case IMG_CMPR:
	    return true;
	default:
	    return false;
    }
}

static uint gx_image_size ( uint width, uint height, image_format_t iform )
{
    uint size = 0;
    CalcImageGeometry(iform,width,height,0,0,0,0,&size);
    return size;
}

//-----------------------------------------------------------------------------

#define HSD_IMAGE_SIZE 0x0c	// packed HSD_Image, compacted variant
#define HSD_TLUT_SIZE  0x10	// packed HSD_Tlut

// already identified HSD_Image at exactly this offset, or 0
static const hsd_tex_t * find_tex_at
	( const hsd_tex_t *tex, uint n_tex, u32 off )
{
    for ( uint i = 0; i < n_tex; i++ )
	if ( tex[i].off == off )
	    return tex + i;
    return 0;
}

// Is 'off' a relocated pointer location holding a usable HSD_Tlut with at
// least 'min_pal' colors? Optionally returns the palette it names.
static bool is_hsd_tlut
	( const hsd_t *hsd, u32 off, uint min_pal,
	  u32 *ret_pal_off, u32 *ret_pform, uint *ret_n_pal )
{
    if ( off + 0x0e > hsd->reloc_off )
	return false;

    const u32 pal_off = get_ptr(hsd,off);
    if (!pal_off)
	return false;

    const u8 *s = hsd->data + off;
    const u32 pform = be32(s+0x04);
    const uint n_pal = be16(s+0x0c);
    if ( pform > PAL_RGB5A3 || n_pal < min_pal || n_pal > 0x4000 )
	return false;
    if ( pal_off + 2*n_pal > hsd->reloc_off )
	return false;
    if ( get_blob_size(hsd,pal_off) < 2*n_pal )
	return false;

    if (ret_pal_off) *ret_pal_off = pal_off;
    if (ret_pform)   *ret_pform	  = pform;
    if (ret_n_pal)   *ret_n_pal	  = n_pal;
    return true;
}

//-----------------------------------------------------------------------------

// Scan for HSD_Image structs. Every candidate must be a relocated pointer
// location whose target is large enough to hold exactly the pixel data the
// width/height/format triple describes -- the graph carries no type tags, so
// this size agreement (plus the format enum check) is the identification.
static uint find_textures ( const hsd_t *hsd, hsd_tex_t **ret_tex )
{
    *ret_tex = 0;
    if (!hsd->n_rel)
	return 0;

    hsd_tex_t *tex = CALLOC(hsd->n_rel,sizeof(*tex));
    uint n_tex = 0;

    // pass 1: images
    for ( uint i = 0; i < hsd->n_rel; i++ )
    {
	const u32 off = hsd->rel_src[i];
	if ( off + 0x0c > hsd->reloc_off )
	    continue;

	const u8 *s	= hsd->data + off;
	const uint width  = be16(s+0x04);
	const uint height = be16(s+0x06);
	if ( !width || !height || width > 4096 || height > 4096 )
	    continue;

	const u32 fmt_word = be32(s+0x08);
	const u32 fmt = fmt_word < 0x100 ? fmt_word : fmt_word >> 24;
	if (!is_gx_format(fmt))
	    continue;

	const u32 data_off = hsd->rel_dest[i];
	const uint img_size = gx_image_size(width,height,fmt);
	if (!img_size)
	    continue;
	const u32 blob = get_blob_size(hsd,data_off);
	// mipmaps may follow the main level, so accept anything at least as
	// large, but require the main level to be present in full
	if ( blob < img_size || data_off + img_size > hsd->reloc_off )
	    continue;

	hsd_tex_t *t = tex + n_tex++;
	t->off		= off;
	t->data_off	= data_off;
	t->width	= width;
	t->height	= height;
	t->iform	= fmt;
	t->img_size	= img_size;
	t->pform	= PAL_INVALID;
    }

    // pass 2: palettes for the CI formats.
    for ( uint i = 0; i < n_tex; i++ )
    {
	hsd_tex_t *t = tex + i;
	uint min_pal, max_pal;
	switch (t->iform)
	{
	    case IMG_C4:    min_pal = max_pal = 1<<4;  break;
	    case IMG_C8:    min_pal = max_pal = 1<<8;  break;
	    case IMG_C14X2: min_pal = 1; max_pal = 1<<14; break; // sparse
	    default:	    continue;
	}

	// (a) the exact route: a HSD_TOBJ references its image at +0x4C and
	// its tlut at +0x50 (HSD_TOBJ.cs ImageData/TlutData), so whatever
	// points at this image has the matching tlut in the very next word.
	for ( uint j = 0; j < hsd->n_rel && !t->pal_off; j++ )
	{
	    if ( hsd->rel_dest[j] != t->off )
		continue;
	    const u32 loc = hsd->rel_src[j] + 4;
	    for ( uint k = 0; k < hsd->n_rel; k++ )
	    {
		if ( hsd->rel_src[k] != loc )
		    continue;
		const u32 off = hsd->rel_dest[k];
		if ( off + 0x0e > hsd->reloc_off )
		    break;
		const u8 *s = hsd->data + off;
		const u32 pform = be32(s+0x04);
		const uint n_pal = be16(s+0x0c);
		if ( pform > PAL_RGB5A3 || !n_pal || n_pal > 0x4000 )
		    break;
		const u32 pal_off = get_ptr(hsd,off);
		if ( !pal_off || pal_off + 2*n_pal > hsd->reloc_off )
		    break;
		t->pal_off = pal_off;
		t->pform   = pform;
		t->n_pal   = n_pal < max_pal ? n_pal : max_pal;
		break;
	    }
	}
	if (t->pal_off)
	    continue;

	// (b) fallback for the compacted variant, where nothing points at the
	// image struct at all (TV no Tomo): the HSD_Image structs of one object
	// form a packed array that is directly followed by the packed array of
	// their HSD_Tlut structs, image N pairing with tlut N (verified on
	// MiiButton.dat: 8 images at 0x93b4+0x0c*N -> 8 tluts at 0x9484+0x10*N
	// naming 8 distinct 256-color palettes at 0x8260+0x200*N).  So find this
	// image's index inside its own run, then step that far into the tlut
	// run.  Taking simply the nearest following tlut -- which is what this
	// used to do -- hands every image of such a run the very same palette.
	uint idx = 0;
	for ( u32 p = t->off; p >= HSD_DATA_BASE + HSD_IMAGE_SIZE; p -= HSD_IMAGE_SIZE )
	{
	    const hsd_tex_t *prev = find_tex_at(tex,n_tex,p-HSD_IMAGE_SIZE);
	    if ( !prev || prev->iform != t->iform )
		break;
	    idx++;
	}
	u32 run_end = t->off + HSD_IMAGE_SIZE;
	for (;;)
	{
	    const hsd_tex_t *next = find_tex_at(tex,n_tex,run_end);
	    if ( !next || next->iform != t->iform )
		break;
	    run_end += HSD_IMAGE_SIZE;
	}

	// the first plausible tlut behind the whole image run starts the run
	u32 first = 0;
	for ( uint j = 0; j < hsd->n_rel; j++ )
	{
	    const u32 off = hsd->rel_src[j];
	    if ( off < run_end || ( first && off >= first ) )
		continue;
	    if (is_hsd_tlut(hsd,off,min_pal,0,0,0))
		first = off;
	}
	if (!first)
	    continue;

	u32 pal_off = 0, pform = 0;
	uint n_pal = 0;
	if ( !idx
	    || !is_hsd_tlut(hsd,first+HSD_TLUT_SIZE*idx,min_pal,&pal_off,&pform,&n_pal) )
	{
	    // no run behind this one (or a shorter one): stay with the old,
	    // single-palette behaviour instead of inventing a pairing
	    is_hsd_tlut(hsd,first,min_pal,&pal_off,&pform,&n_pal);
	}

	// lib-image1.c TransformPalette() sizes its working palette by the
	// index width of the target IMG_X_PAL* format, so more entries than
	// the indices can address must not be handed over; sysdolphin does
	// store oversized TLUTs (TV no Tomo pairs 256-color tables with CI4
	// images), and only the addressable prefix is ever sampled.
	t->pal_off	= pal_off;
	t->pform	= pform;
	t->n_pal	= n_pal < max_pal ? n_pal : max_pal;
    }

    // pass 3: de-duplicate. Several HSD_Image structs legitimately share one
    // pixel buffer (Melee's character-select portraits are referenced from
    // more than one menu object), and the structural identification can also
    // latch onto a second, unreferenced struct describing the same buffer.
    // Keep one entry per (buffer,geometry,format), preferring the one that a
    // HSD_TOBJ actually points at -- that is the one with the right palette.
    uint n_keep = 0;
    for ( uint i = 0; i < n_tex; i++ )
    {
	bool dup = false;
	for ( uint j = 0; j < n_keep; j++ )
	{
	    hsd_tex_t *k = tex + j;
	    if (   k->data_off != tex[i].data_off
		|| k->width	!= tex[i].width
		|| k->height	!= tex[i].height
		|| k->iform	!= tex[i].iform )
		continue;
	    if ( !k->pal_off && tex[i].pal_off )
		*k = tex[i];	// the better identified one wins
	    dup = true;
	    break;
	}
	if (!dup)
	    tex[n_keep++] = tex[i];
    }

    *ret_tex = tex;
    return n_keep;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     export			///////////////
///////////////////////////////////////////////////////////////////////////////

int ExportHSDTextures
	( const hsd_t *hsd, ccp dest_dir, ccp basename )
{
    if ( !hsd || !hsd->data || !dest_dir )
	return -1;

    hsd_tex_t *tex = 0;
    const uint n_tex = find_textures(hsd,&tex);
    if (!n_tex)
    {
	FREE(tex);
	return 0;
    }

    char dir[PATH_MAX];
    StringCopyS(dir,sizeof(dir),dest_dir);
    CreatePath(dir,true);

    char base[80];
    StringCopyS(base,sizeof(base),basename&&*basename?basename:"hsd");
    char *dot = strrchr(base,'.');
    if ( dot && dot > base && !strcasecmp(dot,".dat") )
	*dot = 0;

    int written = 0;
    for ( uint i = 0; i < n_tex; i++ )
    {
	const hsd_tex_t *t = tex + i;

	Image_t img;
	InitializeIMG(&img);
	// The native decoders in lib-image1.c walk whole blocks and the
	// palette helpers peek one entry past the used range (they normally
	// operate on a pointer into a much larger loaded file), so both
	// buffers get zero padding instead of an exact-size copy.
	img.data		= CALLOC(1,t->img_size+HSD_COPY_SLACK);
	memcpy(img.data,hsd->data+t->data_off,t->img_size);
	img.data_alloced	= true;
	img.data_size		= t->img_size;
	img.width		= t->width;
	img.height		= t->height;
	img.iform		= img.info_iform = t->iform;
	img.info_fform		= FF_PNG;
	img.info_n_image	= 1;
	img.endian		= &be_func;
	CalcImageGeometry(img.iform,img.width,img.height,
				&img.xwidth,&img.xheight,0,0,0);

	if (t->pal_off)
	{
	    img.pal		= CALLOC(1,4*t->n_pal+HSD_COPY_SLACK);
	    memcpy(img.pal,hsd->data+t->pal_off,2*t->n_pal);
	    img.pal_alloced	= true;
	    img.pal_size	= 2*t->n_pal;
	    img.n_pal		= t->n_pal;
	    img.pform		= img.info_pform = t->pform;
	}

	char path[PATH_MAX];
	snprintf(path,sizeof(path),"%s/%s.tex%03u_%ux%u_%s.png",
		dir, base, i, t->width, t->height,
		GetImageFormatName(t->iform,"?") );

	if ( !ConvertIMG(&img,false,0,IMG_X_RGB,PAL_INVALID)
	    && !SavePNG(&img,false,0,path,0,0,true,0) )
	{
	    written++;
	}
	ResetIMG(&img);
    }

    FREE(tex);
    return written;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHSDTexturesFromData
	( const u8 *data, uint size, ccp dest_dir, ccp basename )
{
    hsd_t hsd;
    if (!ScanHSD(&hsd,data,size))
	return -1;
    const int stat = ExportHSDTextures(&hsd,dest_dir,basename);
    ResetHSD(&hsd);
    return stat;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     model geometry		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// JOBJ (0x40 bytes; HSD_JOBJ.cs):
//	+0x00 ptr  class name (unused here)
//	+0x04 u32  flags
//	+0x08 ptr  child JOBJ (first-child/next-sibling tree)
//	+0x0C ptr  next JOBJ (sibling)
//	+0x10 ptr  DOBJ list head
//	+0x14 f32  RX  +0x18 RY  +0x1C RZ  (radians)
//	+0x20 f32  SX  +0x24 SY  +0x28 SZ
//	+0x2C f32  TX  +0x30 TY  +0x34 TZ
//	+0x38 ptr  inverse world transform (unused here)
//	+0x3C ptr  ROBJ (unused here)
//
// DOBJ (0x10 bytes; HSD_DOBJ.cs): class name ptr, next DOBJ ptr, MOBJ
// (material, unused here) ptr @0x08, POBJ ptr @0x0C.
//
// POBJ (0x18 bytes; HSD_POBJ.cs): unused @0x00, next POBJ ptr @0x04,
// GX_Attribute array ptr @0x08, flags (u16) @0x0C, display-list size in
// 32-byte units (s16) @0x0E, display-list buffer ptr @0x10, and a
// flags-dependent union @0x14 (SingleBoundJOBJ / envelope weights / shape
// set -- not consumed here, see scope note below).
//
// GX_Attribute (0x18 bytes; GX/GX_Attribute.cs): name (u32, GX attribute ID),
// type (u32: 0 NONE, 1 DIRECT, 2 INDEX8, 3 INDEX16), component-count enum
// (u32, unused here -- stride/component-size already gives the real count,
// see below), component-type enum (u32: 0 U8, 1 S8, 2 U16, 3 S16, 4 F32),
// fixed-point scale (u8), per-element byte stride (s16), buffer ptr. The
// array is terminated by an entry named GX_VA_NULL (0xff).
//
// All three struct layouts, the attribute array shape, and the real GX
// display-list opcode stream were confirmed byte-for-byte against a real
// file (Super Smash Bros. Melee's TyBox.dat item-box model) before being
// trusted -- not copied from Ploaj/HSDLib (MIT licensed) on faith alone:
// root table -> "ToyBoxModel_TopN_joint" resolved correctly; its DOBJ/POBJ
// chain's GX_Attribute array (POS index8/S16, NBT index8/F32, TEX0/TEX1
// index8/S16 both sharing one buffer) predicted the real per-vertex tuple
// width (4 bytes) the display list actually uses; and the position buffer
// -- found via the relocation table, not a raw-offset guess -- decoded to
// plausible small-object coordinates.
//
// Scope, deliberately: static geometry only. Each POBJ's vertices bind to
// the JOBJ that owns it (its DOBJ's parent), matching POBJ_FLAG.ENVELOPE/
// SHAPEANIM both being unset (i.e. the +0x14 union either null or a
// SingleBoundJOBJ, and this doesn't yet follow a non-null SingleBoundJOBJ
// override, only the owning-joint default). Weighted (ENVELOPE) and morph
// (SHAPEANIM) POBJs, and MOBJ material/texture binding, are real further
// work broken out from this pass rather than guessed at -- see lib-hsd.h.

static inline float bef32 ( const u8 *p )
{
    const u32 v = be32(p);
    float f; memcpy(&f,&v,4); return f;
}

enum
{
    HSD_GX_VA_POS  = 9,
    HSD_GX_VA_NRM  = 10,
    HSD_GX_VA_CLR0 = 11,
    HSD_GX_VA_CLR1 = 12,
    HSD_GX_VA_TEX0 = 13,
    HSD_GX_VA_NBT  = 25,
    HSD_GX_VA_NULL = 0xff,
};
enum { HSD_GX_NONE=0, HSD_GX_DIRECT=1, HSD_GX_INDEX8=2, HSD_GX_INDEX16=3 };
enum { HSD_GX_U8=0, HSD_GX_S8=1, HSD_GX_U16=2, HSD_GX_S16=3, HSD_GX_F32=4 };

typedef struct hsd_attr_t
{
    u32 name, type, ctype;
    u8  scale;
    int stride;		// bytes per element (buffer stride, or DIRECT width)
    u32 buf_off;	// absolute; 0 if unresolved/not applicable
}
hsd_attr_t;

#define HSD_MAX_ATTR 16

// Reads the GX_Attribute array at 'off' (relative to the JOBJ graph, i.e.
// already resolved through get_ptr()) until the GX_VA_NULL terminator.
static uint read_gx_attrs ( const hsd_t *hsd, u32 off, hsd_attr_t *out )
{
    uint n = 0;
    while ( n < HSD_MAX_ATTR && off && (u64)off+0x18 <= hsd->reloc_off )
    {
	const u8 *s = hsd->data+off;
	const u32 name = be32(s);
	if ( name == HSD_GX_VA_NULL )
	    break;
	out[n].name    = name;
	out[n].type    = be32(s+4);
	out[n].ctype   = be32(s+0xC);
	out[n].scale   = s[0x10];
	out[n].stride  = (s16)be16(s+0x12);
	out[n].buf_off = get_ptr(hsd,off+0x14);
	n++;
	off += 0x18;
    }
    return n;
}

static uint hsd_ctype_size ( u32 ctype )
{
    switch (ctype) { case HSD_GX_U16: case HSD_GX_S16: return 2; case HSD_GX_F32: return 4; default: return 1; }
}

// Decodes one attribute's element at 'src' ('stride' bytes) into up to
// 'max_comp' floats. Integer types are fixed-point (/2^scale, matching
// GX_Attribute.GetDecodedDataAt()); colour attributes (CLR0/CLR1) always
// decode to 4 RGBA floats regardless of nominal component count, matching
// GetColorAt() -- 'ctype' there reuses GXCompTypeClr's own numbering
// (0 RGB565, 1 RGB8, 2 RGBX8, 3 RGBA4, 5 RGBA8; 4 RGBA6 is approximated as
// opaque white, like the reference implementation's own "TODO" admits).
static uint decode_gx_elem ( const hsd_attr_t *a, const u8 *src, float *out, uint max_comp )
{
    if ( a->name == HSD_GX_VA_CLR0 || a->name == HSD_GX_VA_CLR1 )
    {
	out[0]=out[1]=out[2]=out[3]=1;
	switch (a->ctype)
	{
	    case 0: { const u32 v=be16(src); out[0]=((v&0x1F)<<3)/255.0f; out[1]=(((v>>5)&0x3F)<<2)/255.0f; out[2]=(((v>>11)&0x1F)<<3)/255.0f; break; }
	    case 1: case 2: out[0]=src[0]/255.0f; out[1]=src[1]/255.0f; out[2]=src[2]/255.0f; out[3]= a->ctype==2 ? src[3]/255.0f : 1; break;
	    case 3: out[0]=(src[0]>>4)/15.0f; out[1]=(src[0]&0xF)/15.0f; out[2]=(src[1]>>4)/15.0f; out[3]=(src[1]&0xF)/15.0f; break;
	    case 5: out[0]=src[0]/255.0f; out[1]=src[1]/255.0f; out[2]=src[2]/255.0f; out[3]=src[3]/255.0f; break;
	    default: break; // RGBA6, approximate: leave opaque white
	}
	return 4;
    }

    const uint esz = hsd_ctype_size(a->ctype);
    uint n = a->stride>0 ? (uint)a->stride/(esz?esz:1) : 0;
    if ( n > max_comp ) n = max_comp;
    const float fscale = a->ctype == HSD_GX_F32 ? 1.0f : 1.0f/(float)(1u<<a->scale);
    for ( uint i = 0; i < n; i++ )
    {
	if ( a->ctype == HSD_GX_F32 )
	{
	    const u32 b = be32(src+4*i);
	    memcpy(out+i,&b,4);
	    continue;
	}
	s32 raw;
	switch (a->ctype)
	{
	    case HSD_GX_U8:  raw = src[i];		break;
	    case HSD_GX_S8:  raw = (s8)src[i];		break;
	    case HSD_GX_U16: raw = be16(src+2*i);	break;
	    case HSD_GX_S16: raw = (s16)be16(src+2*i);	break;
	    default:	     raw = src[i];		break;
	}
	out[i] = raw*fscale;
    }
    return n;
}

typedef struct hsd_vtx_t
{
    float pos[3];
    float nrm[3]; bool has_nrm;
    float uv[2];  bool has_uv;
    float clr[4]; bool has_clr;
}
hsd_vtx_t;

// Reads one vertex's worth of attribute data from the display-list cursor
// '*dl' and advances it. DIRECT attributes are consumed inline; INDEX8/
// INDEX16 read an index and fetch the element from the attribute's own
// buffer. Returns false (leaving '*dl' unspecified) on any out-of-bounds
// read, which the caller treats as "stop decoding this display list".
static bool hsd_fetch_vertex
	( const hsd_t *hsd, const hsd_attr_t *attrs, uint n_attr,
	  const u8 **dl, const u8 *dl_end, hsd_vtx_t *v )
{
    memset(v,0,sizeof(*v));
    for ( uint a = 0; a < n_attr; a++ )
    {
	const hsd_attr_t *at = attrs+a;
	if ( at->type == HSD_GX_NONE )
	    continue;

	const u8 *src;
	if ( at->type == HSD_GX_DIRECT )
	{
	    if ( at->stride < 0 || *dl + at->stride > dl_end )
		return false;
	    src = *dl;
	    *dl += at->stride;
	}
	else
	{
	    uint idx;
	    if ( at->type == HSD_GX_INDEX8 )
	    {
		if ( *dl + 1 > dl_end ) return false;
		idx = **dl; *dl += 1;
	    }
	    else if ( at->type == HSD_GX_INDEX16 )
	    {
		if ( *dl + 2 > dl_end ) return false;
		idx = be16(*dl); *dl += 2;
	    }
	    else
		continue;

	    if ( !at->buf_off || at->stride <= 0 )
		continue;
	    src = hsd->data + at->buf_off + (u64)idx*(uint)at->stride;
	    if ( src < hsd->data || (u64)(src-hsd->data)+(uint)at->stride > hsd->reloc_off )
		return false;
	}

	float comp[9];
	decode_gx_elem(at,src,comp,9);
	switch (at->name)
	{
	    case HSD_GX_VA_POS:  memcpy(v->pos,comp,12); break;
	    case HSD_GX_VA_NRM:  memcpy(v->nrm,comp,12); v->has_nrm = true; break;
	    case HSD_GX_VA_NBT:  memcpy(v->nrm,comp,12); v->has_nrm = true; break; // binormal/tangent (comp[3..8]) not used yet
	    case HSD_GX_VA_TEX0: memcpy(v->uv,comp,8); v->has_uv = true; break;
	    case HSD_GX_VA_CLR0: memcpy(v->clr,comp,16); v->has_clr = true; break;
	    default: break;
	}
    }
    return true;
}

// Growable triangle-corner buffer: every corner gets its own entry (no
// vertex deduplication) so building it needs no hashing, at the cost of a
// larger-than-necessary but perfectly valid glTF primitive.
static void hsd_emit_tri
	( hsd_vtx_t **tri, uint *n, uint *cap,
	  const hsd_vtx_t *v, uint a, uint b, uint c )
{
    if ( *n+3 > *cap )
    {
	*cap = *cap ? *cap*2 : 64;
	*tri = REALLOC(*tri,*cap*sizeof(**tri));
    }
    (*tri)[(*n)++] = v[a];
    (*tri)[(*n)++] = v[b];
    (*tri)[(*n)++] = v[c];
}

// GX primitive opcode -> triangle fan-out. kind: 0 QUADS, 2 TRIANGLES,
// 3 TRISTRIP, 4 TRIFAN (numbering local to this file, not a GX constant).
static void hsd_triangulate
	( int kind, const hsd_vtx_t *v, uint cnt,
	  hsd_vtx_t **tri, uint *n_tri, uint *cap_tri )
{
    switch (kind)
    {
	case 2: // TRIANGLES
	    for ( uint i = 0; i+3 <= cnt; i += 3 )
		hsd_emit_tri(tri,n_tri,cap_tri,v,i,i+1,i+2);
	    break;
	case 3: // TRISTRIP
	    for ( uint i = 0; i+3 <= cnt; i++ )
		if ( i & 1 ) hsd_emit_tri(tri,n_tri,cap_tri,v,i,i+2,i+1);
		else	     hsd_emit_tri(tri,n_tri,cap_tri,v,i,i+1,i+2);
	    break;
	case 4: // TRIFAN
	    for ( uint i = 1; i+2 <= cnt; i++ )
		hsd_emit_tri(tri,n_tri,cap_tri,v,0,i,i+1);
	    break;
	case 0: // QUADS
	    for ( uint i = 0; i+4 <= cnt; i += 4 )
	    {
		hsd_emit_tri(tri,n_tri,cap_tri,v,i,i+1,i+2);
		hsd_emit_tri(tri,n_tri,cap_tri,v,i,i+2,i+3);
	    }
	    break;
	default: break;
    }
}

// Walks one POBJ's raw display list, decoding every recognised draw call
// into flat triangle corners. Unrecognised opcodes (lines/points -- no
// faces to emit -- or anything else) stop decoding cleanly rather than
// desyncing the byte stream; whatever triangles were already decoded are
// still returned.
static bool hsd_decode_display_list
	( const hsd_t *hsd, const u8 *dl, uint dl_size,
	  const hsd_attr_t *attrs, uint n_attr,
	  hsd_vtx_t **out_tri, uint *out_n_tri )
{
    uint cap_tri = 0, n_tri = 0;
    hsd_vtx_t *tri = 0;
    const u8 *p = dl, *end = dl+dl_size;
    bool any = false;

    while ( p < end )
    {
	const u8 op = *p;
	if ( op == 0 ) { p++; continue; }
	if ( op == 0x61 ) { if (p+5>end) break; p += 5; continue; }
	if ( op == 0x08 ) { if (p+6>end) break; p += 6; continue; }
	if ( op == 0x10 )
	{
	    if ( p+5>end ) break;
	    const u64 adv = 5 + ((u64)be16(p+1)+1)*4;
	    if ( p+adv>end ) break;
	    p += adv;
	    continue;
	}

	int kind;
	switch ( op & 0xf8 )
	{
	    case 0x80: kind = 0; break; // QUADS
	    case 0x90: kind = 2; break; // TRIANGLES
	    case 0x98: kind = 3; break; // TRISTRIP
	    case 0xA0: kind = 4; break; // TRIFAN
	    default: goto done; // LINES/LINESTRIP/POINTS/QUADS2 or unknown: no more faces
	}

	if ( p+3 > end ) break;
	const uint cnt = be16(p+1);
	p += 3;
	if ( !cnt || cnt > 4096 )
	    break;

	hsd_vtx_t *verts = MALLOC(cnt*sizeof(*verts));
	bool ok = true;
	for ( uint i = 0; i < cnt && ok; i++ )
	    ok = hsd_fetch_vertex(hsd,attrs,n_attr,&p,end,verts+i);
	if (ok)
	{
	    hsd_triangulate(kind,verts,cnt,&tri,&n_tri,&cap_tri);
	    any = true;
	}
	FREE(verts);
	if (!ok)
	    break;
    }

 done:
    *out_tri = tri;
    *out_n_tri = n_tri;
    if ( !any )
	FREE(tri);
    return any;
}

//-----------------------------------------------------------------------------

typedef struct hsd_model_ctx_t
{
    const hsd_t	*hsd;
    joint_t	*joints;  uint n_joints, cap_joints;
    mesh_t	*meshes;  uint n_meshes, cap_meshes;
}
hsd_model_ctx_t;

static int hsd_add_joint ( hsd_model_ctx_t *ctx, ccp name, int parent_idx,
	float rx, float ry, float rz, float sx, float sy, float sz,
	float tx, float ty, float tz )
{
    if ( ctx->n_joints == ctx->cap_joints )
    {
	ctx->cap_joints = ctx->cap_joints ? ctx->cap_joints*2 : 16;
	ctx->joints = REALLOC(ctx->joints,ctx->cap_joints*sizeof(*ctx->joints));
    }
    joint_t *j = ctx->joints + ctx->n_joints;
    memset(j,0,sizeof(*j));
    StringCopyS(j->name,sizeof(j->name),name);
    j->parent_idx = parent_idx;
    // HSD stores radians; joint_t.rotate is degrees (see lib-model-dae.c).
    const double r2d = 180.0/M_PI;
    j->rotate  = (vec3_t){ (float)(rx*r2d), (float)(ry*r2d), (float)(rz*r2d) };
    j->scale   = (vec3_t){ sx, sy, sz };
    j->translate = (vec3_t){ tx, ty, tz };
    return (int)ctx->n_joints++;
}

// Decodes every POBJ of 'dobj_off's own DOBJ list (dobj->next chain) into
// one mesh_t bound to 'joint_idx'. Appends to ctx->meshes for each DOBJ that
// yields at least one triangle; DOBJs with no decodable POBJ are skipped.
static void hsd_build_dobj_meshes ( hsd_model_ctx_t *ctx, u32 dobj_off, int joint_idx, ccp jobj_name )
{
    const hsd_t *hsd = ctx->hsd;
    uint dobj_n = 0;

    while ( dobj_off && (u64)dobj_off+0x10 <= hsd->reloc_off && dobj_n < 4096 )
    {
	dobj_n++;
	const u32 next_dobj = get_ptr(hsd,dobj_off+0x04);
	u32 pobj_off = get_ptr(hsd,dobj_off+0x0C);

	hsd_vtx_t *all_tri = 0;
	uint n_all_tri = 0, cap_all_tri = 0;
	uint pobj_n = 0;

	while ( pobj_off && (u64)pobj_off+0x18 <= hsd->reloc_off && pobj_n < 4096 )
	{
	    pobj_n++;
	    const u32 next_pobj = get_ptr(hsd,pobj_off+0x04);
	    const u32 attrs_off = get_ptr(hsd,pobj_off+0x08);
	    const s16 dl_words  = (s16)be16(hsd->data+pobj_off+0x0E);
	    const u32 dl_off    = get_ptr(hsd,pobj_off+0x10);
	    const u32 dl_size   = dl_words > 0 ? (u32)dl_words*32 : 0;

	    if ( attrs_off && dl_off && dl_size && (u64)dl_off+dl_size <= hsd->reloc_off )
	    {
		hsd_attr_t attrs[HSD_MAX_ATTR];
		const uint n_attr = read_gx_attrs(hsd,attrs_off,attrs);

		hsd_vtx_t *tri = 0; uint n_tri = 0;
		if ( n_attr && hsd_decode_display_list(hsd,hsd->data+dl_off,dl_size,attrs,n_attr,&tri,&n_tri) )
		{
		    if ( n_all_tri+n_tri > cap_all_tri )
		    {
			cap_all_tri = cap_all_tri ? cap_all_tri*2 : 64;
			if ( cap_all_tri < n_all_tri+n_tri ) cap_all_tri = n_all_tri+n_tri;
			all_tri = REALLOC(all_tri,cap_all_tri*sizeof(*all_tri));
		    }
		    memcpy(all_tri+n_all_tri,tri,n_tri*sizeof(*tri));
		    n_all_tri += n_tri;
		    FREE(tri);
		}
	    }

	    pobj_off = next_pobj;
	}

	if ( n_all_tri )
	{
	    if ( ctx->n_meshes == ctx->cap_meshes )
	    {
		ctx->cap_meshes = ctx->cap_meshes ? ctx->cap_meshes*2 : 16;
		ctx->meshes = REALLOC(ctx->meshes,ctx->cap_meshes*sizeof(*ctx->meshes));
	    }
	    mesh_t *m = ctx->meshes + ctx->n_meshes++;
	    memset(m,0,sizeof(*m));
	    snprintf(m->name,sizeof(m->name),"%s_dobj%u",jobj_name,dobj_n-1);

	    m->num_positions = m->num_vertices = n_all_tri;
	    m->positions = MALLOC(n_all_tri*sizeof(*m->positions));
	    m->position_node = MALLOC(n_all_tri*sizeof(*m->position_node));
	    m->vertices = CALLOC(n_all_tri,sizeof(*m->vertices));

	    bool any_nrm=false, any_uv=false, any_clr=false;
	    for ( uint i = 0; i < n_all_tri; i++ )
	    {
		any_nrm |= all_tri[i].has_nrm;
		any_uv  |= all_tri[i].has_uv;
		any_clr |= all_tri[i].has_clr;
	    }
	    if (any_nrm) { m->num_normals = n_all_tri; m->normals = MALLOC(n_all_tri*sizeof(*m->normals)); }
	    if (any_uv)  { m->num_texcoords = n_all_tri; m->texcoords = MALLOC(n_all_tri*sizeof(*m->texcoords)); }
	    if (any_clr) { m->num_colors[0] = n_all_tri; m->colors[0] = MALLOC(n_all_tri*sizeof(*m->colors[0])); }

	    for ( uint i = 0; i < n_all_tri; i++ )
	    {
		const hsd_vtx_t *v = all_tri+i;
		m->positions[i] = (vec3_t){ v->pos[0], v->pos[1], v->pos[2] };
		m->position_node[i] = joint_idx;
		m->vertices[i].position_idx = (int)i;
		m->vertices[i].normal_idx = any_nrm ? (int)i : -1;
		m->vertices[i].texcoord_idx = any_uv ? (int)i : -1;
		m->vertices[i].matrix_idx = -1;
		m->vertices[i].color_idx[0] = any_clr ? (int)i : -1;
		m->vertices[i].color_idx[1] = -1;
		for ( int k = 0; k < 7; k++ ) m->vertices[i].extra_texcoord_idx[k] = -1;
		if (any_nrm) m->normals[i] = (vec3_t){ v->nrm[0], v->nrm[1], v->nrm[2] };
		if (any_uv)  m->texcoords[i] = (vec2_t){ v->uv[0], v->uv[1] };
		if (any_clr) m->colors[0][i] = (color4_t){ v->clr[0], v->clr[1], v->clr[2], v->clr[3] };
	    }
	    m->material_idx = -1;
	}
	FREE(all_tri);

	dobj_off = next_dobj;
    }
}

// Recursively walks the child/next-sibling JOBJ tree starting at 'off',
// appending one joint_t per JOBJ (parented to 'parent_idx') and one mesh_t
// per non-empty DOBJ found along the way.
static void hsd_walk_jobj ( hsd_model_ctx_t *ctx, u32 off, int parent_idx, uint depth )
{
    const hsd_t *hsd = ctx->hsd;
    if ( depth > 64 || !off || (u64)off+0x40 > hsd->reloc_off )
	return;

    const u8 *s = hsd->data+off;
    const float rx=bef32(s+0x14), ry=bef32(s+0x18), rz=bef32(s+0x1C);
    const float sx=bef32(s+0x20), sy=bef32(s+0x24), sz=bef32(s+0x28);
    const float tx=bef32(s+0x2C), ty=bef32(s+0x30), tz=bef32(s+0x34);
    // A real JOBJ's TRS floats are always exact zeroes/ordinary values, never
    // subnormals -- a root table entry that resolves to something other than
    // a JOBJ (e.g. Melee's per-fighter data root, reached indirectly rather
    // than listed as its own root -- not handled here, see lib-hsd.h) reads
    // back as small non-zero denormals here instead, which plain isfinite()
    // doesn't catch. Reject those rather than build a garbage skeleton node
    // from them.
    if ( !isfinite(rx)||!isfinite(ry)||!isfinite(rz)
      || !isfinite(sx)||!isfinite(sy)||!isfinite(sz)
      || !isfinite(tx)||!isfinite(ty)||!isfinite(tz)
      || (rx&&fpclassify(rx)==FP_SUBNORMAL) || (ry&&fpclassify(ry)==FP_SUBNORMAL) || (rz&&fpclassify(rz)==FP_SUBNORMAL)
      || (sx&&fpclassify(sx)==FP_SUBNORMAL) || (sy&&fpclassify(sy)==FP_SUBNORMAL) || (sz&&fpclassify(sz)==FP_SUBNORMAL)
      || (tx&&fpclassify(tx)==FP_SUBNORMAL) || (ty&&fpclassify(ty)==FP_SUBNORMAL) || (tz&&fpclassify(tz)==FP_SUBNORMAL) )
	return;

    char name[64];
    snprintf(name,sizeof(name),"jobj_%u",off);
    const int idx = hsd_add_joint(ctx,name,parent_idx,rx,ry,rz,sx,sy,sz,tx,ty,tz);

    const u32 dobj_off = get_ptr(hsd,off+0x10);
    if (dobj_off)
	hsd_build_dobj_meshes(ctx,dobj_off,idx,name);

    const u32 child_off = get_ptr(hsd,off+0x08);
    const u32 next_off  = get_ptr(hsd,off+0x0C);

    if (child_off)
	hsd_walk_jobj(ctx,child_off,idx,depth+1);
    if (next_off)
	hsd_walk_jobj(ctx,next_off,parent_idx,depth+1);
}

//-----------------------------------------------------------------------------

int ExportHSDModel ( const hsd_t *hsd, ccp out_glb_file )
{
    if ( !hsd || !hsd->data || !out_glb_file )
	return -1;

    // Root table: (offset,name-offset) pairs right after the relocation
    // table, offsets 0x20-relative like everywhere else (verified: root[0]
    // of TyBox.dat resolves to "ToyBoxModel_TopN_joint", a real JOBJ).
    const u32 root_table = hsd->reloc_off + 4*hsd->n_reloc;

    hsd_model_ctx_t ctx = { .hsd = hsd };

    for ( uint i = 0; i < hsd->n_root; i++ )
    {
	if ( (u64)root_table+8*(i+1) > hsd->size )
	    break;
	const s32 root_off_raw = (s32)be32(hsd->data+root_table+8*i);
	const u32 root_off = (u32)root_off_raw + HSD_DATA_BASE;
	if ( root_off < HSD_DATA_BASE || root_off >= hsd->reloc_off )
	    continue;
	hsd_walk_jobj(&ctx,root_off,-1,0);
    }

    int written = -1;
    if ( ctx.n_meshes )
    {
	model_t model; memset(&model,0,sizeof(model));
	model.meshes = ctx.meshes;
	model.num_meshes = ctx.n_meshes;
	model.joints = ctx.joints;
	model.num_joints = ctx.n_joints;
	ComputeModelTRSBinds(&model);

	const uint path_len = strlen(out_glb_file);
	const bool is_dae = path_len > 4 && !strcasecmp(out_glb_file+path_len-4,".dae");
	written = ( is_dae ? ExportModelToDAE(&model,out_glb_file)
			    : ExportModelToGLB(&model,out_glb_file) ) == 0
	    ? (int)ctx.n_meshes : -1;
    }

    for ( uint i = 0; i < ctx.n_meshes; i++ )
    {
	mesh_t *m = ctx.meshes+i;
	FREE(m->positions); FREE(m->normals); FREE(m->texcoords);
	FREE(m->colors[0]); FREE(m->colors[1]);
	FREE(m->position_node); FREE(m->vertices); FREE(m->triangle_materials);
    }
    FREE(ctx.meshes);
    FREE(ctx.joints);
    return written;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHSDModelFromData ( const u8 *data, uint size, ccp out_glb_file )
{
    hsd_t hsd;
    if (!ScanHSD(&hsd,data,size))
	return -1;
    const int stat = ExportHSDModel(&hsd,out_glb_file);
    ResetHSD(&hsd);
    return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
