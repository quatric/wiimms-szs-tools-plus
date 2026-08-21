// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-hsf.h"
#include "lib-model-dae.h"

//-----------------------------------------------------------------------------
///////////////		HSFV037 static geometry decode			///////////////
//-----------------------------------------------------------------------------
//
// HSFV037 is HAL Laboratory's tool-export model format (their "sysdolphin"
// GX runtime -- the same JObj/DObj/PObj/MObj/TObj object model documented in
// the public Kirby Air Ride decompilation, doldecomp/kar). Unlike that
// runtime's own .dat archives (which use a pointer-relocation table),
// HSFV037 files carry a flat top-level table right after the magic: 20
// entries of { u32 offset; u32 count; }, big-endian, each pointing at one
// class of scene data (attributes, materials, vertex positions, normals,
// display-list faces, joints/objects, textures, ..., ending in a
// NUL-separated name string table). A count of 0 means "section absent";
// a count of 1 means "a single data block lives at offset"; a count > 1
// means the section holds an array of *sub-blocks* (per-material, per-joint,
// ...) whose individual sizes are not recorded in the top table -- decoding
// those requires walking the object graph (joints/materials/display lists)
// to find each sub-block boundary, which multi-part / skinned models (every
// retail character model seen so far) need and this decoder does not yet
// do. Confirmed empirically against both a synthetic single-object test
// file (table entries at indices 4/5/7 name themselves "cube_vtxs",
// "cube_nrms", "cube_faces" via the trailing string table) and a real
// retail Mario Party 4 model (Luigi) whose equivalent entries all have
// count > 1.
//
// What's decoded here: the count==1 case only -- table index 4 (vertex
// positions) and table index 7 (triangle faces), which is enough for
// simple, single-piece models. Table index 4's data is a 32-byte sub-header
// (word[1] = vertex count) followed by that many big-endian float32 XYZ
// triples. Table index 7's data is a 16-byte sub-header (word[1] = triangle
// count) followed by that many 48-byte per-triangle records: 3 corners of
// 4 big-endian u16 each (vertex_idx, unused, normal_idx, color_idx), then
// two f32 and a trailing u32 whose meaning wasn't needed for positions-only
// output. Materials, textures, joints/skinning and multi-block sections are
// out of scope for this pass, same as the still-unported Excite .mod format.

#define HSF_MAGIC       "HSFV037"
#define HSF_NUM_ENTRIES  20
#define HSF_IDX_POSITIONS 4
#define HSF_IDX_FACES      7

static inline u32 hsf_be32 ( const u8 *p )
{
    return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3];
}

static inline float hsf_bef32 ( const u8 *p )
{
    u32 bits = hsf_be32(p);
    float f;
    memcpy(&f,&bits,4);
    return f;
}

static inline u16 hsf_be16 ( const u8 *p )
{
    return (u16)p[0]<<8 | p[1];
}

enumError DecodeHSF ( const u8 *data, uint size, ccp out_dae_path )
{
    if ( !data || size < 8 + HSF_NUM_ENTRIES*8 || memcmp(data,HSF_MAGIC,7) )
	return ERR_NOTHING_TO_DO;

    u32 entry_off[HSF_NUM_ENTRIES], entry_cnt[HSF_NUM_ENTRIES];
    const u8 *table = data + 8;
    for ( int i = 0; i < HSF_NUM_ENTRIES; i++ )
    {
	entry_off[i] = hsf_be32(table+i*8);
	entry_cnt[i] = hsf_be32(table+i*8+4);
    }

    // Only the single-block case is supported (see the file header comment).
    if ( entry_cnt[HSF_IDX_POSITIONS] != 1 || entry_cnt[HSF_IDX_FACES] != 1 )
	return ERR_NOTHING_TO_DO;

    const u32 pos_off = entry_off[HSF_IDX_POSITIONS];
    const u32 face_off = entry_off[HSF_IDX_FACES];
    if ( pos_off + 32 > size || face_off + 16 > size )
	return ERR_NOTHING_TO_DO;

    const u32 n_verts = hsf_be32(data+pos_off+4);
    if ( !n_verts || pos_off + 32 + (u64)n_verts*12 > size )
	return ERR_NOTHING_TO_DO;

    const u32 n_tris = hsf_be32(data+face_off+4);
    if ( !n_tris || face_off + 16 + (u64)n_tris*48 > size )
	return ERR_NOTHING_TO_DO;

    model_t model; memset(&model,0,sizeof(model));
    mesh_t mesh; memset(&mesh,0,sizeof(mesh));
    snprintf(mesh.name,sizeof(mesh.name),"hsf_model");
    mesh.material_idx = -1;

    mesh.positions = MALLOC(n_verts*sizeof(vec3_t));
    mesh.num_positions = n_verts;
    const u8 *pp = data + pos_off + 32;
    for ( u32 i = 0; i < n_verts; i++, pp += 12 )
    {
	mesh.positions[i].x = hsf_bef32(pp);
	mesh.positions[i].y = hsf_bef32(pp+4);
	mesh.positions[i].z = hsf_bef32(pp+8);
    }

    mesh.vertices = MALLOC(n_tris*3*sizeof(vertex_t));
    mesh.num_vertices = n_tris*3;
    const u8 *fp = data + face_off + 16;
    for ( u32 t = 0; t < n_tris; t++, fp += 48 )
    {
	for ( int c = 0; c < 3; c++ )
	{
	    const u32 vtx_idx = hsf_be16(fp+c*8);
	    vertex_t *v = mesh.vertices + t*3 + c;
	    v->position_idx = vtx_idx < n_verts ? (int)vtx_idx : 0;
	    v->normal_idx = v->texcoord_idx = v->matrix_idx = -1;
	    v->color_idx[0] = v->color_idx[1] = -1;
	    for ( int k = 0; k < 7; k++ ) v->extra_texcoord_idx[k] = -1;
	}
    }

    model.meshes = &mesh;
    model.num_meshes = 1;

    const int rc = ExportModelToDAE(&model,out_dae_path);

    FREE(mesh.positions);
    FREE(mesh.vertices);
    return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}
