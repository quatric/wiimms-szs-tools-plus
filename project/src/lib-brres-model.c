#include "lib-brres-model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)

typedef struct {
    char tag[4];
    uint32_t size;
    uint32_t version;
    int32_t bresOffset;
    
    int32_t defsOffset;
    int32_t bonesOffset;
    int32_t positionsOffset;
    int32_t normalsOffset;
    int32_t colorsOffset;
    int32_t uvsOffset;
    int32_t furVecsOffset;
    int32_t furPosOffset;
    int32_t materialsOffset;
    int32_t texSRTOffset;
    int32_t shadersOffset;
    int32_t meshesOffset;
    int32_t texLinksOffset;
    int32_t palettesOffset;
    int32_t userDataOffset;
    int32_t stringOffset;
} MDL0Header;

typedef struct {
    int32_t headerLen;
    int32_t mdl0Offset;
    int32_t stringOffset;
    int32_t index;

    int32_t nodeId;
    uint32_t flags;
    uint32_t bbFlags;
    uint32_t bbIndex;

    float scale[3];
    float rotation[3];
    float translation[3];
    float extents[6];

    int32_t parentOffset;
    int32_t firstChildOffset;
    int32_t nextOffset;
    int32_t prevOffset;
    int32_t userDataOffset;

    float transform[12];
    float transformInv[12];
} MDL0Bone;

typedef struct {
    int32_t dataLen;
    int32_t mdl0Offset;
    int32_t dataOffset;
    int32_t stringOffset;
    int32_t index;
    int32_t isXYZ;
    int32_t type;
    uint8_t divisor;
    uint8_t entryStride;
    uint16_t numVertices;
    float extents[6];
    int32_t pad1;
    int32_t pad2;
} MDL0VertexData;

typedef struct {
    int32_t dataLen;
    int32_t mdl0Offset;
    int32_t dataOffset;
    int32_t stringOffset;
    int32_t index;
    int32_t isNBT;
    int32_t type;
    uint8_t divisor;
    uint8_t entryStride;
    uint16_t numVertices;
} MDL0NormalData;

typedef struct {
    int32_t bufferSize;
    int32_t size;
    int32_t offset;
} PrimDataGroup;

typedef struct {
    int32_t totalLength;
    int32_t mdl0Offset;
    int32_t nodeId;

    uint32_t vertexFormatLo;
    uint32_t vertexFormatHi;
    uint32_t vertexSpecs;

    PrimDataGroup defintions;
    PrimDataGroup primitives;

    uint32_t arrayFlags;
    int32_t flag;
    int32_t stringOffset;
    int32_t index;
    int32_t numVertices;
    int32_t numFaces;

    int16_t vertexId;
    int16_t normalId;
    int16_t colorIds[2];
    int16_t uvIds[8];
} MDL0Object;

typedef struct {
    uint32_t size;
    uint32_t numNodes;
} ResourceGroup;

typedef struct {
    uint16_t id;
    uint16_t unknown;
    uint16_t leftIndex;
    uint16_t rightIndex;
    int32_t stringOffset;
    int32_t dataOffset;
} ResourceEntry;

#pragma pack(pop)

static uint16_t swap16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

static uint32_t swap32(uint32_t val) {
    return ((val << 24) & 0xff000000) |
           ((val <<  8) & 0x00ff0000) |
           ((val >>  8) & 0x0000ff00) |
           ((val >> 24) & 0x000000ff);
}

static float swapf(float val) {
    uint32_t temp;
    memcpy(&temp, &val, sizeof(float));
    temp = swap32(temp);
    memcpy(&val, &temp, sizeof(float));
    return val;
}

static void decode_gx_primitives(const uint8_t *prim_data, int32_t prim_size, uint32_t cp_lo, uint32_t cp_hi, mesh_t *mesh) {
    (void)cp_lo;
    (void)cp_hi;
    int offset = 0;
    
    // Very basic display list decoding for Triangles/Quads/Strips
    size_t capacity = 1024;
    mesh->vertices = malloc(capacity * sizeof(vertex_t));
    mesh->num_vertices = 0;

    // A complete vertex codec requires CP settings, but a naive pass assuming indexed positions/normals/uvs:
    while (offset < prim_size) {
        uint8_t opcode = prim_data[offset++];
        if (opcode == 0) break;
        
        uint8_t prim_type = opcode & 0xF8;
        // 0x90 = Triangles, 0x98 = Triangle Strip, 0x80 = Quads
        
        if (prim_type != 0x90 && prim_type != 0x98 && prim_type != 0x80 && prim_type != 0xA0) {
            break;
        }

        if (offset + 2 > prim_size) break;
        uint16_t count = (prim_data[offset] << 8) | prim_data[offset+1];
        offset += 2;

        int stride = 6; // pos_id(2) + norm_id(2) + uv0_id(2)

        // This is a dummy implementation that assumes triangles
        for (int i = 0; i < count; i++) {
            if (offset + stride > prim_size) goto done;
            uint16_t pid = (prim_data[offset] << 8) | prim_data[offset+1]; offset += 2;
            uint16_t nid = (prim_data[offset] << 8) | prim_data[offset+1]; offset += 2;
            uint16_t tid = (prim_data[offset] << 8) | prim_data[offset+1]; offset += 2;

            if (mesh->num_vertices + 3 >= capacity) {
                capacity *= 2;
                mesh->vertices = realloc(mesh->vertices, capacity * sizeof(vertex_t));
            }
            
            mesh->vertices[mesh->num_vertices].position_idx = pid;
            mesh->vertices[mesh->num_vertices].normal_idx = nid;
            mesh->vertices[mesh->num_vertices].texcoord_idx = tid;
            mesh->num_vertices++;
        }
    }
    done:;
}

// Every offset below comes straight from untrusted file bytes and is used
// as raw pointer arithmetic into `data`/mmap'd storage. Bounds-check each
// one before dereferencing -- an out-of-range offset previously walked off
// the mapped region and raised SIGBUS (bus error) instead of failing
// cleanly, e.g. on malformed/unusual MDL0 bones or mesh groups.
static int in_bounds(const uint8_t *base, size_t size, const void *ptr, size_t len) {
    if ((const uint8_t*)ptr < base) return 0;
    size_t off = (const uint8_t*)ptr - base;
    return off <= size && len <= size - off;
}

// Resolves a length-prefixed pooled string from a 4-byte big-endian offset
// field. `resolve_base` is what the offset is added to: the outer
// ResourceHeader convention (name/index) is struct-relative (resolve_base =
// start of the owning struct), but layer/texture-name fields deeper inside
// a Material use a different, self-relative convention (resolve_base =
// address of the offset field itself) -- verified against a real retail
// BRRES (SpinningRoom_01.brres / MT_L_map material). A zero offset means
// "absent". Returns NULL if the field, length prefix, or string body would
// read outside [base,base+size), or the bytes aren't printable ASCII.
static const char* read_pooled_string(const uint8_t *base, size_t size,
                                       const uint8_t *field_ptr, const uint8_t *resolve_base,
                                       uint32_t *out_len) {
    if (!in_bounds(base, size, field_ptr, 4)) return NULL;
    int32_t rel = (int32_t)swap32(*(const uint32_t*)field_ptr);
    if (!rel) return NULL;
    const uint8_t *target = resolve_base + rel;
    if (!in_bounds(base, size, target - 4, 4)) return NULL;
    uint32_t len = swap32(*(const uint32_t*)(target - 4));
    if (!len || len > 255 || !in_bounds(base, size, target, len)) return NULL;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = target[i];
        if (c < 0x20 || c > 0x7e) return NULL;
    }
    if (out_len) *out_len = len;
    return (const char*)target;
}

static void copy_pooled_string(char *dst, size_t dst_size, const char *src, uint32_t len) {
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

// Materials store the texture-layer names somewhere in a shader/TEV config
// block whose exact fixed layout we haven't fully reverse-engineered, but
// every genuine layer-texture-name field is a self-relative pooled-string
// offset, so a false positive (some other int32 that happens to also
// resolve to a valid printable pooled string) is effectively impossible.
// Scan every aligned dword in the material body and collect the unique
// texture names it references, in order, up to `max_layers`.
static void scan_material_textures(const uint8_t *data, size_t size,
                                    const uint8_t *mat_base, int32_t mat_len,
                                    material_t *mat, const char *skip_name) {
    int max_layers = (int)(sizeof(mat->textures) / sizeof(mat->textures[0]));
    for (int32_t rel = 0; rel + 4 <= mat_len && mat->num_textures < max_layers; rel += 4) {
        const uint8_t *field = mat_base + rel;
        uint32_t len;
        const char *s = read_pooled_string(data, size, field, field, &len);
        if (!s) continue;
        if (skip_name && len == strlen(skip_name) && !memcmp(s, skip_name, len)) continue;

        int dup = 0;
        for (int i = 0; i < mat->num_textures; i++)
            if ((uint32_t)strlen(mat->textures[i]) == len && !memcmp(mat->textures[i], s, len)) { dup = 1; break; }
        if (dup) continue;

        copy_pooled_string(mat->textures[mat->num_textures], sizeof(mat->textures[0]), s, len);
        mat->num_textures++;
    }
}

model_t* ParseMDL0(const uint8_t *data, size_t size) {
    if (!data || size < sizeof(MDL0Header)) return NULL;

    MDL0Header *hdr = (MDL0Header*)data;
    if (strncmp(hdr->tag, "MDL0", 4) != 0) return NULL;

    model_t *model = calloc(1, sizeof(model_t));

    // Parse Bones
    if (hdr->bonesOffset) {
        int32_t bOffset = swap32(hdr->bonesOffset);
        if (bOffset < 0 || !in_bounds(data, size, data + bOffset, sizeof(ResourceGroup))) goto skip_bones;
        ResourceGroup *grp = (ResourceGroup*)(data + bOffset);
        int32_t numBones = swap32(grp->numNodes);
        if (numBones < 0 || !in_bounds(data, size, grp + 1, (size_t)(numBones + 1) * sizeof(ResourceEntry))) goto skip_bones;
        model->num_joints = numBones;
        model->joints = calloc(numBones, sizeof(joint_t));

        ResourceEntry *entries = (ResourceEntry*)(grp + 1);
        for (int i = 1; i <= numBones; i++) {
            int32_t dOffset = swap32(entries[i].dataOffset);
            MDL0Bone *bNode = (MDL0Bone*)((uint8_t*)grp + dOffset);
            if (dOffset < 0 || !in_bounds(data, size, bNode, sizeof(MDL0Bone))) continue;

            // joint_t doesn't have id, parentOffset, etc in lib-model-dae.h!
            // Wait, joint_t has: name, parent_idx, translate, rotate, scale
            model->joints[i-1].parent_idx = -1; // stub

            model->joints[i-1].scale.x = swapf(bNode->scale[0]);
            model->joints[i-1].scale.y = swapf(bNode->scale[1]);
            model->joints[i-1].scale.z = swapf(bNode->scale[2]);

            model->joints[i-1].rotate.x = swapf(bNode->rotation[0]);
            model->joints[i-1].rotate.y = swapf(bNode->rotation[1]);
            model->joints[i-1].rotate.z = swapf(bNode->rotation[2]);

            model->joints[i-1].translate.x = swapf(bNode->translation[0]);
            model->joints[i-1].translate.y = swapf(bNode->translation[1]);
            model->joints[i-1].translate.z = swapf(bNode->translation[2]);
        }
    }
    skip_bones:

    // Parse Materials (name + referenced texture layer names, so mesh
    // exporters can actually emit/bind the textures a model needs).
    if (hdr->materialsOffset) {
        int32_t maOffset = swap32(hdr->materialsOffset);
        if (maOffset < 0 || !in_bounds(data, size, data + maOffset, sizeof(ResourceGroup))) goto skip_materials;
        ResourceGroup *grp = (ResourceGroup*)(data + maOffset);
        int32_t numMats = swap32(grp->numNodes);
        if (numMats < 0 || !in_bounds(data, size, grp + 1, (size_t)(numMats + 1) * sizeof(ResourceEntry))) goto skip_materials;
        model->num_materials = numMats;
        model->materials = calloc(numMats, sizeof(material_t));

        ResourceEntry *entries = (ResourceEntry*)(grp + 1);
        for (int i = 1; i <= numMats; i++) {
            int32_t dOffset = swap32(entries[i].dataOffset);
            const uint8_t *matBase = (uint8_t*)grp + dOffset;
            // Material headers start with { length, mdl0Offset, stringOffset, index, ... }.
            if (dOffset < 0 || !in_bounds(data, size, matBase, 16)) continue;

            int32_t matLen = (int32_t)swap32(*(const uint32_t*)matBase);
            if (matLen < 16 || !in_bounds(data, size, matBase, (size_t)matLen)) matLen = 16;

            material_t *mat = &model->materials[i-1];
            uint32_t nlen;
            const char *nm = read_pooled_string(data, size, matBase + 8, matBase, &nlen);
            if (nm) copy_pooled_string(mat->name, sizeof(mat->name), nm, nlen);

            scan_material_textures(data, size, matBase, matLen, mat, nm ? mat->name : NULL);
        }
    }
    skip_materials:

    // Parse Meshes
    if (hdr->meshesOffset) {
        int32_t mOffset = swap32(hdr->meshesOffset);
        if (mOffset < 0 || !in_bounds(data, size, data + mOffset, sizeof(ResourceGroup))) goto skip_meshes;
        ResourceGroup *grp = (ResourceGroup*)(data + mOffset);
        int32_t numMeshes = swap32(grp->numNodes);
        if (numMeshes < 0 || !in_bounds(data, size, grp + 1, (size_t)(numMeshes + 1) * sizeof(ResourceEntry))) goto skip_meshes;
        model->num_meshes = numMeshes;
        model->meshes = calloc(numMeshes, sizeof(mesh_t));

        ResourceEntry *entries = (ResourceEntry*)(grp + 1);
        for (int i = 1; i <= numMeshes; i++) {
            int32_t dOffset = swap32(entries[i].dataOffset);
            MDL0Object *oNode = (MDL0Object*)((uint8_t*)grp + dOffset);
            if (dOffset < 0 || !in_bounds(data, size, oNode, sizeof(MDL0Object))) continue;

            uint32_t cpLo = swap32(oNode->vertexFormatLo);
            uint32_t cpHi = swap32(oNode->vertexFormatHi);
            int32_t primOffset = swap32(oNode->primitives.offset);
            int32_t primSize = swap32(oNode->primitives.size);

            const uint8_t *primData = (const uint8_t*)&oNode->primitives + primOffset;
            if (primOffset < 0 || primSize < 0 || !in_bounds(data, size, primData, (size_t)primSize)) continue;
            decode_gx_primitives(primData, primSize, cpLo, cpHi, &model->meshes[i-1]);

            // Real mesh->material binding lives in the MDL0 draw-op node
            // tree, which isn't parsed here. When there's exactly one
            // material every mesh must use it; otherwise leave unbound
            // rather than guess wrong.
            model->meshes[i-1].material_idx = model->num_materials == 1 ? 0 : -1;
        }
    }
    skip_meshes:

    return model;
}

void FreeModel(model_t *model) {
    if (!model) return;
    if (model->joints) free(model->joints);
    
    if (model->meshes) {
        for (size_t i = 0; i < model->num_meshes; i++) {
            if (model->meshes[i].positions) free(model->meshes[i].positions);
            if (model->meshes[i].normals) free(model->meshes[i].normals);
            if (model->meshes[i].texcoords) free(model->meshes[i].texcoords);
            if (model->meshes[i].vertices) {
                free(model->meshes[i].vertices);
            }
        }
        free(model->meshes);
    }
    if (model->materials) free(model->materials);
    free(model);
}
