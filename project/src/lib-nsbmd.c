// NSBMD ("BMD0") -- Nintendo DS model container.
//
// A BMD0 file holds an MDL0 block, which holds one or more models. Each
// model has a bone dictionary and a set of shapes ("polygons"), and each
// shape's geometry is a DS geometry-engine command list: the same packed
// FIFO command stream the hardware consumes, four command bytes per word
// followed by their parameters. Decoding that stream is what actually
// produces vertices.

#include "lib-nsbmd.h"
#include "lib-brres-model.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-std.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned int uint;

//-----------------------------------------------------------------------------

static uint16_t rd16 ( const uint8_t *p ) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
static uint32_t rd32 ( const uint8_t *p )
    { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static int16_t  rds16 ( const uint8_t *p ) { return (int16_t)rd16(p); }

// DS fixed-point: positions are 1:3:12, texcoords 1:11:4.
static float fx12 ( int v ) { return (float)v / 4096.0f; }
static float fx4  ( int v ) { return (float)v / 16.0f; }
// Normals are 1:0:9 in a packed 10-bit-per-axis word.
static float fx9 ( int v )
{
    if ( v & 0x200 ) v -= 0x400;   // sign-extend 10 bits
    return (float)v / 512.0f;
}

//-----------------------------------------------------------------------------
// Nitro "3D info list" dictionary: a fixed header, a per-entry data array and
// a table of 16-byte names.
//-----------------------------------------------------------------------------

typedef struct
{
    uint n;             // entry count
    uint data_off;      // offset of the first entry's data
    uint data_size;     // bytes per entry
    uint names_off;     // offset of the 16-byte name table
}
nitro_dict_t;

static int read_dict ( nitro_dict_t *d, const uint8_t *base, size_t avail )
{
    if ( avail < 0x10 ) return 0;
    const uint n = base[1];
    if (!n) return 0;

    // header(8) + unknown block(n*4 + 4) then the info block header
    const uint unk_off = 8;
    const uint unk_size = 4 + n*4;
    const uint info_hdr = unk_off + unk_size;
    if ( info_hdr + 4 > avail ) return 0;

    // Info block header: u16 bytes-per-entry, u16 offset from this header to
    // the name table (which is 4 + n*item_size -- the entries sit directly
    // after the 4-byte header).
    const uint item_size = rd16(base+info_hdr);
    const uint item_off  = rd16(base+info_hdr+2);
    if ( !item_size || item_size > 0x1000 ) return 0;

    d->n = n;
    d->data_size = item_size;
    d->data_off  = info_hdr + 4;
    d->names_off = info_hdr + item_off;
    if ( (size_t)d->names_off + (size_t)n*16 > avail ) return 0;
    return 1;
}

static void dict_name ( const nitro_dict_t *d, const uint8_t *base, uint i, char *out, size_t outsz )
{
    const uint8_t *p = base + d->names_off + i*16;
    size_t n = 0;
    while ( n < 16 && n+1 < outsz && p[n] ) { out[n] = (char)p[n]; n++; }
    out[n] = 0;
}

//-----------------------------------------------------------------------------
// DS geometry command list
//-----------------------------------------------------------------------------

typedef struct
{
    vec3_t *pos;   size_t n_pos,  cap_pos;
    vec3_t *nrm;   size_t n_nrm,  cap_nrm;
    vec2_t *uv;    size_t n_uv,   cap_uv;
    vertex_t *vtx; size_t n_vtx,  cap_vtx;
}
geom_t;

#define GROW(arr,n,cap,type) \
    do { if ( (n) >= (cap) ) { \
	    size_t nc = (cap) ? (cap)*2 : 256; \
	    type *np = REALLOC((arr),nc*sizeof(type)); \
	    if (!np) return 0; (arr)=np; (cap)=nc; } } while (0)

static int push_vertex ( geom_t *g, vec3_t p, vec3_t n, vec2_t t, int has_n, int has_t )
{
    GROW(g->pos,g->n_pos,g->cap_pos,vec3_t);
    g->pos[g->n_pos] = p;
    GROW(g->nrm,g->n_nrm,g->cap_nrm,vec3_t);
    g->nrm[g->n_nrm] = n;
    GROW(g->uv,g->n_uv,g->cap_uv,vec2_t);
    g->uv[g->n_uv] = t;
    GROW(g->vtx,g->n_vtx,g->cap_vtx,vertex_t);
    g->vtx[g->n_vtx].position_idx = (int)g->n_pos;
    g->vtx[g->n_vtx].normal_idx   = has_n ? (int)g->n_nrm : -1;
    g->vtx[g->n_vtx].texcoord_idx = has_t ? (int)g->n_uv  : -1;
    g->n_pos++; g->n_nrm++; g->n_uv++; g->n_vtx++;
    return 1;
}

// Emits the triangles of one primitive run, converting quads and strips to
// triangles so the DAE writer only ever sees a triangle list.
static int emit_primitive ( geom_t *g, uint prim, const vec3_t *P, const vec3_t *N,
			     const vec2_t *T, const int *hasN, const int *hasT, uint count )
{
    #define V(i) do { if (!push_vertex(g,P[i],N[i],T[i],hasN[i],hasT[i])) return 0; } while (0)
    switch (prim)
    {
	case 0: // triangles
	    for ( uint i = 0; i+2 < count; i += 3 ) { V(i); V(i+1); V(i+2); }
	    break;
	case 1: // quads
	    for ( uint i = 0; i+3 < count; i += 4 )
		{ V(i); V(i+1); V(i+2); V(i); V(i+2); V(i+3); }
	    break;
	case 2: // triangle strip
	    for ( uint i = 0; i+2 < count; i++ )
	    {
		if ( i & 1 ) { V(i+1); V(i); V(i+2); }
		else         { V(i); V(i+1); V(i+2); }
	    }
	    break;
	case 3: // quad strip
	    for ( uint i = 0; i+3 < count; i += 2 )
		{ V(i); V(i+1); V(i+3); V(i); V(i+3); V(i+2); }
	    break;
    }
    #undef V
    return 1;
}

// Runs one display list. The stream packs four command bytes into each word;
// each command's parameters follow, in order, after the packed word.
static int run_display_list ( geom_t *g, const uint8_t *d, size_t size )
{
    static const uint8_t nparams[0x100] =
    {
	[0x00]=0, [0x10]=1, [0x11]=0, [0x12]=1, [0x13]=1, [0x14]=1,
	[0x15]=0, [0x16]=16,[0x17]=12,[0x18]=16,[0x19]=12,[0x1a]=9,
	[0x1b]=3, [0x1c]=3,
	[0x20]=1, [0x21]=1, [0x22]=1, [0x23]=2, [0x24]=1, [0x25]=1,
	[0x26]=1, [0x27]=1, [0x28]=1, [0x29]=1, [0x2a]=1, [0x2b]=1,
	[0x30]=1, [0x31]=1, [0x32]=1, [0x33]=1, [0x34]=32,
	[0x40]=1, [0x41]=0,
	[0x50]=1, [0x60]=1, [0x70]=3, [0x71]=2, [0x72]=1,
    };

    // Current vertex state, latched between commands like the hardware does.
    vec3_t cur_p = {0,0,0}, cur_n = {0,0,0};
    vec2_t cur_t = {0,0};
    int has_n = 0, has_t = 0;

    #define MAXRUN 8192
    static vec3_t P[MAXRUN], N[MAXRUN];
    static vec2_t T[MAXRUN];
    static int    HN[MAXRUN], HT[MAXRUN];
    uint run = 0, prim = 0, in_prim = 0;

    size_t pos = 0;
    while ( pos + 4 <= size )
    {
	const uint8_t cmd[4] = { d[pos], d[pos+1], d[pos+2], d[pos+3] };
	pos += 4;

	for ( int c = 0; c < 4; c++ )
	{
	    const uint8_t op = cmd[c];
	    const uint np = nparams[op];
	    if ( pos + (size_t)np*4 > size )
		return 1; // truncated stream: keep what we already decoded
	    const uint8_t *p = d + pos;
	    pos += (size_t)np*4;

	    switch (op)
	    {
		case 0x40: // BEGIN_VTXS
		    prim = rd32(p) & 3;
		    run = 0;
		    in_prim = 1;
		    break;

		case 0x41: // END_VTXS
		    if (in_prim)
			emit_primitive(g,prim,P,N,T,HN,HT,run);
		    in_prim = 0;
		    run = 0;
		    break;

		case 0x21: // NORMAL
		{
		    const uint32_t v = rd32(p);
		    cur_n.x = fx9( v        & 0x3FF);
		    cur_n.y = fx9((v >> 10) & 0x3FF);
		    cur_n.z = fx9((v >> 20) & 0x3FF);
		    has_n = 1;
		    break;
		}

		case 0x22: // TEXCOORD
		    cur_t.u = fx4(rds16(p));
		    cur_t.v = fx4(rds16(p+2));
		    has_t = 1;
		    break;

		case 0x23: // VTX_16
		    cur_p.x = fx12(rds16(p));
		    cur_p.y = fx12(rds16(p+2));
		    cur_p.z = fx12(rds16(p+4));
		    goto emit;

		case 0x24: // VTX_10
		{
		    const uint32_t v = rd32(p);
		    #define S10(x) ( ((x) & 0x200) ? (int)(x)-0x400 : (int)(x) )
		    cur_p.x = (float)S10( v        & 0x3FF)/64.0f;
		    cur_p.y = (float)S10((v >> 10) & 0x3FF)/64.0f;
		    cur_p.z = (float)S10((v >> 20) & 0x3FF)/64.0f;
		    #undef S10
		    goto emit;
		}

		case 0x25: // VTX_XY
		    cur_p.x = fx12(rds16(p));
		    cur_p.y = fx12(rds16(p+2));
		    goto emit;

		case 0x26: // VTX_XZ
		    cur_p.x = fx12(rds16(p));
		    cur_p.z = fx12(rds16(p+2));
		    goto emit;

		case 0x27: // VTX_YZ
		    cur_p.y = fx12(rds16(p));
		    cur_p.z = fx12(rds16(p+2));
		    goto emit;

		case 0x28: // VTX_DIFF -- a signed 10-bit delta on each axis
		{
		    const uint32_t v = rd32(p);
		    #define D10(x) ( (((x) & 0x200) ? (int)(x)-0x400 : (int)(x)) / 4096.0f / 8.0f )
		    cur_p.x += D10( v        & 0x3FF);
		    cur_p.y += D10((v >> 10) & 0x3FF);
		    cur_p.z += D10((v >> 20) & 0x3FF);
		    #undef D10
		    goto emit;
		}

		emit:
		    if ( in_prim && run < MAXRUN )
		    {
			P[run]=cur_p; N[run]=cur_n; T[run]=cur_t;
			HN[run]=has_n; HT[run]=has_t;
			run++;
		    }
		    break;

		default:
		    break; // matrix/material/lighting commands do not add geometry
	    }
	}
    }
    if (in_prim)
	emit_primitive(g,prim,P,N,T,HN,HT,run);
    return 1;
}

//-----------------------------------------------------------------------------

// Walks a Model's RenderCommandList to recover bone parent/child hierarchy
// -- NSBMD has no direct parent-index field in the bone dictionary itself
// (unlike most other formats this project parses); the relationship is
// only recorded as a side effect of the "Multiply Current Matrix with Bone
// Matrix" render command's own parameters. Layout from
// github.com/scurest/nsbmd_docs (nsbmd_docs.txt), read directly rather
// than guessed: opcode low 5 bits select the operation, high 3 bits (0x40
// load-from-stack, 0x20 store-to-stack) add extra parameter bytes on top
// of the base set for a handful of opcodes.
static void parse_bone_hierarchy ( model_t *out, const uint8_t *cmds, size_t len )
{
    size_t p = 0;
    while ( p < len )
    {
	const uint8_t opcode = cmds[p++];
	const uint8_t op5 = opcode & 0x1f;

	if ( op5 == 0x01 ) // End
	    break;

	switch (op5)
	{
	    case 0x00: break;			// Nop: 0 params
	    case 0x02: p += 2; break;		// Unknown: 2 params
	    case 0x03: p += 1; break;		// Load Matrix from Stack: 1
	    case 0x04:				// Bind Material: 1 param, regardless of high bits
		p += 1; break;
	    case 0x05: p += 1; break;		// Draw Mesh: 1 param
	    case 0x06:				// Multiply w/ Bone Matrix: 3 base + high-bit extras
	    {
		if ( p+2 > len ) return;
		const uint bone_idx   = cmds[p];
		const uint parent_idx = cmds[p+1];
		if ( bone_idx < out->num_joints && parent_idx < out->num_joints
		     && bone_idx != parent_idx )
		    out->joints[bone_idx].parent_idx = (int)parent_idx;
		p += 3;
		if ( opcode & 0x40 ) p += 1;
		if ( opcode & 0x20 ) p += 1;
		break;
	    }
	    case 0x07: p += (opcode == 0x47) ? 2 : 1; break;
	    case 0x08: p += 1; break;
	    case 0x09:				// Calculate Skinning Equation: variable
	    {
		if ( p+2 > len ) return;
		const uint num_terms = cmds[p+1];
		p += 2 + (size_t)num_terms * 3;
		break;
	    }
	    case 0x0b: break;			// Scale Up: 0 params
	    case 0x0c: p += 2; break;
	    case 0x0d: p += 2; break;
	    default:
		// Unrecognized opcode -- stop rather than risk misreading the
		// rest of the stream as garbage parameters.
		return;
	}
    }
}

//-----------------------------------------------------------------------------

model_t* ParseEarlyDSBMD ( const uint8_t *data, size_t size )
{
    if ( !data || size < 60 )
	return NULL;

    uint8_t *allocated_data = NULL;
    if ( size >= 5 && !memcmp(data, "LZ77\x10", 5) )
    {
	uint dec_sz = 0;
	u8 *dec_buf = 0;
	if ( DecodeLZ10LZ11(&dec_buf, &dec_sz, data + 4, (uint)size - 4) == ERR_OK && dec_buf )
	{
	    allocated_data = dec_buf;
	    data = allocated_data;
	    size = dec_sz;
	}
    }
    else if ( size >= 4 && data[0] == 0x10 )
    {
	const uint uncomp_len = (uint)data[1] | (uint)data[2]<<8 | (uint)data[3]<<16;
	if ( uncomp_len > size && uncomp_len < 0x2000000 )
	{
	    uint dec_sz = 0;
	    u8 *dec_buf = 0;
	    if ( DecodeLZ10LZ11(&dec_buf, &dec_sz, data, (uint)size) == ERR_OK && dec_buf )
	    {
		allocated_data = dec_buf;
		data = allocated_data;
		size = dec_sz;
	    }
	}
    }

    if ( size < 60 )
    {
	if (allocated_data) FREE(allocated_data);
	return NULL;
    }

    const uint32_t bone_count = rd32(data + 4);
    const uint32_t bone_off   = rd32(data + 8);
    const uint32_t shapes_base= rd32(data + 16);
    const uint32_t mat_count  = rd32(data + 20);
    const uint32_t mat_off    = rd32(data + 24);
    const uint32_t tex_count  = rd32(data + 36);
    const uint32_t tex_off    = rd32(data + 40);

    if ( shapes_base != 0x3c || bone_off <= shapes_base || bone_off > size )
    {
	if (allocated_data) FREE(allocated_data);
	return NULL;
    }

    model_t *out = CALLOC(1,sizeof(model_t));
    if (!out)
    {
	if (allocated_data) FREE(allocated_data);
	return NULL;
    }

    // --- bones -------------------------------------------------------------
    if ( bone_count > 0 && bone_off < size )
    {
	out->joints = CALLOC(bone_count, sizeof(joint_t));
	if (out->joints)
	{
	    out->num_joints = bone_count;
	    for ( uint b = 0; b < bone_count; b++ )
	    {
		joint_t *j = out->joints + b;
		const uint32_t boff = bone_off + b * 0x48;
		if ( boff + 8 <= size )
		{
		    uint32_t name_ptr = rd32(data + boff + 4);
		    if ( name_ptr > 0 && name_ptr < size )
		    {
			size_t slen = 0;
			while ( name_ptr + slen < size && slen + 1 < sizeof(j->name) && data[name_ptr + slen] )
			{
			    j->name[slen] = (char)data[name_ptr + slen];
			    slen++;
			}
			j->name[slen] = 0;
		    }
		}
		if ( !j->name[0] )
		    snprintf(j->name, sizeof(j->name), "bone_%u", b);
		j->parent_idx = -1;
		j->scale.x = j->scale.y = j->scale.z = 1.0f;
	    }
	}
    }

    // --- materials ---------------------------------------------------------
    if ( mat_count > 0 && mat_off > 0 && mat_off < size )
    {
	out->materials = CALLOC(mat_count, sizeof(material_t));
	if (out->materials)
	{
	    out->num_materials = mat_count;
	    for ( uint m = 0; m < mat_count; m++ )
	    {
		material_t *mat = out->materials + m;
		snprintf(mat->name, sizeof(mat->name), "mat_%u", m);
		if ( tex_count > 0 && tex_off > 0 && tex_off + m * 32 + 32 <= size )
		{
		    const uint32_t name_ptr = rd32(data + tex_off + m * 32);
		    if ( name_ptr > 0 && name_ptr < size && data[name_ptr] )
		    {
			size_t slen = 0;
			while ( name_ptr + slen < size && slen + 1 < sizeof(mat->textures[0]) && data[name_ptr + slen] )
			{
			    mat->textures[0][slen] = (char)data[name_ptr + slen];
			    slen++;
			}
			mat->textures[0][slen] = 0;
			mat->num_textures = 1;
		    }
		}
	    }
	}
    }

    // --- display lists -----------------------------------------------------
    typedef struct { uint32_t sz; uint32_t off; } dl_entry_t;
    dl_entry_t dls[512];
    uint n_dls = 0;

    for ( uint32_t off = shapes_base; off + 8 <= bone_off && n_dls < 512; off += 4 )
    {
	uint32_t sz = rd32(data + off);
	uint32_t dloff = rd32(data + off + 4);
	if ( dloff >= shapes_base && dloff + sz <= bone_off && sz >= 16 )
	{
	    const uint8_t *w = data + dloff;
	    if ( (w[0] == 0x40 || w[1] == 0x40 || w[2] == 0x40 || w[3] == 0x40) &&
		 (w[0] >= 0x14 || w[1] >= 0x14 || w[2] >= 0x14 || w[3] >= 0x14) )
	    {
		bool overlap = false;
		for ( uint k = 0; k < n_dls; k++ )
		{
		    if ( dloff >= dls[k].off && dloff < dls[k].off + dls[k].sz )
		    {
			overlap = true;
			break;
		    }
		}
		if (!overlap)
		{
		    dls[n_dls].sz = sz;
		    dls[n_dls].off = dloff;
		    n_dls++;
		}
	    }
	}
    }

    if ( n_dls > 0 )
    {
	out->meshes = CALLOC(n_dls, sizeof(mesh_t));
	if (out->meshes)
	{
	    for ( uint i = 0; i < n_dls; i++ )
	    {
		geom_t g;
		memset(&g, 0, sizeof(g));
		run_display_list(&g, data + dls[i].off, dls[i].sz);
		if ( !g.n_vtx )
		{
		    FREE(g.pos); FREE(g.nrm); FREE(g.uv); FREE(g.vtx);
		    continue;
		}
		mesh_t *mesh = out->meshes + out->num_meshes;
		snprintf(mesh->name, sizeof(mesh->name), "mesh_%u", (uint)out->num_meshes);
		mesh->positions    = g.pos; mesh->num_positions = g.n_pos;
		mesh->normals      = g.nrm; mesh->num_normals   = g.n_nrm;
		mesh->texcoords    = g.uv;  mesh->num_texcoords = g.n_uv;
		mesh->vertices     = g.vtx; mesh->num_vertices  = g.n_vtx;
		mesh->material_idx = (out->num_materials > 0) ? (int)(i % out->num_materials) : 0;
		out->num_meshes++;
	    }
	}
    }

    if (allocated_data) FREE(allocated_data);

    if ( !out->num_meshes && !out->num_joints )
    {
	FreeModel(out);
	return NULL;
    }
    return out;
}

//-----------------------------------------------------------------------------

static inline bool is_ext ( ccp src, ccp ext )
{
    if ( !src || !ext ) return false;
    const size_t slen = strlen(src);
    const size_t elen = strlen(ext);
    return slen >= elen && !strcasecmp(src + slen - elen, ext);
}

enumError ExportEarlyDSBMDTextures ( const uint8_t *data, size_t size, const char *dest_path_or_dir )
{
    if ( !data || size < 60 || !dest_path_or_dir )
	return ERR_OK;

    uint8_t *allocated_data = NULL;
    if ( size >= 5 && !memcmp(data, "LZ77\x10", 5) )
    {
	uint dec_sz = 0;
	u8 *dec_buf = 0;
	if ( DecodeLZ10LZ11(&dec_buf, &dec_sz, data + 4, (uint)size - 4) == ERR_OK && dec_buf )
	{
	    allocated_data = dec_buf;
	    data = allocated_data;
	    size = dec_sz;
	}
    }
    else if ( size >= 4 && data[0] == 0x10 )
    {
	const uint uncomp_len = (uint)data[1] | (uint)data[2]<<8 | (uint)data[3]<<16;
	if ( uncomp_len > size && uncomp_len < 0x2000000 )
	{
	    uint dec_sz = 0;
	    u8 *dec_buf = 0;
	    if ( DecodeLZ10LZ11(&dec_buf, &dec_sz, data, (uint)size) == ERR_OK && dec_buf )
	    {
		allocated_data = dec_buf;
		data = allocated_data;
		size = dec_sz;
	    }
	}
    }

    if ( size < 60 )
    {
	if (allocated_data) FREE(allocated_data);
	return ERR_OK;
    }

    const uint32_t bone_off   = rd32(data + 8);
    const uint32_t shapes_base= rd32(data + 16);
    const uint32_t tex_count  = rd32(data + 36);
    const uint32_t tex_off    = rd32(data + 40);
    const uint32_t tex_data_off = rd32(data + 56);

    if ( shapes_base != 0x3c || bone_off <= shapes_base || bone_off > size || !tex_count || !tex_off || !tex_data_off || tex_data_off >= size )
    {
	if (allocated_data) FREE(allocated_data);
	return ERR_OK;
    }

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", dest_path_or_dir);
    if ( is_ext(dir, ".dae") )
    {
	char *slash = strrchr(dir, '/');
	if (slash) *slash = 0;
	else snprintf(dir, sizeof(dir), ".");
    }
    CreatePath(dir, true);

    uint cur_data_off = tex_data_off;
    for ( uint t = 0; t < tex_count; t++ )
    {
	const uint32_t toff = tex_off + t * 32;
	if ( toff + 32 > size ) break;

	const uint32_t name_ptr = rd32(data + toff);
	const uint32_t w_raw = rd32(data + toff + 12);
	const uint32_t h_raw = rd32(data + toff + 16);
	const uint w = (w_raw > 256) ? (w_raw >> 8) : w_raw;
	const uint h = (h_raw > 256) ? (h_raw >> 8) : h_raw;

	if ( !w || !h || w > 1024 || h > 1024 ) continue;

	char clean_name[128];
	if ( name_ptr > 0 && name_ptr < size && data[name_ptr] )
	{
	    size_t slen = 0;
	    while ( name_ptr + slen < size && slen + 1 < sizeof(clean_name) && data[name_ptr + slen] )
	    {
		clean_name[slen] = (char)data[name_ptr + slen];
		slen++;
	    }
	    clean_name[slen] = 0;
	}
	else
	{
	    snprintf(clean_name, sizeof(clean_name), "tex_%03u", t);
	}

	const size_t pix_size = (size_t)w * h;
	if ( (size_t)cur_data_off + pix_size > size ) break;

	const uint8_t *pixels = data + cur_data_off;
	const uint32_t pal_pos = cur_data_off + (uint32_t)pix_size;
	if ( pal_pos + 2 > size ) break;

	const uint8_t *pal_bytes = data + pal_pos;
	const size_t pal_len = size - pal_pos;
	const size_t n_colors = (pal_len > 512) ? 256 : (pal_len / 2);

	u8 *rgba = MALLOC(w * h * 4);
	if ( !rgba ) continue;

	u8 pal_rgba[256][4];
	memset(pal_rgba, 0, sizeof(pal_rgba));
	for ( size_t c = 0; c < n_colors; c++ )
	{
	    const uint16_t col = (uint16_t)pal_bytes[c*2] | (uint16_t)pal_bytes[c*2+1]<<8;
	    pal_rgba[c][0] = (col & 0x1f) << 3;
	    pal_rgba[c][1] = ((col >> 5) & 0x1f) << 3;
	    pal_rgba[c][2] = ((col >> 10) & 0x1f) << 3;
	    pal_rgba[c][3] = (c == 0 && col == 0) ? 0 : 255;
	}

	for ( size_t p = 0; p < pix_size; p++ )
	{
	    const uint8_t idx = pixels[p];
	    rgba[p*4+0] = pal_rgba[idx][0];
	    rgba[p*4+1] = pal_rgba[idx][1];
	    rgba[p*4+2] = pal_rgba[idx][2];
	    rgba[p*4+3] = pal_rgba[idx][3];
	}

	char out_path[PATH_MAX];
	snprintf(out_path, sizeof(out_path), "%s/%s.png", dir, clean_name);

	Image_t img;
	InitializeIMG(&img);
	const uint xw = EXPAND8(w), xh = EXPAND8(h);
	u8 *padded = xw == w && xh == h ? rgba : CALLOC(1, xw * xh * 4);
	if (padded != rgba)
	{
	    for (uint y = 0; y < h; y++)
		memcpy(padded + (size_t)y * xw * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
	    FREE(rgba);
	}
	img.data = padded;
	img.data_alloced = true;
	img.data_size = xw * xh * 4;
	img.width = w; img.xwidth = xw;
	img.height = h; img.xheight = xh;
	img.iform = img.info_iform = IMG_X_RGB;
	img.info_fform = FF_PNG;
	img.info_n_image = 1;
	img.endian = &le_func;

	SavePNG(&img, false, 0, out_path, 0, 0, true, 0);
	ResetIMG(&img);

	cur_data_off = pal_pos + (uint32_t)(n_colors * 2);
    }

    if (allocated_data) FREE(allocated_data);
    return ERR_OK;
}

//-----------------------------------------------------------------------------

model_t* ParseNSBMD ( const uint8_t *data, size_t size )
{
    if ( !data || size < 0x20 )
	return NULL;
    if ( memcmp(data,"BMD0",4) )
	return ParseEarlyDSBMD(data, size);
    if ( rd16(data+4) != 0xFEFF )
	return NULL;

    const uint n_blocks = rd16(data+0x0e);
    if ( !n_blocks || (size_t)0x10 + n_blocks*4 > size )
	return NULL;

    // Find the MDL0 block.
    const uint8_t *mdl = NULL;
    size_t mdl_size = 0;
    for ( uint i = 0; i < n_blocks; i++ )
    {
	const uint32_t off = rd32(data+0x10+i*4);
	if ( (size_t)off + 8 > size ) continue;
	if ( memcmp(data+off,"MDL0",4) ) continue;
	mdl = data + off;
	mdl_size = rd32(data+off+4);
	if ( mdl_size > size - off ) mdl_size = size - off;
	break;
    }
    if ( !mdl || mdl_size < 0x10 )
	return NULL;

    // Model dictionary starts right after the MDL0 header.
    nitro_dict_t models;
    if ( !read_dict(&models,mdl+8,mdl_size-8) )
	return NULL;

    // Only the first model is exported; DAE has no multi-model concept here.
    const uint32_t model_off = rd32(mdl+8+models.data_off);
    if ( (size_t)model_off + 0x40 > mdl_size )
	return NULL;
    const uint8_t *m = mdl + model_off;
    const size_t m_avail = mdl_size - model_off;

    model_t *out = CALLOC(1,sizeof(model_t));
    if (!out) return NULL;

    // --- bones -------------------------------------------------------------
    // The bone dictionary sits at a fixed offset in the model header.
    const uint32_t bones_off = 0x40;
    nitro_dict_t bones;
    if ( bones_off < m_avail && read_dict(&bones,m+bones_off,m_avail-bones_off) )
    {
	out->joints = CALLOC(bones.n,sizeof(joint_t));
	if (out->joints)
	{
	    out->num_joints = bones.n;
	    for ( uint i = 0; i < bones.n; i++ )
	    {
		joint_t *j = out->joints+i;
		dict_name(&bones,m+bones_off,i,j->name,sizeof(j->name));
		j->parent_idx = -1;   // recovered below from the render commands, if present
		j->scale.x = j->scale.y = j->scale.z = 1.0f;
	    }

	    const uint32_t render_cmds_off = rd32(m+0x04);
	    if ( render_cmds_off && (size_t)render_cmds_off < m_avail )
		parse_bone_hierarchy(out,m+render_cmds_off,m_avail-render_cmds_off);
	}
    }

    // --- shapes ------------------------------------------------------------
    const uint32_t shapes_off = rd32(m+0x0c);
    nitro_dict_t shapes;
    if ( shapes_off < m_avail && read_dict(&shapes,m+shapes_off,m_avail-shapes_off) )
    {
	out->meshes = CALLOC(shapes.n,sizeof(mesh_t));
	if (out->meshes)
	{
	    const uint8_t *sbase = m + shapes_off;
	    for ( uint i = 0; i < shapes.n; i++ )
	    {
		const uint8_t *rec = sbase + shapes.data_off + i*shapes.data_size;
		if ( (size_t)(rec-m) + shapes.data_size > m_avail ) continue;
		// The dictionary entry is a bare u32 offset when itemSize is 4;
		// wider records keep it in the second word.
		const uint32_t sh_off = shapes.data_size >= 8 ? rd32(rec+4) : rd32(rec);
		if ( (size_t)shapes_off + sh_off + 0x10 > m_avail ) continue;
		const uint8_t *sh = sbase + sh_off;

		// shape header: a constant tag (0x00100000), the shape size, then
		// the display list's size and its offset from the shape start.
		const uint32_t dl_size = rd32(sh+8);
		const uint32_t dl_off  = rd32(sh+12);
		const size_t dl_pos = (size_t)(sh - m) + dl_off;
		if ( !dl_size || dl_pos + dl_size > m_avail ) continue;

		geom_t g;
		memset(&g,0,sizeof(g));
		run_display_list(&g,m+dl_pos,dl_size);
		if (!g.n_vtx)
		{
		    FREE(g.pos); FREE(g.nrm); FREE(g.uv); FREE(g.vtx);
		    continue;
		}

		mesh_t *mesh = out->meshes + out->num_meshes;
		dict_name(&shapes,sbase,i,mesh->name,sizeof(mesh->name));
		if (!mesh->name[0])
		    snprintf(mesh->name,sizeof(mesh->name),"shape%u",i);
		mesh->positions     = g.pos; mesh->num_positions = g.n_pos;
		mesh->normals       = g.nrm; mesh->num_normals   = g.n_nrm;
		mesh->texcoords     = g.uv;  mesh->num_texcoords = g.n_uv;
		mesh->vertices      = g.vtx; mesh->num_vertices  = g.n_vtx;
		mesh->material_idx  = -1;
		out->num_meshes++;
	    }
	}
    }

    if ( !out->num_meshes && !out->num_joints )
    {
	FreeModel(out);
	return NULL;
    }
    return out;
}
