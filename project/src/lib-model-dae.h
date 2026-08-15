#ifndef LIB_MODEL_DAE_H
#define LIB_MODEL_DAE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    float u, v;
} vec2_t;

typedef struct {
    float r, g, b, a;
} color4_t;

typedef struct {
    int position_idx;
    int normal_idx;
    int texcoord_idx;
    int matrix_idx;
    int color_idx[2];
    int extra_texcoord_idx[7];
} vertex_t;

typedef struct {
    int   bone_idx;   // index into model_t::joints
    float weight;
} influence_t;

// Skin influences for one GX matrix-node id. A single-bone node carries one
// entry with weight 1; a NodeMix influence carries its full weight list.
typedef struct {
    influence_t *weights;
    size_t       num_weights;
} node_influence_t;

typedef struct {
    char name[64];
    
    vec3_t *positions;
    size_t num_positions;
    
    vec3_t *normals;
    size_t num_normals;
    
    vec2_t *texcoords;
    size_t num_texcoords;

    color4_t *colors[2];
    size_t num_colors[2];

    vec2_t *extra_texcoords[7];
    size_t num_extra_texcoords[7];
    
    // For each entry of `positions`, the GX matrix-node id its source vertex
    // was bound to (-1 when the mesh is unbound). Deduplication in the bind
    // pass is keyed on (node,index), so this is well defined per position and
    // is what the COLLADA skin controller weights each vertex with.
    int *position_node;

    vertex_t *vertices;
    size_t num_vertices;
    
    int material_idx;
} mesh_t;

typedef struct {
    char name[64];
    int parent_idx;
    vec3_t translate;
    vec3_t rotate;
    vec3_t scale;
    // MDL0 stores each bone's absolute bind matrix and its inverse as 3x4
    // row-major affine matrices. COLLADA skinning needs the inverse; the
    // forward matrix is authoritative for bones whose scale/rotation cannot
    // be expressed by a plain inherited TRS chain (segment scale compensate).
    float bind[12];
    float inverse_bind[12];
    uint8_t has_inverse_bind;
} joint_t;

typedef struct {
    char name[64];
    char textures[8][64]; // texture layer names, in declaration order
    int texture_coord[8]; // GX texgen UV source for each texture layer
    // GX sampler state per layer, straight from MDL0TextureRef:
    // wrap 0=clamp 1=repeat 2=mirror, filter 0=nearest 1=linear.
    uint8_t wrap_s[8], wrap_t[8];
    uint8_t min_filter[8], mag_filter[8];
    int num_textures;
} material_t;

typedef struct {
    mesh_t *meshes;
    size_t num_meshes;
    
    joint_t *joints;
    size_t num_joints;
    
    material_t *materials;
    size_t num_materials;

    // Indexed by GX matrix-node id; empty entries mean "not a skinned node".
    node_influence_t *node_influences;
    size_t num_node_influences;
} model_t;

#ifdef __cplusplus
extern "C" {
#endif

int ExportModelToDAE(const model_t *model, const char *out_xml_file);

// Parse a COLLADA DAE (.dae) file or in-memory XML string into a model_t.
model_t* ParseDAE(const char *xml_data, size_t xml_size);
model_t* ParseDAEFile(const char *filename);
void FreeModel(model_t *model);

// Configure an optional tree-wide PNG lookup used by recursive archive
// extraction. Pass NULL to release the index and restore standalone lookup.
void SetDAETextureSearchRoot(const char *root);

#ifdef __cplusplus
}
#endif

#endif // LIB_MODEL_DAE_H
