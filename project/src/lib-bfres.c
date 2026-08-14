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
	mesh->material_idx = -1;

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
