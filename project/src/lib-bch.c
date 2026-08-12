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
