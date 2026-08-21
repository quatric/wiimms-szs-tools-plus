
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

#include <stdlib.h>
#include <string.h>

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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
