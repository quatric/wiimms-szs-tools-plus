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
    int position_idx;
    int normal_idx;
    int texcoord_idx;
} vertex_t;

typedef struct {
    char name[64];
    
    vec3_t *positions;
    size_t num_positions;
    
    vec3_t *normals;
    size_t num_normals;
    
    vec2_t *texcoords;
    size_t num_texcoords;
    
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
} joint_t;

typedef struct {
    char name[64];
    char textures[8][64]; // texture layer names, in declaration order
    int num_textures;
} material_t;

typedef struct {
    mesh_t *meshes;
    size_t num_meshes;
    
    joint_t *joints;
    size_t num_joints;
    
    material_t *materials;
    size_t num_materials;
} model_t;

#ifdef __cplusplus
extern "C" {
#endif

int ExportModelToDAE(const model_t *model, const char *out_xml_file);

#ifdef __cplusplus
}
#endif

#endif // LIB_MODEL_DAE_H
