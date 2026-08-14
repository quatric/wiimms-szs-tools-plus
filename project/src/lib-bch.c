// BCH / CTR H3D container -- see lib-bch.h.
//
// The relocation pass is a port of the reference H3D relocator: each entry
// in the relocation table names a source section and a pointer offset inside
// it, plus the target section whose base address should be added to the
// 32-bit value stored there.

#include "lib-std.h"
#include "lib-bch.h"

static inline u16 hrd16 ( const u8 *p ) { return (u16)p[0] | (u16)p[1]<<8; }
static inline u32 hrd32 ( const u8 *p )
    { return (u32)p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24; }
static inline void hwr32 ( u8 *p, u32 v )
    { p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }

// H3D section ids, in the order the reference enumerates them.
enum
{
    H3D_CONTENTS, H3D_STRINGS, H3D_COMMANDS, H3D_COMMANDS_SRC,
    H3D_RAW_DATA, H3D_RAW_DATA_TEXTURE, H3D_RAW_DATA_VERTEX,
    H3D_RAW_DATA_INDEX16, H3D_RAW_DATA_INDEX8,
    H3D_RAW_EXT, H3D_RAW_EXT_TEXTURE, H3D_RAW_EXT_VERTEX,
    H3D_RAW_EXT_INDEX16, H3D_RAW_EXT_INDEX8,
    H3D_BASE_ADDRESS
};

static ccp dict_names[BCH_N_DICTS] =
{
    "Models", "Materials", "Shaders", "Textures", "LUTs",
    "Lights", "Cameras", "Fogs",
    "SkeletalAnimations", "MaterialAnimations", "VisibilityAnimations",
    "LightAnimations", "CameraAnimations", "FogAnimations"
};

ccp GetBCHDictName ( bch_dict_id_t id )
{
    return id < BCH_N_DICTS ? dict_names[id] : "?";
}

void ResetBCH ( bch_t *bch )
{
    if (!bch) return;
    for ( int i = 0; i < BCH_N_DICTS; i++ )
	FREE(bch->dict[i].entries);
    FREE(bch->data);
    memset(bch,0,sizeof(*bch));
}

// Older H3D revisions had fewer sections, so the ids shift.
static int legacy_reloc_diff ( int section, uint bc )
{
    if ( bc > 7 && bc < 0x21 && section >= H3D_RAW_DATA_VERTEX )
	return -1;
    if ( bc < 7 && section >= H3D_COMMANDS_SRC )
	return 1;
    return 0;
}

static u32 section_address ( const bch_t *b, int section )
{
    switch (section)
    {
	case H3D_CONTENTS:		return b->contents_addr;
	case H3D_STRINGS:		return b->strings_addr;
	case H3D_COMMANDS:
	case H3D_COMMANDS_SRC:		return b->commands_addr;
	case H3D_RAW_DATA:
	case H3D_RAW_DATA_TEXTURE:
	case H3D_RAW_DATA_VERTEX:
	case H3D_RAW_DATA_INDEX8:	return b->raw_data_addr;
	// The reference tags 16-bit index buffers with the high bit so the
	// consumer can tell them apart; keep that, the mask is applied when
	// the pointer is actually followed.
	case H3D_RAW_DATA_INDEX16:	return b->raw_data_addr | (1u<<31);
	case H3D_RAW_EXT:
	case H3D_RAW_EXT_TEXTURE:
	case H3D_RAW_EXT_VERTEX:
	case H3D_RAW_EXT_INDEX8:	return b->raw_ext_addr;
	case H3D_RAW_EXT_INDEX16:	return b->raw_ext_addr | (1u<<31);
    }
    return 0;
}

// Applies the relocation table in place: every referenced 32-bit slot gets
// its target section's base address added to it.
static void apply_relocations ( bch_t *b )
{
    if ( !b->reloc_len || (u64)b->reloc_addr + b->reloc_len > b->size )
	return;

    for ( u32 off = 0; off + 4 <= b->reloc_len; off += 4 )
    {
	const u32 value = hrd32(b->data + b->reloc_addr + off);
	u32 ptr_addr = value & 0x1ffffff;
	int target = (value >> 25) & 0xf;
	const int source = value >> 29;

	target += legacy_reloc_diff(target,b->bc);
	if ( target != H3D_STRINGS )
	    ptr_addr <<= 2;

	const u32 slot = section_address(b,source) + ptr_addr;
	if ( (u64)slot + 4 > b->size )
	    continue;
	hwr32( b->data+slot, hrd32(b->data+slot) + section_address(b,target) );
    }
}

// A safe NUL-terminated string read out of the relocated buffer.
static ccp bch_str ( const bch_t *b, u32 addr )
{
    if ( !addr || addr >= b->size )
	return 0;
    const u8 *p = b->data + addr;
    const u8 *end = b->data + b->size;
    for ( const u8 *q = p; q < end; q++ )
	if (!*q)
	    return (ccp)p;
    return 0; // unterminated
}

enumError ScanBCH ( bch_t *bch, const u8 *data, uint size )
{
    if ( !bch || !data || size < 0x44 || memcmp(data,"BCH",3) || data[3] )
	return EINVAL;

    memset(bch,0,sizeof(*bch));
    bch->bc = data[4];
    bch->fc = data[5];

    // The RawExt pair only exists from revision 0x21 on, which shifts every
    // field after it.
    const bool has_ext = bch->bc >= 0x21;
    uint o = 8;
    bch->contents_addr = hrd32(data+o); o += 4;
    bch->strings_addr  = hrd32(data+o); o += 4;
    bch->commands_addr = hrd32(data+o); o += 4;
    bch->raw_data_addr = hrd32(data+o); o += 4;
    if (has_ext) { bch->raw_ext_addr = hrd32(data+o); o += 4; }
    bch->reloc_addr    = hrd32(data+o); o += 4;
    if ( o + 4 > size ) return EINVAL;
    bch->contents_len  = hrd32(data+o); o += 4;
    bch->strings_len   = hrd32(data+o); o += 4;
    bch->commands_len  = hrd32(data+o); o += 4;
    bch->raw_data_len  = hrd32(data+o); o += 4;
    if (has_ext) { bch->raw_ext_len = hrd32(data+o); o += 4; }
    bch->reloc_len     = hrd32(data+o); o += 4;
    if ( o > size ) return EINVAL;

    if ( (u64)bch->contents_addr + bch->contents_len > size
      || (u64)bch->strings_addr  + bch->strings_len  > size
      || (u64)bch->reloc_addr    + bch->reloc_len    > size )
	return EINVAL;

    // Work on a private copy: relocation rewrites pointers in place and the
    // caller's buffer must not be disturbed.
    bch->size = size;
    bch->data = MALLOC(size);
    if (!bch->data) return ERR_CANT_CREATE;
    memcpy(bch->data,data,size);
    apply_relocations(bch);

    // Contents: BCH_N_DICTS consecutive (valuesPtr, count, treePtr) triples.
    for ( int i = 0; i < BCH_N_DICTS; i++ )
    {
	const u32 rec = bch->contents_addr + (u32)i*12;
	if ( (u64)rec + 12 > size )
	    break;
	const u32 values = hrd32(bch->data+rec);
	const u32 count  = hrd32(bch->data+rec+4);
	const u32 tree   = hrd32(bch->data+rec+8);
	if ( !count || count > 0x10000 )
	    continue;
	// The patricia tree is a root node plus one node per name, 12 bytes
	// each: refbit(4) left(2) right(2) nameptr(4).
	if ( (u64)tree + (u64)(count+1)*12 > size )
	    continue;

	bch_entry_t *ent = CALLOC(count,sizeof(*ent));
	if (!ent) continue;
	uint n = 0;
	for ( u32 k = 0; k < count; k++ )
	{
	    ccp name = bch_str(bch,hrd32(bch->data + tree + (u64)(k+1)*12 + 8));
	    if (!name)
		continue;
	    ent[n].name = name;
	    // The values array is one pointer per entry.
	    ent[n].address = values && (u64)values + (u64)(k+1)*4 <= size
			   ? hrd32(bch->data + values + (u64)k*4) : 0;
	    n++;
	}
	if (n)
	{
	    bch->dict[i].entries = ent;
	    bch->dict[i].n = n;
	}
	else
	    FREE(ent);
    }

    return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		PICA200 geometry extraction		///////////////
//-----------------------------------------------------------------------------
//
// A BCH mesh does not store its vertex layout as a struct: it stores the GPU
// command list that would program the PICA200 to draw it. The attribute
// formats, the vertex stride and the index buffer all have to be recovered by
// replaying that command stream.

#include "lib-model-dae.h"
#include "lib-brres-model.h"

// model_t buffers are released by FreeModel(), which uses the real free(),
// so they must come from the real allocator too -- dclib redirects the bare
// names to its own debugging wrappers for the rest of this file.
#undef calloc
#undef free

// PICA200 registers that carry the information we need.
#define PICA_ATTRIBBUFFERS_LOC		0x200
#define PICA_ATTRIBBUFFERS_FORMAT_LOW	0x201
#define PICA_ATTRIBBUFFERS_FORMAT_HIGH	0x202
#define PICA_ATTRIBBUFFER0_OFFSET	0x203
#define PICA_ATTRIBBUFFER0_CONFIG1	0x204
#define PICA_ATTRIBBUFFER0_CONFIG2	0x205
#define PICA_INDEXBUFFER_CONFIG		0x227
#define PICA_NUMVERTICES		0x228
#define PICA_VSH_NUM_ATTR		0x242
#define PICA_PRIMITIVE_CONFIG		0x25E
#define PICA_VSH_ATTR_PERMUTATION_LOW	0x2BB
#define PICA_VSH_ATTR_PERMUTATION_HIGH	0x2BC

// Attribute names, as indexed by the shader permutation.
#define PICA_ATTR_POSITION	0
#define PICA_ATTR_NORMAL	1
#define PICA_ATTR_TEXCOORD0	4

// Element size for each PICA attribute format: byte, ubyte, short, float.
static const u8 pica_fmt_size[4] = { 1, 1, 2, 4 };

typedef struct
{
    uint name;      // PICA attribute name
    uint fmt;       // 0..3
    uint elements;  // 1..4
    uint offset;    // byte offset within the vertex
}
pica_attr_t;

// Replays one command stream. Commands are (parameter, header) word pairs;
// the header carries the register id, a mask, an extra-parameter count and a
// "consecutive" flag that walks consecutive registers.
typedef struct
{
    u32 attr_loc, buf_offset;
    u64 formats, buffer_attrs, permutation;
    uint stride, n_total;
    u32 index_addr, n_vertices, primitive;
}
pica_state_t;

static void pica_replay ( pica_state_t *st, const u8 *data, uint size, u32 ptr, u32 count )
{
    if ( !count || (u64)ptr + (u64)count*4 > size )
	return;
    uint i = 0;
    while ( i + 1 < count )
    {
	const u32 param = hrd32(data+ptr+(u64)i*4);
	const u32 cmd   = hrd32(data+ptr+(u64)(i+1)*4);
	i += 2;
	const uint id     = cmd & 0xffff;
	const uint extra  = (cmd >> 20) & 0x7ff;
	const bool consec = (cmd >> 31) != 0;

	uint n_written = 1;
	u32 p = param;
	for ( uint k = 0; k <= (consec ? extra : 0); k++ )
	{
	    const uint reg = consec ? id+k : id;
	    switch (reg)
	    {
		case PICA_ATTRIBBUFFERS_LOC:		st->attr_loc = p; break;
		case PICA_ATTRIBBUFFERS_FORMAT_LOW:	st->formats |= p; break;
		case PICA_ATTRIBBUFFERS_FORMAT_HIGH:	st->formats |= (u64)p << 32; break;
		case PICA_ATTRIBBUFFER0_OFFSET:		st->buf_offset = p; break;
		case PICA_ATTRIBBUFFER0_CONFIG1:	st->buffer_attrs |= p; break;
		case PICA_ATTRIBBUFFER0_CONFIG2:
		    st->buffer_attrs |= (u64)(p & 0xffff) << 32;
		    st->stride = (p >> 16) & 0xff;
		    break;
		case PICA_VSH_NUM_ATTR:			st->n_total = (p & 0xf) + 1; break;
		case PICA_VSH_ATTR_PERMUTATION_LOW:	st->permutation |= p; break;
		case PICA_VSH_ATTR_PERMUTATION_HIGH:	st->permutation |= (u64)p << 32; break;
		case PICA_INDEXBUFFER_CONFIG:		st->index_addr = p; break;
		case PICA_NUMVERTICES:			st->n_vertices = p; break;
		case PICA_PRIMITIVE_CONFIG:		st->primitive = p >> 8; break;
	    }
	    if ( consec && k < extra && i < count )
		p = hrd32(data+ptr+(u64)i*4), i++, n_written++;
	}
	if (!consec)
	{
	    // Non-consecutive writes repeat the same register; skip the extra
	    // parameters, then the pad word that keeps pairs 8-byte aligned.
	    for ( uint k = 0; k < extra && i < count; k++ ) i++;
	    if ( (extra+1) & 1 && extra > 0 && i < count ) i++;
	}
	(void)n_written;
    }
}

// Reads one attribute component as a float.
static float pica_read ( const u8 *p, uint fmt, uint el )
{
    switch (fmt)
    {
	case 0: return (float)(s8)p[el];
	case 1: return (float)p[el];
	case 2: return (float)(s16)( (u16)p[el*2] | (u16)p[el*2+1]<<8 );
	default:
	{
	    union { u32 u; float f; } c;
	    c.u = hrd32(p+el*4);
	    return c.f;
	}
    }
}

void * ParseBCH ( const u8 *data, uint size )
{
    bch_t bch;
    if ( ScanBCH(&bch,data,size) )
	return NULL;
    if ( !bch.dict[BCH_MODELS].n )
	{ ResetBCH(&bch); return NULL; }

    const u8 *b = bch.data;
    const uint bsize = bch.size;
    const u32 model = bch.dict[BCH_MODELS].entries[0].address;
    if ( (u64)model + 0x50 > bsize )
	{ ResetBCH(&bch); return NULL; }

    // Model layout: flags/scaling/silhouette(4), 3x4 world matrix (0x30),
    // materials dict (0x0c), then the mesh list (pointer + count).
    const u32 meshes_ptr = hrd32(b+model+0x40);
    const u32 meshes_cnt = hrd32(b+model+0x44);
    if ( !meshes_cnt || meshes_cnt > 0x10000 )
	{ ResetBCH(&bch); return NULL; }

    model_t *out = calloc(1,sizeof(model_t));
    if (!out) { ResetBCH(&bch); return NULL; }
    out->meshes = calloc(meshes_cnt,sizeof(mesh_t));
    if (!out->meshes) { free(out); ResetBCH(&bch); return NULL; }

    for ( uint mi = 0; mi < meshes_cnt; mi++ )
    {
	const u32 me = meshes_ptr + mi*0x38;
	if ( (u64)me + 0x38 > bsize ) break;

	pica_state_t vst;
	memset(&vst,0,sizeof(vst));
	pica_replay(&vst,b,bsize,hrd32(b+me+0x08),hrd32(b+me+0x0c));
	if ( !vst.stride || !vst.n_total || vst.n_total > 16 )
	    continue;

	// Resolve the per-vertex attribute layout from the permutation tables.
	pica_attr_t attrs[16];
	uint n_attrs = 0, voff = 0;
	for ( uint idx = 0; idx < vst.n_total; idx++ )
	{
	    // A set bit here means a fixed attribute: constant, no vertex data.
	    if ( (vst.formats >> (48+idx)) & 1 )
		continue;
	    const uint pidx = (vst.buffer_attrs >> (idx*4)) & 0xf;
	    const uint name = (vst.permutation  >> (pidx*4)) & 0xf;
	    const uint afmt = (vst.formats      >> (pidx*4)) & 0xf;
	    const uint fmt = afmt & 3, el = (afmt >> 2) + 1;
	    if ( n_attrs < 16 )
	    {
		attrs[n_attrs].name = name;
		attrs[n_attrs].fmt = fmt;
		attrs[n_attrs].elements = el;
		attrs[n_attrs].offset = voff;
		n_attrs++;
	    }
	    voff += pica_fmt_size[fmt]*el;
	}
	if ( !n_attrs || voff > vst.stride )
	    continue;

	const u64 vbase = (u64)vst.attr_loc + vst.buf_offset;

	const u32 smp = hrd32(b+me+0x10);
	const u32 smc = hrd32(b+me+0x14);
	if ( !smc || smc > 0x10000 ) continue;

	// Count the indices first so the arrays can be sized once.
	u32 total_idx = 0;
	for ( uint si = 0; si < smc; si++ )
	{
	    const u32 sm = smp + si*0x34;
	    if ( (u64)sm + 0x34 > bsize ) break;
	    pica_state_t ist;
	    memset(&ist,0,sizeof(ist));
	    pica_replay(&ist,b,bsize,hrd32(b+sm+0x2c),hrd32(b+sm+0x30));
	    total_idx += ist.n_vertices;
	}
	if ( !total_idx || total_idx > 0x1000000 ) continue;

	mesh_t *mesh = out->meshes + out->num_meshes;
	snprintf(mesh->name,sizeof(mesh->name),"mesh%u",mi);
	mesh->material_idx = -1;
	mesh->positions = calloc(total_idx,sizeof(vec3_t));
	mesh->normals   = calloc(total_idx,sizeof(vec3_t));
	mesh->texcoords = calloc(total_idx,sizeof(vec2_t));
	mesh->vertices  = calloc(total_idx,sizeof(vertex_t));
	if ( !mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices )
	{
	    free(mesh->positions); free(mesh->normals);
	    free(mesh->texcoords); free(mesh->vertices);
	    memset(mesh,0,sizeof(*mesh));
	    continue;
	}

	uint n = 0;
	bool have_n = false, have_t = false;
	for ( uint si = 0; si < smc; si++ )
	{
	    const u32 sm = smp + si*0x34;
	    if ( (u64)sm + 0x34 > bsize ) break;
	    pica_state_t ist;
	    memset(&ist,0,sizeof(ist));
	    pica_replay(&ist,b,bsize,hrd32(b+sm+0x2c),hrd32(b+sm+0x30));
	    if ( !ist.n_vertices ) continue;

	    // The high bit of the index buffer address selects 16-bit indices.
	    const bool is16 = (ist.index_addr >> 31) != 0;
	    const u64 ia = ist.index_addr & 0x7fffffff;
	    const u64 ineed = (u64)ist.n_vertices * (is16?2:1);
	    if ( ia + ineed > bsize ) continue;

	    for ( u32 k = 0; k < ist.n_vertices; k++ )
	    {
		const u32 vi = is16 ? (u32)((u16)b[ia+k*2] | (u16)b[ia+k*2+1]<<8) : b[ia+k];
		const u64 vo = vbase + (u64)vi*vst.stride;
		if ( vo + vst.stride > bsize ) continue;

		for ( uint a = 0; a < n_attrs; a++ )
		{
		    const u8 *p = b + vo + attrs[a].offset;
		    const uint el = attrs[a].elements;
		    if ( attrs[a].name == PICA_ATTR_POSITION )
		    {
			mesh->positions[n].x = pica_read(p,attrs[a].fmt,0);
			mesh->positions[n].y = el>1 ? pica_read(p,attrs[a].fmt,1) : 0;
			mesh->positions[n].z = el>2 ? pica_read(p,attrs[a].fmt,2) : 0;
		    }
		    else if ( attrs[a].name == PICA_ATTR_NORMAL )
		    {
			mesh->normals[n].x = pica_read(p,attrs[a].fmt,0);
			mesh->normals[n].y = el>1 ? pica_read(p,attrs[a].fmt,1) : 0;
			mesh->normals[n].z = el>2 ? pica_read(p,attrs[a].fmt,2) : 0;
			have_n = true;
		    }
		    else if ( attrs[a].name == PICA_ATTR_TEXCOORD0 )
		    {
			mesh->texcoords[n].u = pica_read(p,attrs[a].fmt,0);
			mesh->texcoords[n].v = el>1 ? pica_read(p,attrs[a].fmt,1) : 0;
			have_t = true;
		    }
		}
		mesh->vertices[n].position_idx = (int)n;
		mesh->vertices[n].normal_idx   = have_n ? (int)n : -1;
		mesh->vertices[n].texcoord_idx = have_t ? (int)n : -1;
		n++;
	    }
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

    ResetBCH(&bch);
    if (!out->num_meshes)
    {
	FreeModel(out);
	return NULL;
    }
    return out;
}
