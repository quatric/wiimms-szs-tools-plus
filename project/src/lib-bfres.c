// BFRES ("FRES") -- Nintendo's NintendoWare resource container.
//
// This handles the Wii U flavour (version 3.x, big endian), whose offsets
// are all *self-relative*: the stored value is added to the address of the
// field holding it. Geometry lives in FMDL -> FVTX (vertex buffers, laid out
// as interleaved GX2 attributes) and FSHP (shapes, each with a LOD carrying
// an index buffer).
//
// The Switch flavour reuses the "FRES" magic but is little endian with a
// completely different layout and keeps its textures in a separate BNTX; it
// is detected and rejected here rather than misparsed.

#include "lib-bfres.h"
#include "lib-brres-model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned int uint;

static uint16_t rb16 ( const uint8_t *p ) { return (uint16_t)p[0]<<8 | p[1]; }
static uint32_t rb32 ( const uint8_t *p )
    { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static int32_t  rbs32 ( const uint8_t *p ) { return (int32_t)rb32(p); }

// Half-precision float, as used by the 16-bit vertex attribute formats.
static float half_to_float ( uint16_t h )
{
    const int sign = (h >> 15) & 1;
    int exp = (h >> 10) & 0x1F;
    int man = h & 0x3FF;
    float v;
    if (!exp)
	v = man ? (float)man / 16384.0f / 64.0f : 0.0f; // subnormal
    else if ( exp == 31 )
	v = man ? 0.0f : 1e30f;                          // NaN/Inf -> tame value
    else
    {
	float m = 1.0f + (float)man / 1024.0f;
	int e = exp - 15;
	v = m;
	while ( e > 0 ) { v *= 2.0f; e--; }
	while ( e < 0 ) { v /= 2.0f; e++; }
    }
    return sign ? -v : v;
}

// Resolves a self-relative offset stored at ADDR.
#define REL(base,addr) ( (size_t)(addr) + (size_t)rbs32((base)+(addr)) )

static const char *rel_string ( const uint8_t *d, size_t size, size_t at )
{
    if ( at + 4 > size ) return NULL;
    const size_t p = REL(d,at);
    if ( p >= size ) return NULL;
    for ( size_t q = p; q < size; q++ )
	if (!d[q]) return (const char*)(d+p);
    return NULL;
}

//-----------------------------------------------------------------------------
// GX2 vertex attribute formats. Only the ones that actually carry geometry
// are handled; anything else leaves the component at zero.
//-----------------------------------------------------------------------------

static int attr_read ( const uint8_t *p, size_t avail, uint32_t fmt, float out[4] )
{
    out[0]=out[1]=out[2]=0.0f; out[3]=1.0f;
    switch (fmt)
    {
	case 0x00000007: // 16_16 unorm
	    if ( avail < 4 ) return 0;
	    out[0] = rb16(p)/65535.0f; out[1] = rb16(p+2)/65535.0f;
	    return 1;
	case 0x00000207: // 16_16 snorm
	    if ( avail < 4 ) return 0;
	    out[0] = (int16_t)rb16(p)/32767.0f; out[1] = (int16_t)rb16(p+2)/32767.0f;
	    return 1;
	case 0x0000000A: // 8_8_8_8 unorm
	    if ( avail < 4 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = p[i]/255.0f;
	    return 1;
	case 0x0000020A: // 8_8_8_8 snorm
	    if ( avail < 4 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = (int8_t)p[i]/127.0f;
	    return 1;
	case 0x0000020B: // 10_10_10_2 snorm
	{
	    if ( avail < 4 ) return 0;
	    const uint32_t v = rb32(p);
	    for ( int i = 0; i < 3; i++ )
	    {
		int c = (v >> (i*10)) & 0x3FF;
		if ( c & 0x200 ) c -= 0x400;
		out[i] = (float)c/511.0f;
	    }
	    return 1;
	}
	case 0x0000080D: // 16_16 float
	    if ( avail < 4 ) return 0;
	    out[0] = half_to_float(rb16(p)); out[1] = half_to_float(rb16(p+2));
	    return 1;
	case 0x0000080F: // 16_16_16_16 float
	    if ( avail < 8 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = half_to_float(rb16(p+i*2));
	    return 1;
	case 0x00000806: // 32_32 float
	{
	    if ( avail < 8 ) return 0;
	    for ( int i = 0; i < 2; i++ )
		{ union { uint32_t u; float f; } c; c.u = rb32(p+i*4); out[i] = c.f; }
	    return 1;
	}
	case 0x00000811: // 32_32_32 float
	{
	    if ( avail < 12 ) return 0;
	    for ( int i = 0; i < 3; i++ )
		{ union { uint32_t u; float f; } c; c.u = rb32(p+i*4); out[i] = c.f; }
	    return 1;
	}
	case 0x00000813: // 32_32_32_32 float
	{
	    if ( avail < 16 ) return 0;
	    for ( int i = 0; i < 4; i++ )
		{ union { uint32_t u; float f; } c; c.u = rb32(p+i*4); out[i] = c.f; }
	    return 1;
	}
    }
    return 0;
}

//-----------------------------------------------------------------------------

typedef struct
{
    const uint8_t *pos, *nrm, *uv;   // pointers to the first vertex's data
    uint32_t fmt_pos, fmt_nrm, fmt_uv;
    uint stride_pos, stride_nrm, stride_uv;
    size_t avail_pos, avail_nrm, avail_uv;
    uint count;
}
fvtx_t;

// Reads one FVTX and locates its position/normal/uv attribute streams.
static int read_fvtx ( const uint8_t *d, size_t size, size_t fv, fvtx_t *out )
{
    if ( fv + 0x20 > size || memcmp(d+fv,"FVTX",4) )
	return 0;
    memset(out,0,sizeof(*out));

    const uint n_attr = d[fv+4], n_buf = d[fv+5];
    out->count = rb32(d+fv+8);
    if ( !out->count || !n_attr || !n_buf )
	return 0;

    const size_t attrs = REL(d,fv+0x10);
    const size_t bufs  = REL(d,fv+0x18);
    if ( attrs + (size_t)n_attr*12 > size || bufs + (size_t)n_buf*0x18 > size )
	return 0;

    for ( uint i = 0; i < n_attr; i++ )
    {
	const size_t a = attrs + i*12;
	const char *name = rel_string(d,size,a);
	if (!name) continue;
	const uint bi = d[a+4];
	const uint boff = rb16(d+a+6);
	const uint32_t fmt = rb32(d+a+8);
	if ( bi >= n_buf ) continue;

	const size_t b = bufs + (size_t)bi*0x18;
	const uint32_t bsize = rb32(d+b+4);
	const uint stride = rb16(d+b+0x0c);
	const size_t data = REL(d,b+0x14);
	if ( !stride || data >= size || data + bsize > size )
	    continue;
	if ( boff >= stride )
	    continue;

	const uint8_t *p = d + data + boff;
	const size_t avail = size - (data + boff);
	// Attribute names follow the "_p0"/"_n0"/"_u0" convention.
	if ( !strncmp(name,"_p",2) && !out->pos )
	    { out->pos=p; out->fmt_pos=fmt; out->stride_pos=stride; out->avail_pos=avail; }
	else if ( !strncmp(name,"_n",2) && !out->nrm )
	    { out->nrm=p; out->fmt_nrm=fmt; out->stride_nrm=stride; out->avail_nrm=avail; }
	else if ( !strncmp(name,"_u",2) && !out->uv )
	    { out->uv=p; out->fmt_uv=fmt; out->stride_uv=stride; out->avail_uv=avail; }
    }
    return out->pos != NULL;
}

model_t* ParseBFRES ( const uint8_t *data, size_t size )
{
    if ( !data || size < 0x60 || memcmp(data,"FRES",4) )
	return NULL;

    // Wii U BFRES is big endian and version 3.x; Switch BFRES reuses the
    // magic with a different layout entirely.
    if ( rb16(data+8) != 0xFEFF )
	return NULL;
    if ( data[4] != 3 )
	return NULL;

    const uint8_t *d = data;

    // Index group 0 is FMDL.
    const uint16_t n_fmdl = rb16(d+0x50);
    if (!n_fmdl)
	return NULL;
    const size_t grp = REL(d,0x20);
    if ( grp + 8 > size )
	return NULL;
    const uint32_t entries = rb32(d+grp+4);
    if ( !entries || entries > 0x10000 || grp + 8 + (size_t)(entries+1)*16 > size )
	return NULL;

    // First model only: DAE has no multi-model concept here.
    const size_t e = grp + 8 + 16;
    const size_t m = REL(d,e+12);
    if ( m + 0x30 > size || memcmp(d+m,"FMDL",4) )
	return NULL;

    const uint16_t n_fvtx = rb16(d+m+0x20);
    const uint16_t n_fshp = rb16(d+m+0x22);
    if ( !n_fvtx || !n_fshp )
	return NULL;

    const size_t fvtx_arr = REL(d,m+0x10);
    const size_t fshp_grp = REL(d,m+0x14);
    if ( fshp_grp + 8 > size )
	return NULL;

    model_t *out = calloc(1,sizeof(model_t));
    if (!out) return NULL;
    out->meshes = calloc(n_fshp,sizeof(mesh_t));
    if (!out->meshes) { free(out); return NULL; }

    // FMAT materials (FMDL+0x18 index group; header layout verified against
    // mk8.tockdom.com's FMDL doc page byte-for-byte, plus KillzXGaming/
    // BfresLibrary's TextureRef.cs for the texture-ref array's [nameOffset,
    // ftexOffset] pair -- only the first texture ref per material is bound
    // (diffuse-slot heuristic; real files commonly have several ref'd
    // textures -- e.g. normal/specular -- that this fork's DAE export has
    // no material-model slot for yet). Texture *names* only, not pixel data
    // -- the actual FTEX decode-to-PNG happens in wszst.c's extraction
    // pass, so this only needs to match the names those PNGs get written
    // under (see extract_bfres_textures() in wszst.c).
    const uint16_t n_fmat = rb16(d+m+0x24);
    const size_t fmat_grp = REL(d,m+0x18);
    if ( n_fmat && fmat_grp+8 <= size )
    {
	out->materials = calloc(n_fmat,sizeof(material_t));
	if (out->materials)
	{
	    const uint32_t mat_entries = rb32(d+fmat_grp+4);
	    for ( uint32_t i = 0; i < mat_entries && i < n_fmat; i++ )
	    {
		const size_t me = fmat_grp + 8 + (size_t)(i+1)*16;
		if ( me+16 > size ) break;
		const size_t fm = REL(d,me+12);
		if ( fm+0x4C > size || memcmp(d+fm,"FMAT",4) ) continue;

		material_t *mat = out->materials + out->num_materials++;
		const char *mname = rel_string(d,size,fm+4);
		snprintf(mat->name,sizeof(mat->name),"%s",
		    mname && *mname ? mname : "material");

		const uint8_t n_texref = d[fm+0x11];
		if (n_texref)
		{
		    const size_t texrefs = REL(d,fm+0x28);
		    // TextureRef: 8 bytes, [nameOffset:4][ftexOffset:4].
		    if ( texrefs+8 <= size )
		    {
			const char *tname = rel_string(d,size,texrefs);
			if (tname)
			{
			    snprintf(mat->textures[0],sizeof(mat->textures[0]),
				"%s",tname);
			    mat->texture_coord[0] = 0; // uv0
			    mat->num_textures = 1;
			}
		    }
		}
	    }
	}
    }

    const uint32_t sh_entries = rb32(d+fshp_grp+4);
    for ( uint32_t i = 0; i < sh_entries && i < n_fshp; i++ )
    {
	const size_t se = fshp_grp + 8 + (size_t)(i+1)*16;
	if ( se + 16 > size ) break;
	const size_t sh = REL(d,se+12);
	if ( sh + 0x30 > size || memcmp(d+sh,"FSHP",4) ) continue;

	const char *name = rel_string(d,size,sh+4);
	const uint16_t vtx_index = rb16(d+sh+0x12);
	if ( vtx_index >= n_fvtx ) continue;

	// Each FVTX is 0x20 bytes of header in the array.
	fvtx_t fvtx;
	if ( !read_fvtx(d,size,fvtx_arr + (size_t)vtx_index*0x20,&fvtx) )
	    continue;

	// LOD model: primitive type, index format, count, then the buffer.
	const size_t lod = REL(d,sh+0x24);
	if ( lod + 0x18 > size ) continue;
	const uint32_t prim = rb32(d+lod);
	const uint32_t ifmt = rb32(d+lod+4);
	const uint32_t icount = rb32(d+lod+8);
	if ( prim != 4 || !icount || icount > 0x1000000 ) continue; // triangles only

	const size_t ibo = REL(d,lod+0x14);
	if ( ibo + 0x18 > size ) continue;
	const size_t idata = REL(d,ibo+0x14);
	// Index format 4 is 16-bit, 9 is 32-bit.
	const uint isz = ifmt == 9 ? 4 : 2;
	if ( idata + (size_t)icount*isz > size ) continue;

	mesh_t *mesh = out->meshes + out->num_meshes;
	snprintf(mesh->name,sizeof(mesh->name),"%s",
		name && *name ? name : "shape");
	const uint16_t fmat_idx = rb16(d+sh+0x0E); // FSHP+0x0E: FMAT index
	mesh->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

	mesh->positions = calloc(icount,sizeof(vec3_t));
	mesh->normals   = calloc(icount,sizeof(vec3_t));
	mesh->texcoords = calloc(icount,sizeof(vec2_t));
	mesh->vertices  = calloc(icount,sizeof(vertex_t));
	if ( !mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices )
	{
	    free(mesh->positions); free(mesh->normals);
	    free(mesh->texcoords); free(mesh->vertices);
	    memset(mesh,0,sizeof(*mesh));
	    continue;
	}

	uint n = 0;
	for ( uint32_t k = 0; k < icount; k++ )
	{
	    const uint8_t *ip = d + idata + (size_t)k*isz;
	    const uint32_t vi = isz == 4 ? rb32(ip) : rb16(ip);
	    if ( vi >= fvtx.count ) continue;

	    float v[4];
	    if ( attr_read(fvtx.pos + (size_t)vi*fvtx.stride_pos,
			   fvtx.avail_pos - (size_t)vi*fvtx.stride_pos,
			   fvtx.fmt_pos,v) )
		{ mesh->positions[n].x=v[0]; mesh->positions[n].y=v[1]; mesh->positions[n].z=v[2]; }
	    if ( fvtx.nrm && attr_read(fvtx.nrm + (size_t)vi*fvtx.stride_nrm,
			   fvtx.avail_nrm - (size_t)vi*fvtx.stride_nrm,
			   fvtx.fmt_nrm,v) )
		{ mesh->normals[n].x=v[0]; mesh->normals[n].y=v[1]; mesh->normals[n].z=v[2]; }
	    if ( fvtx.uv && attr_read(fvtx.uv + (size_t)vi*fvtx.stride_uv,
			   fvtx.avail_uv - (size_t)vi*fvtx.stride_uv,
			   fvtx.fmt_uv,v) )
		{ mesh->texcoords[n].u=v[0]; mesh->texcoords[n].v=v[1]; }

	    mesh->vertices[n].position_idx = (int)n;
	    mesh->vertices[n].normal_idx   = fvtx.nrm ? (int)n : -1;
	    mesh->vertices[n].texcoord_idx = fvtx.uv  ? (int)n : -1;
	    n++;
	}
	if (!n)
	{
	    free(mesh->positions); free(mesh->normals);
	    free(mesh->texcoords); free(mesh->vertices);
	    memset(mesh,0,sizeof(*mesh));
	    continue;
	}
	mesh->num_positions = mesh->num_normals = mesh->num_texcoords = n;
	mesh->num_vertices = n;
	out->num_meshes++;
    }

    if (!out->num_meshes)
    {
	FreeModel(out);
	return NULL;
    }
    return out;
}

//-----------------------------------------------------------------------------
// Switch BFRES ("FRES", little endian, version-major-gated header layout).
//
// Header field offsets (name-offset, model-array, material-array, per-
// FMDL/FSHP/FVTX/FMAT prologue width) are ported from KillzXGaming's
// BfresLibrary (MIT) -- ResFileParser.Load()/ModelParser.Read()/
// ShapeParser.Read()/VertexBufferParser.Load()/Mesh.Load(), mirroring the
// already-verified name-resolution code in wszst.c's
// extract_bfres_switch_manifest(). What that code did NOT solve -- the
// vertex/index *data* location -- is solved here:
//
// - ResFileParser.Load()'s exact field sequence (hand-counted byte-by-byte
//   against the C# source, not guessed) places the `BufferInfo` pointer at
//   absolute header offset 0x90 (144) for version-major<9/<10 files (every
//   real sample seen so far). Cross-checked against the already-verified
//   `numModel` field position (0xBC for v8): counting forward from 0x90
//   through ExternalFiles/padding/StringTable/StringPoolSize lands exactly
//   on 0xBC, confirming the byte count is right, not just plausible.
// - BufferInfo's own struct is `u32 unk, u32 Size, s64 BufferOffset, u8[16]
//   padding` -- verified on a real file (AirBubble.bfres, Super Mario
//   Odyssey): `unk` read back exactly 34, BfresLibrary's own hardcoded
//   default for that field, and the bytes at `BufferOffset` decode as a
//   clean u16 triangle index list (0,1,2, 3,0,2, 0,4,1, ...).
// - Index and vertex buffer data all live in ONE pool starting at
//   `BufferOffset`: the index buffer for each Mesh at
//   `BufferOffset + FaceBufferOffset` (a local s32 in the Mesh struct), and
//   each FVTX's buffers (one buffer per attribute is common -- FVTX
//   attributes are NOT necessarily interleaved on Switch, unlike Wii U)
//   starting at `BufferOffset + (the FVTX's own local s32 offset)`,
//   8-byte-aligned, one after another, size/stride given by separate
//   per-buffer arrays. Verified end to end on the same real file: index
//   buffer (1560 * 2 bytes = 3120, matching its separately-stored Size
//   field exactly) is immediately followed with zero padding by vertex
//   buffer 0 (275 vertices * stride 12 = 3300 bytes, again matching its
//   separately-stored Size field exactly).
// - Per-attribute Format (and Mesh's PrimitiveType/IndexFormat) are stored
//   as their raw enum value but the reader temporarily swaps to
//   BIG-endian just for that one field (see VertexAttrib.Load() setting
//   `loader.ByteOrder = ByteOrder.BigEndian` around the Format read) even
//   though the rest of the file is little-endian -- confirmed by matching
//   real attribute bytes (0x05,0x18 -> big-endian 0x0518) against
//   BfresLibrary's own `SwitchAttribFormat` enum (0x0518 =
//   Format_32_32_32_Single, exactly a 3-float position attribute) rather
//   than assuming a plain little-endian read (which would give a
//   nonexistent format code).
//-----------------------------------------------------------------------------

static uint16_t le16 ( const uint8_t *p ) { return (uint16_t)p[1]<<8 | p[0]; }
static uint32_t le32 ( const uint8_t *p )
    { return (uint32_t)p[3]<<24 | (uint32_t)p[2]<<16 | (uint32_t)p[1]<<8 | p[0]; }
static int32_t  les32 ( const uint8_t *p ) { return (int32_t)le32(p); }
static uint64_t le64 ( const uint8_t *p )
    { return (uint64_t)le32(p+4)<<32 | le32(p); }
static int64_t  les64 ( const uint8_t *p ) { return (int64_t)le64(p); }

// A handful of enum values are stored byte-order-swapped relative to the
// rest of the (little-endian) file -- see the comment above. Only the low
// 16 bits are ever non-zero on any real sample seen, so this just swaps
// the first two bytes rather than fully byte-reversing a 32-bit read.
static inline uint32_t swz16 ( const uint8_t *p ) { return (uint32_t)p[0]<<8 | p[1]; }

static int attr_read_switch ( const uint8_t *p, size_t avail, uint32_t fmt, float out[4] )
{
    out[0]=out[1]=out[2]=0.0f; out[3]=1.0f;
    switch (fmt)
    {
	case 0x0112: // 16_16 unorm
	    if ( avail < 4 ) return 0;
	    out[0] = le16(p)/65535.0f; out[1] = le16(p+2)/65535.0f;
	    return 1;
	case 0x0212: // 16_16 snorm
	    if ( avail < 4 ) return 0;
	    out[0] = (int16_t)le16(p)/32767.0f; out[1] = (int16_t)le16(p+2)/32767.0f;
	    return 1;
	case 0x010b: // 8_8_8_8 unorm
	    if ( avail < 4 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = p[i]/255.0f;
	    return 1;
	case 0x020b: // 8_8_8_8 snorm
	    if ( avail < 4 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = (int8_t)p[i]/127.0f;
	    return 1;
	case 0x020e: // 10_10_10_2 snorm (verified: real normal attribute)
	{
	    if ( avail < 4 ) return 0;
	    const uint32_t v = le32(p);
	    for ( int i = 0; i < 3; i++ )
	    {
		int c = (v >> (i*10)) & 0x3FF;
		if ( c & 0x200 ) c -= 0x400;
		out[i] = (float)c/511.0f;
	    }
	    return 1;
	}
	case 0x050a: // 16 float (single half, e.g. some scalar attribs)
	    if ( avail < 2 ) return 0;
	    out[0] = half_to_float(le16(p));
	    return 1;
	case 0x0512: // 16_16 float (verified: real uv attribute)
	    if ( avail < 4 ) return 0;
	    out[0] = half_to_float(le16(p)); out[1] = half_to_float(le16(p+2));
	    return 1;
	case 0x0515: // 16_16_16_16 float
	    if ( avail < 8 ) return 0;
	    for ( int i = 0; i < 4; i++ ) out[i] = half_to_float(le16(p+i*2));
	    return 1;
	case 0x0516: // 32 float
	{
	    if ( avail < 4 ) return 0;
	    union { uint32_t u; float f; } c; c.u = le32(p); out[0] = c.f;
	    return 1;
	}
	case 0x0517: // 32_32 float
	{
	    if ( avail < 8 ) return 0;
	    for ( int i = 0; i < 2; i++ )
		{ union { uint32_t u; float f; } c; c.u = le32(p+i*4); out[i] = c.f; }
	    return 1;
	}
	case 0x0518: // 32_32_32 float (verified: real position attribute)
	{
	    if ( avail < 12 ) return 0;
	    for ( int i = 0; i < 3; i++ )
		{ union { uint32_t u; float f; } c; c.u = le32(p+i*4); out[i] = c.f; }
	    return 1;
	}
	case 0x0519: // 32_32_32_32 float
	{
	    if ( avail < 16 ) return 0;
	    for ( int i = 0; i < 4; i++ )
		{ union { uint32_t u; float f; } c; c.u = le32(p+i*4); out[i] = c.f; }
	    return 1;
	}
    }
    return 0;
}

// Bytes consumed by the version-gated prologue before the first LoadString()
// in FMDL/FSHP/FMAT/FVTX sections -- same convention documented in wszst.c's
// bfres_switch_hdr_extra(), duplicated here to keep this file's Switch path
// self-contained.
static inline uint bfres_switch_hdr_extra ( uint vmajor )
{
    return vmajor >= 9 ? 4 : 12;
}

static const char *rel_string_switch ( const uint8_t *d, size_t size, int64_t off )
{
    if ( off < 2 || (size_t)off + 2 > size ) return NULL;
    const uint len = le16(d+off);
    if ( (size_t)off + 2 + len > size ) return NULL;
    return (const char*)(d+off+2);
}

typedef struct
{
    const uint8_t *pos, *nrm, *uv;
    uint32_t fmt_pos, fmt_nrm, fmt_uv;
    uint stride_pos, stride_nrm, stride_uv;
    size_t avail_pos, avail_nrm, avail_uv;
    uint count;
}
fvtx_switch_t;

// Reads one FVTX (Switch): attribute list + one buffer per attribute
// (commonly non-interleaved, unlike Wii U), located via the shared
// BufferInfo pool base plus this FVTX's own local buffer offset.
static int read_fvtx_switch ( const uint8_t *d, size_t size, size_t fv,
    uint vmajor, int64_t pool_base, fvtx_switch_t *out )
{
    memset(out,0,sizeof(*out));
    if ( fv + 0x60 > size || memcmp(d+fv,"FVTX",4) ) return 0;

    const uint vhdr = bfres_switch_hdr_extra(vmajor);
    const int64_t attr_arr = les64(d+fv+4+vhdr);
    const int64_t counts_off = fv + 4 + vhdr + 0x40;
    if ( (size_t)counts_off+16 > size ) return 0;

    const int32_t vb_local_off  = les32(d+counts_off);
    const uint n_attr           = d[counts_off+4];
    const uint n_buf            = d[counts_off+5];
    // VertexBufferSizeOffset then VertexStrideSizeOffset are the two
    // ReadOffset() calls right before an 8-byte padding field that ends
    // exactly at counts_off (VertexBufferParser.Load()) -- so counting
    // backward from counts_off: padding(8) at counts_off-8,
    // VertexStrideSizeOffset(8) at counts_off-16, VertexBufferSizeOffset(8)
    // at counts_off-24. Verified against a real file (AirBubble.bfres):
    // these land on 2432/2480 respectively, and the values they point to
    // (stride 12/4/4, size 3300/1100/1100) match the real vertex count
    // (275) and attribute formats exactly, zero slack.
    const int64_t vtx_bufsize_off = les64(d+counts_off-24);
    const int64_t vtx_stride_off2 = les64(d+counts_off-16);
    const uint32_t vertex_count = (size_t)counts_off+12 <= size ? le32(d+counts_off+8) : 0;

    if ( !n_attr || !n_buf || n_buf > 8 || !vertex_count )
	return 0;
    if ( attr_arr <= 0 || (size_t)attr_arr + (size_t)n_attr*16 > size )
	return 0;
    if ( vtx_bufsize_off <= 0 || vtx_stride_off2 <= 0
	|| (size_t)vtx_bufsize_off + (size_t)n_buf*16 > size
	|| (size_t)vtx_stride_off2 + (size_t)n_buf*16 > size )
	return 0;

    // Walk the buffer pool sequentially, 8-byte aligned, same order the
    // buffers are declared in (verified: on a real file this lands with
    // zero gap directly after the preceding index buffer).
    uint64_t bufpos[8];
    uint64_t cur = (uint64_t)pool_base + (uint32_t)vb_local_off;
    for ( uint i = 0; i < n_buf; i++ )
    {
	cur = (cur + 7) & ~(uint64_t)7;
	bufpos[i] = cur;
	const uint32_t bsize = le32(d+vtx_bufsize_off+(size_t)i*16);
	cur += bsize;
    }

    out->count = vertex_count;
    for ( uint i = 0; i < n_attr; i++ )
    {
	const size_t a = (size_t)attr_arr + (size_t)i*16;
	const char *name = rel_string_switch(d,size,les64(d+a));
	if (!name) continue;
	const uint32_t fmt = swz16(d+a+8);
	const uint boff = le16(d+a+12);
	const uint bi = le16(d+a+14);
	if ( bi >= n_buf ) continue;

	const uint stride = le32(d+vtx_stride_off2+(size_t)bi*16);
	if ( !stride || bufpos[bi] >= size ) continue;
	const size_t data = (size_t)bufpos[bi];
	if ( boff >= stride || data+boff >= size ) continue;

	const uint8_t *p = d + data + boff;
	const size_t avail = size - (data + boff);
	if ( !strncmp(name,"_p",2) && !out->pos )
	    { out->pos=p; out->fmt_pos=fmt; out->stride_pos=stride; out->avail_pos=avail; }
	else if ( !strncmp(name,"_n",2) && !out->nrm )
	    { out->nrm=p; out->fmt_nrm=fmt; out->stride_nrm=stride; out->avail_nrm=avail; }
	else if ( !strncmp(name,"_u",2) && !out->uv )
	    { out->uv=p; out->fmt_uv=fmt; out->stride_uv=stride; out->avail_uv=avail; }
    }
    return out->pos != NULL;
}

model_t* ParseBFRESSwitch ( const uint8_t *data, size_t size )
{
    if ( !data || size < 0x100 || memcmp(data,"FRES",4) ) return NULL;
    if ( le16(data+0x0C) != 0xFEFF ) return NULL; // Switch BOM position/endianness

    const uint32_t version = le32(data+8);
    const uint vmajor = (version >> 16) & 0xFFFF;
    const uint8_t *d = data;

    const int64_t fmdl_arr = les64(d+0x28);
    if ( fmdl_arr <= 0 || (size_t)fmdl_arr+0x60 > size || memcmp(d+fmdl_arr,"FMDL",4) )
	return NULL;

    // BufferInfo pointer: see the long comment above this section for the
    // byte-by-byte derivation of offset 0x90 (verified against real data,
    // not assumed).
    if ( size < 0x90+8 ) return NULL;
    const int64_t bufinfo = les64(d+0x90);
    if ( bufinfo <= 0 || (size_t)bufinfo+16 > size )
	return NULL;
    const int64_t pool_base = les64(d+bufinfo+8);
    if ( pool_base <= 0 || (size_t)pool_base >= size )
	return NULL;

    const uint fhdr = bfres_switch_hdr_extra(vmajor);
    // name(8) + path(8) + skeleton(8) + vertex-buffer array(8) precede the
    // shapes-array field; materials/userdata/etc that follow it aren't
    // needed here since shapes are located by scanning for "FSHP" magics
    // below, not via a numShape count field.
    const int64_t shapes_val_field = fmdl_arr + 4 + fhdr + 32;

    if ( (size_t)shapes_val_field+8 > size ) return NULL;
    const int64_t shapes_val = les64(d+shapes_val_field);
    if ( shapes_val <= 0 || (size_t)shapes_val+0x60 > size || memcmp(d+shapes_val,"FSHP",4) )
	return NULL;

    // Count shapes by scanning for "FSHP" magics from the first one (same
    // approach the already-verified name-resolution manifest code uses --
    // FSHP entries aren't fixed-stride, so there's no clean array stride to
    // step through instead).
    uint n_fshp = 0;
    {
	const uint8_t *s = d+shapes_val;
	while ( s && (size_t)(s-d) < size )
	{
	    n_fshp++;
	    s = memmem(s+4,size-(s+4-d),"FSHP",4);
	}
    }
    if (!n_fshp) return NULL;

    model_t *out = calloc(1,sizeof(model_t));
    if (!out) return NULL;
    out->meshes = calloc(n_fshp,sizeof(mesh_t));
    if (!out->meshes) { free(out); return NULL; }

    // Parse FMAT materials if present so DAE materials and texture bindings resolve
    const int64_t mat_val_field = shapes_val_field + 16;
    const int64_t mat_val = (size_t)mat_val_field+8 <= size ? les64(d+mat_val_field) : -1;
    uint n_fmat = 0;
    if ( mat_val > 0 && (size_t)mat_val+0x20 <= size && !memcmp(d+mat_val,"FMAT",4) )
    {
	const uint8_t *m = d+mat_val;
	while ( m && (size_t)(m-d) < size )
	{
	    n_fmat++;
	    m = memmem(m+4,size-(m+4-d),"FMAT",4);
	}
    }

    if ( n_fmat )
    {
	out->materials = calloc(n_fmat,sizeof(material_t));
	if (out->materials)
	{
	    int64_t mp = mat_val;
	    for ( uint mi = 0; mi < n_fmat && mp > 0 && (size_t)mp+0x20 <= size; mi++ )
	    {
		if (memcmp(d+mp,"FMAT",4)) break;
		const uint mhdr = bfres_switch_hdr_extra(vmajor);
		const int64_t mp_base = mp + 4 + mhdr;
		const char *matname = rel_string_switch(d,size,les64(d+mp_base));
		material_t *mat = out->materials + out->num_materials++;
		snprintf(mat->name,sizeof(mat->name),"%s",
		    matname && *matname ? matname : "material");

		if ( (size_t)mp_base+0x30 <= size )
		{
		    const int64_t texref_arr = les64(d+mp_base+0x28);
		    if ( texref_arr > 0 && (size_t)texref_arr+8 <= size )
		    {
			const char *tname = rel_string_switch(d,size,les64(d+texref_arr));
			if (tname && *tname)
			{
			    snprintf(mat->textures[0],sizeof(mat->textures[0]),"%s",tname);
			    mat->texture_coord[0] = 0; // uv0
			    mat->num_textures = 1;
			}
		    }
		}

		const uint8_t *next_m = mi+1 < n_fmat
		    ? memmem(d+mp+4,size-(mp+4),"FMAT",4) : NULL;
		mp = next_m ? next_m-d : -1;
	    }
	}
    }

    int64_t sh = shapes_val;
    for ( uint si = 0; si < n_fshp && sh > 0 && (size_t)sh+0x60 <= size; si++ )
    {
	if (memcmp(d+sh,"FSHP",4)) break;
	const uint shdr = bfres_switch_hdr_extra(vmajor);
	const int64_t sname_off = sh + 4 + shdr;
	const char *sname = rel_string_switch(d,size,les64(d+sname_off));
	const int64_t fvtx = les64(d+sname_off+8);
	const int64_t mesh_arr_off_field = sname_off + 16;
	const int64_t mesh_arr = les64(d+mesh_arr_off_field);
	const uint8_t num_mesh = (size_t)sname_off+88 < size ? d[sname_off+87] : 0;
	const uint16_t fmat_idx = (size_t)sname_off+70 <= size ? le16(d+sname_off+68) : 0;

	do
	{
	    if ( fvtx <= 0 || !num_mesh || mesh_arr <= 0 )
		break;
	    fvtx_switch_t fv;
	    if ( !read_fvtx_switch(d,size,(size_t)fvtx,vmajor,pool_base,&fv) )
		break;

	    // First mesh only (LOD 0) -- same "no multi-LOD concept in a
	    // plain DAE" scope ParseBFRES() already uses for Wii U.
	    const int64_t mesh = mesh_arr;
	    if ( (size_t)mesh+56 > size ) break;
	    const uint32_t face_off  = le32(d+mesh+32);
	    // Unlike VertexAttrib.Format (which explicitly flips to big-endian
	    // for just that field), Mesh.Load() reads PrimitiveType/IndexFormat
	    // with no ByteOrder override, so these are plain little-endian
	    // like everything else in the file -- confirmed on a real file:
	    // bytes 03 00 00 00 / 01 00 00 00 are plain-LE 3 (Triangles) and
	    // 1 (UInt16), not byte-swapped values.
	    const uint32_t prim_raw  = le32(d+mesh+36);
	    const uint32_t ifmt_raw  = le32(d+mesh+40);
	    const uint32_t idx_count = le32(d+mesh+44);
	    if ( prim_raw != 3 || !idx_count || idx_count > 0x1000000 ) break; // triangles only
	    const uint isz = ifmt_raw == 2 ? 4 : 2; // 0=u8(as u16),1=u16,2=u32

	    const uint64_t idata = (uint64_t)pool_base + face_off;
	    if ( idata + (uint64_t)idx_count*isz > size ) break;

	    mesh_t *ms = out->meshes + out->num_meshes;
	    snprintf(ms->name,sizeof(ms->name),"%s",
		sname && *sname ? sname : "shape");
	    ms->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

	    ms->positions = calloc(idx_count,sizeof(vec3_t));
	    ms->normals   = calloc(idx_count,sizeof(vec3_t));
	    ms->texcoords = calloc(idx_count,sizeof(vec2_t));
	    ms->vertices  = calloc(idx_count,sizeof(vertex_t));
	    if ( !ms->positions || !ms->normals || !ms->texcoords || !ms->vertices )
	    {
		free(ms->positions); free(ms->normals);
		free(ms->texcoords); free(ms->vertices);
		memset(ms,0,sizeof(*ms));
		break;
	    }

	    uint n = 0;
	    for ( uint32_t k = 0; k < idx_count; k++ )
	    {
		const uint8_t *ip = d + idata + (size_t)k*isz;
		const uint32_t vi = isz == 4 ? le32(ip) : le16(ip);
		if ( vi >= fv.count ) continue;

		float v[4];
		if ( attr_read_switch(fv.pos + (size_t)vi*fv.stride_pos,
			fv.avail_pos - (size_t)vi*fv.stride_pos, fv.fmt_pos,v) )
		    { ms->positions[n].x=v[0]; ms->positions[n].y=v[1]; ms->positions[n].z=v[2]; }
		if ( fv.nrm && attr_read_switch(fv.nrm + (size_t)vi*fv.stride_nrm,
			fv.avail_nrm - (size_t)vi*fv.stride_nrm, fv.fmt_nrm,v) )
		    { ms->normals[n].x=v[0]; ms->normals[n].y=v[1]; ms->normals[n].z=v[2]; }
		if ( fv.uv && attr_read_switch(fv.uv + (size_t)vi*fv.stride_uv,
			fv.avail_uv - (size_t)vi*fv.stride_uv, fv.fmt_uv,v) )
		    { ms->texcoords[n].u=v[0]; ms->texcoords[n].v=v[1]; }

		ms->vertices[n].position_idx = (int)n;
		ms->vertices[n].normal_idx   = fv.nrm ? (int)n : -1;
		ms->vertices[n].texcoord_idx = fv.uv  ? (int)n : -1;
		n++;
	    }
	    if (n)
	    {
		ms->num_positions = ms->num_normals = ms->num_texcoords = n;
		ms->num_vertices = n;
		out->num_meshes++;
	    }
	    else
	    {
		free(ms->positions); free(ms->normals);
		free(ms->texcoords); free(ms->vertices);
		memset(ms,0,sizeof(*ms));
	    }
	}
	while (0);

	const uint8_t *next = si+1 < n_fshp
	    ? memmem(d+sh+4,size-(sh+4),"FSHP",4) : NULL;
	sh = next ? next-d : -1;
    }

    if (!out->num_meshes)
    {
	FreeModel(out);
	return NULL;
    }
    return out;
}
