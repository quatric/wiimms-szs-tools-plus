#include "lib-brres-inject.h"
#include "lib-szs.h"
#include "lib-brres.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma pack(push, 1)

typedef struct {
    char magic[4];       // "bres"
    uint16_t endian;     // 0xfeff
    uint16_t version;    // 0
    uint32_t fileSize;
    uint16_t headerSize; // 0x10
    uint16_t numSections;// number of root sections (usually 1 or 2)
} BRESHeader;

typedef struct {
    char tag[4];         // "root"
    uint32_t size;
} BRESSectionHeader;

typedef struct {
    char tag[4];         // "MDL0"
    uint32_t size;
    uint32_t version;    // 8, 9, 10, 11
    int32_t bresOffset;  // negative offset to BRES header or 0
    int32_t sectionOffsets[14];
} MDL0Header;

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
} MDL0VertexHeader;

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
} MDL0NormalHeader;

typedef struct {
    int32_t dataLen;
    int32_t mdl0Offset;
    int32_t dataOffset;
    int32_t stringOffset;
    int32_t index;
    int32_t isRGBA;
    int32_t format;
    uint8_t entryStride;
    uint8_t pad;
    uint16_t numEntries;
} MDL0ColorHeader;

typedef struct {
    int32_t dataLen;
    int32_t mdl0Offset;
    int32_t dataOffset;
    int32_t stringOffset;
    int32_t index;
    int32_t isST;
    int32_t format;
    uint8_t divisor;
    uint8_t entryStride;
    uint16_t numEntries;
    float min[2];
    float max[2];
    int32_t pad[4];
} MDL0UVHeader;

typedef struct {
    int32_t bufferSize;
    int32_t size;
    int32_t offset;
} PrimGroup;

typedef struct {
    int32_t totalLength;
    int32_t mdl0Offset;
    int32_t nodeId;
    uint32_t vertexFormatLo;
    uint32_t vertexFormatHi;
    uint32_t vertexSpecs;
    PrimGroup defintions;
    PrimGroup primitives;
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
} MDL0ObjHeader;

typedef struct {
    uint32_t size;
    uint32_t numNodes;
} ResGroup;

typedef struct {
    uint16_t id;
    uint16_t flag;
    uint16_t leftIndex;
    uint16_t rightIndex;
    int32_t stringOffset;
    int32_t dataOffset;
} ResEntry;

#pragma pack(pop)

static inline uint16_t to_be16(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint32_t to_be32(uint32_t v) {
    return ((v << 24) & 0xff000000) | ((v << 8) & 0x00ff0000) |
           ((v >> 8) & 0x0000ff00) | ((v >> 24) & 0x000000ff);
}
static inline float to_bef(float v) {
    uint32_t t; memcpy(&t, &v, 4); t = to_be32(t);
    float out; memcpy(&out, &t, 4); return out;
}

static size_t align32(size_t n) { return (n + 31) & ~31; }
static size_t align4(size_t n) { return (n + 3) & ~3; }

// Patricia binary string table entry
typedef struct patricia_node {
    char name[128];
    int id;
    int index;
    struct patricia_node *left;
    struct patricia_node *right;
} patricia_node_t;

static int compare_bits(int b1, int b2)
{
    for (int i = 8, b = 0x80; i-- != 0; b >>= 1) {
        if ((b1 & b) != (b2 & b)) return i;
    }
    return 0;
}

static bool node_is_right(const patricia_node_t *node, const patricia_node_t *entry)
{
    size_t nlen = strlen(node->name);
    size_t elen = strlen(entry->name);
    if (nlen != elen) return false;
    int bit_idx = node->id;
    size_t char_idx = (size_t)(bit_idx >> 3);
    if (char_idx >= elen) return false;
    return ((entry->name[char_idx] >> (bit_idx & 7)) & 1) != 0;
}

static int node_generate_id(patricia_node_t *entry, patricia_node_t *comp)
{
    size_t elen = strlen(entry->name);
    size_t clen = strlen(comp->name);
    size_t len = elen < clen ? elen : clen;
    for (int i = (int)len; i-- > 0;) {
        if (entry->name[i] != comp->name[i]) {
            entry->id = (i << 3) | compare_bits(entry->name[i], comp->name[i]);
            if (node_is_right(entry, comp)) {
                entry->left = entry;
                entry->right = comp;
            } else {
                entry->left = comp;
                entry->right = entry;
            }
            return entry->id;
        }
    }
    return 0;
}

static void patricia_insert_left(patricia_node_t *prev, patricia_node_t *entry)
{
    if (node_is_right(entry, prev->left))
        entry->right = prev->left;
    else
        entry->left = prev->left;
    prev->left = entry;
}

static void patricia_insert_right(patricia_node_t *prev, patricia_node_t *entry)
{
    if (node_is_right(entry, prev->right))
        entry->right = prev->right;
    else
        entry->left = prev->right;
    prev->right = entry;
}

// Build a BRES ResourceGroup buffer containing names, Patricia binary search entries, and string table
static uint8_t *build_resource_group(size_t num_items, const char **names, const int32_t *data_offsets, size_t *out_group_size)
{
    size_t total_nodes = num_items + 1;
    patricia_node_t *nodes = CALLOC(total_nodes, sizeof(patricia_node_t));
    if (!nodes) return NULL;

    // Root node
    nodes[0].name[0] = 0;
    nodes[0].id = -1;
    nodes[0].index = 0;
    nodes[0].left = &nodes[0];
    nodes[0].right = &nodes[0];

    for (size_t i = 0; i < num_items; i++) {
        patricia_node_t *entry = &nodes[i + 1];
        snprintf(entry->name, sizeof(entry->name), "%s", names[i]);
        entry->index = (int)(i + 1);
        entry->left = entry;
        entry->right = entry;
        size_t slen = strlen(entry->name);
        entry->id = slen ? (((int)(slen - 1) << 3) | compare_bits(entry->name[slen - 1], 0)) : -1;

        // Traverse
        patricia_node_t *current = nodes[0].left, *prev = &nodes[0];
        bool is_right = false;
        while (entry->id <= current->id) {
            if (entry->id == current->id)
                node_generate_id(entry, current);
            is_right = node_is_right(current, entry);
            prev = current;
            current = is_right ? current->right : current->left;
            if (prev->id <= current->id) break;
        }
        if (is_right)
            patricia_insert_right(prev, entry);
        else
            patricia_insert_left(prev, entry);
    }

    // Compute size: ResGroup (8) + total_nodes * ResEntry (16) + strings
    size_t header_and_entries = sizeof(ResGroup) + total_nodes * sizeof(ResEntry);
    size_t strings_size = 0;
    for (size_t i = 0; i < num_items; i++) {
        size_t slen = strlen(names[i]);
        strings_size += 4 + align4(slen + 1); // 4-byte length prefix + null-terminated string
    }

    size_t group_size = align32(header_and_entries + strings_size);
    uint8_t *buf = CALLOC(1, group_size);
    if (!buf) { FREE(nodes); return NULL; }

    ResGroup *grp = (ResGroup*)buf;
    grp->size = to_be32((uint32_t)group_size);
    grp->numNodes = to_be32((uint32_t)num_items);

    ResEntry *entries = (ResEntry*)(buf + sizeof(ResGroup));
    uint8_t *str_cursor = buf + header_and_entries;

    // Root entry
    entries[0].id = to_be16(0xffff);
    entries[0].flag = 0;
    entries[0].leftIndex = to_be16((uint16_t)nodes[0].left->index);
    entries[0].rightIndex = to_be16((uint16_t)nodes[0].right->index);
    entries[0].stringOffset = 0;
    entries[0].dataOffset = 0;

    for (size_t i = 0; i < num_items; i++) {
        ResEntry *e = &entries[i + 1];
        patricia_node_t *n = &nodes[i + 1];
        e->id = to_be16((uint16_t)n->id);
        e->flag = 0;
        e->leftIndex = to_be16((uint16_t)n->left->index);
        e->rightIndex = to_be16((uint16_t)n->right->index);

        // Write string: 4-byte big-endian len + string
        size_t slen = strlen(names[i]);
        int32_t str_offset = (int32_t)(str_cursor - buf);
        *(uint32_t*)str_cursor = to_be32((uint32_t)slen);
        memcpy(str_cursor + 4, names[i], slen + 1);
        str_cursor += 4 + align4(slen + 1);

        e->stringOffset = to_be32((uint32_t)str_offset);
        e->dataOffset = to_be32((uint32_t)data_offsets[i]);
    }

    FREE(nodes);
    *out_group_size = group_size;
    return buf;
}

// Extract pooled string from MDL0
static const char *get_mdl0_string(const uint8_t *mdl0, size_t size, int32_t str_offset_field, const uint8_t *base)
{
    if (!str_offset_field) return "";
    const uint8_t *ptr = base + str_offset_field;
    if (ptr < mdl0 || ptr + 4 > mdl0 + size) return "";
    uint32_t len = to_be32(*(const uint32_t*)ptr);
    if (ptr + 4 + len > mdl0 + size) return "";
    return (const char*)(ptr + 4);
}

int InjectDAEIntoMDL0(const uint8_t *mdl0_data, size_t mdl0_size,
                      const model_t *dae_model,
                      uint8_t **out_data, size_t *out_size)
{
    if (!mdl0_data || mdl0_size < sizeof(MDL0Header) || !dae_model || !out_data || !out_size)
        return 0;

    if (memcmp(mdl0_data, "MDL0", 4)) return 0;
    const MDL0Header *in_hdr = (const MDL0Header*)mdl0_data;
    uint32_t version = to_be32(in_hdr->version);
    int32_t in_size = to_be32(in_hdr->size);
    if (in_size <= 0 || (size_t)in_size > mdl0_size) in_size = (int32_t)mdl0_size;

    // Offsets in parent MDL0
    int32_t off_defs = to_be32(in_hdr->sectionOffsets[0]);
    int32_t off_bones = to_be32(in_hdr->sectionOffsets[1]);
    int32_t off_mat = to_be32(in_hdr->sectionOffsets[8]);
    int32_t off_shd = to_be32(in_hdr->sectionOffsets[9]);
    int32_t off_obj = to_be32(in_hdr->sectionOffsets[10]);
    int32_t off_tex = to_be32(in_hdr->sectionOffsets[11]);
    int32_t off_plt = to_be32(in_hdr->sectionOffsets[12]);
    int32_t off_usr = (version >= 10 && in_hdr->sectionOffsets[13]) ? to_be32(in_hdr->sectionOffsets[13]) : 0;

    // Read parent objects to know how many objects exist
    if (off_obj <= 0 || off_obj + sizeof(ResGroup) > (size_t)in_size) return 0;
    const ResGroup *obj_grp = (const ResGroup*)(mdl0_data + off_obj);
    uint32_t num_objs = to_be32(obj_grp->numNodes);
    if (!num_objs || num_objs > 256) return 0;

    const ResEntry *obj_entries = (const ResEntry*)(obj_grp + 1);

    // Buffers for new sections
    // For each object: build new Position, Normal, UV, and Object Primitives buffers
    typedef struct {
        char name[128];
        uint8_t *pos_buf; size_t pos_size;
        uint8_t *nrm_buf; size_t nrm_size;
        uint8_t *uv_buf;  size_t uv_size;
        uint8_t *obj_buf; size_t obj_size;
    } new_obj_data_t;

    new_obj_data_t *new_objs = CALLOC(num_objs, sizeof(new_obj_data_t));
    if (!new_objs) return 0;

    float overall_min[3] = { 1e9f, 1e9f, 1e9f }, overall_max[3] = { -1e9f, -1e9f, -1e9f };

    for (uint32_t i = 0; i < num_objs; i++) {
        int32_t o_off = to_be32(obj_entries[i + 1].dataOffset);
        const MDL0ObjHeader *orig_obj = (const MDL0ObjHeader*)((const uint8_t*)obj_grp + o_off);
        const char *obj_name = get_mdl0_string(mdl0_data, in_size, orig_obj->stringOffset, (const uint8_t*)orig_obj);
        if (!*obj_name) obj_name = get_mdl0_string(mdl0_data, in_size, obj_entries[i+1].stringOffset, (const uint8_t*)obj_grp);
        snprintf(new_objs[i].name, sizeof(new_objs[i].name), "%s", obj_name);

        // Find matching mesh in dae_model
        const mesh_t *match_mesh = NULL;
        for (size_t m = 0; m < dae_model->num_meshes; m++) {
            if (!strcmp(dae_model->meshes[m].name, obj_name)) {
                match_mesh = &dae_model->meshes[m];
                break;
            }
        }
        if (!match_mesh && dae_model->num_meshes == num_objs)
            match_mesh = &dae_model->meshes[i];
        if (!match_mesh && dae_model->num_meshes == 1)
            match_mesh = &dae_model->meshes[0];

        if (match_mesh && match_mesh->num_vertices > 0 && match_mesh->num_positions > 0) {
            // 1. Build Position Buffer (MDL0VertexHeader + floats)
            size_t pos_data_len = match_mesh->num_positions * 12;
            size_t pos_total_len = align32(0x40 + pos_data_len);
            uint8_t *pbuf = CALLOC(1, pos_total_len);
            MDL0VertexHeader *vh = (MDL0VertexHeader*)pbuf;
            vh->dataLen = to_be32((uint32_t)pos_total_len);
            vh->dataOffset = to_be32(0x40);
            vh->index = to_be32(i);
            vh->isXYZ = to_be32(1);
            vh->type = to_be32(4); // GX_F32
            vh->divisor = 0;
            vh->entryStride = 12;
            vh->numVertices = to_be16((uint16_t)match_mesh->num_positions);

            float pmin[3] = { 1e9f, 1e9f, 1e9f }, pmax[3] = { -1e9f, -1e9f, -1e9f };
            float *pos_out = (float*)(pbuf + 0x40);
            for (size_t p = 0; p < match_mesh->num_positions; p++) {
                float x = match_mesh->positions[p].x;
                float y = match_mesh->positions[p].y;
                float z = match_mesh->positions[p].z;
                if (x < pmin[0]) pmin[0] = x; if (x > pmax[0]) pmax[0] = x;
                if (y < pmin[1]) pmin[1] = y; if (y > pmax[1]) pmax[1] = y;
                if (z < pmin[2]) pmin[2] = z; if (z > pmax[2]) pmax[2] = z;
                pos_out[p * 3 + 0] = to_bef(x);
                pos_out[p * 3 + 1] = to_bef(y);
                pos_out[p * 3 + 2] = to_bef(z);
            }
            for (int c = 0; c < 3; c++) {
                vh->extents[c] = to_bef(pmin[c]);
                vh->extents[c + 3] = to_bef(pmax[c]);
                if (pmin[c] < overall_min[c]) overall_min[c] = pmin[c];
                if (pmax[c] > overall_max[c]) overall_max[c] = pmax[c];
            }
            new_objs[i].pos_buf = pbuf;
            new_objs[i].pos_size = pos_total_len;

            // 2. Build Normal Buffer (MDL0NormalHeader + floats)
            if (match_mesh->num_normals > 0) {
                size_t nrm_data_len = match_mesh->num_normals * 12;
                size_t nrm_total_len = align32(sizeof(MDL0NormalHeader) + nrm_data_len);
                uint8_t *nbuf = CALLOC(1, nrm_total_len);
                MDL0NormalHeader *nh = (MDL0NormalHeader*)nbuf;
                nh->dataLen = to_be32((uint32_t)nrm_total_len);
                nh->dataOffset = to_be32(sizeof(MDL0NormalHeader));
                nh->index = to_be32(i);
                nh->isNBT = to_be32(0);
                nh->type = to_be32(4); // GX_F32
                nh->divisor = 0;
                nh->entryStride = 12;
                nh->numVertices = to_be16((uint16_t)match_mesh->num_normals);

                float *nrm_out = (float*)(nbuf + sizeof(MDL0NormalHeader));
                for (size_t p = 0; p < match_mesh->num_normals; p++) {
                    nrm_out[p * 3 + 0] = to_bef(match_mesh->normals[p].x);
                    nrm_out[p * 3 + 1] = to_bef(match_mesh->normals[p].y);
                    nrm_out[p * 3 + 2] = to_bef(match_mesh->normals[p].z);
                }
                new_objs[i].nrm_buf = nbuf;
                new_objs[i].nrm_size = nrm_total_len;
            }

            // 3. Build UV Buffer (MDL0UVHeader + floats)
            if (match_mesh->num_texcoords > 0) {
                size_t uv_data_len = match_mesh->num_texcoords * 8;
                size_t uv_total_len = align32(sizeof(MDL0UVHeader) + uv_data_len);
                uint8_t *ubuf = CALLOC(1, uv_total_len);
                MDL0UVHeader *uh = (MDL0UVHeader*)ubuf;
                uh->dataLen = to_be32((uint32_t)uv_total_len);
                uh->dataOffset = to_be32(sizeof(MDL0UVHeader));
                uh->index = to_be32(i);
                uh->isST = to_be32(1);
                uh->format = to_be32(4); // GX_F32
                uh->divisor = 0;
                uh->entryStride = 8;
                uh->numEntries = to_be16((uint16_t)match_mesh->num_texcoords);

                float uvmin[2] = { 1e9f, 1e9f }, uvmax[2] = { -1e9f, -1e9f };
                float *uv_out = (float*)(ubuf + sizeof(MDL0UVHeader));
                for (size_t p = 0; p < match_mesh->num_texcoords; p++) {
                    float u = match_mesh->texcoords[p].u;
                    float v = 1.0f - match_mesh->texcoords[p].v; // GX top-left coordinate system
                    if (u < uvmin[0]) uvmin[0] = u; if (u > uvmax[0]) uvmax[0] = u;
                    if (v < uvmin[1]) uvmin[1] = v; if (v > uvmax[1]) uvmax[1] = v;
                    uv_out[p * 2 + 0] = to_bef(u);
                    uv_out[p * 2 + 1] = to_bef(v);
                }
                uh->min[0] = to_bef(uvmin[0]); uh->min[1] = to_bef(uvmin[1]);
                uh->max[0] = to_bef(uvmax[0]); uh->max[1] = to_bef(uvmax[1]);
                new_objs[i].uv_buf = ubuf;
                new_objs[i].uv_size = uv_total_len;
            }

            // 4. Build Display List & Object Header
            bool has_nrm = (match_mesh->num_normals > 0);
            bool has_uv = (match_mesh->num_texcoords > 0);

            // Configure standard 16-bit indices: Pos (idx16), Norm (idx16 if present), UV0 (idx16 if present)
            uint32_t new_cp_lo = 0;
            new_cp_lo |= (3 << 9); // Pos = 3 (index16)
            if (has_nrm) new_cp_lo |= (3 << 11); // Nrm = 3 (index16)

            uint32_t new_cp_hi = 0;
            if (has_uv) new_cp_hi |= 3; // Tex0 = 3 (index16)

            size_t vertex_stride = 2 + (has_nrm ? 2 : 0) + (has_uv ? 2 : 0);
            size_t num_vtx = match_mesh->num_vertices;
            size_t prim_stream_len = 1 + 2 + num_vtx * vertex_stride; // 0x90 + uint16 count + vertices
            size_t prim_padded_len = align32(prim_stream_len);

            size_t obj_total_len = align32(sizeof(MDL0ObjHeader) + prim_padded_len);
            uint8_t *obuf = CALLOC(1, obj_total_len);
            MDL0ObjHeader *oh = (MDL0ObjHeader*)obuf;

            // Copy base settings from parent object
            *oh = *orig_obj;
            oh->totalLength = to_be32((uint32_t)obj_total_len);
            oh->vertexFormatLo = to_be32(new_cp_lo);
            oh->vertexFormatHi = to_be32(new_cp_hi);
            oh->vertexSpecs = to_be32(has_uv ? 1 : 0); // 1 texcoord
            oh->primitives.bufferSize = to_be32((uint32_t)prim_padded_len);
            oh->primitives.size = to_be32((uint32_t)prim_padded_len);
            oh->primitives.offset = to_be32(sizeof(MDL0ObjHeader));
            oh->defintions.offset = to_be32(0);
            oh->defintions.size = to_be32(0);
            oh->defintions.bufferSize = to_be32(0);
            oh->numVertices = to_be32((int32_t)num_vtx);
            oh->numFaces = to_be32((int32_t)(num_vtx / 3));
            oh->vertexId = to_be16((int16_t)i);
            oh->normalId = has_nrm ? to_be16((int16_t)i) : to_be16(-1);
            oh->uvIds[0] = has_uv ? to_be16((int16_t)i) : to_be16(-1);
            for (int u = 1; u < 8; u++) oh->uvIds[u] = to_be16(-1);
            oh->colorIds[0] = to_be16(-1); oh->colorIds[1] = to_be16(-1);

            uint8_t *dl = obuf + sizeof(MDL0ObjHeader);
            dl[0] = 0x90; // GX_DRAW_TRIANGLES
            *(uint16_t*)(dl + 1) = to_be16((uint16_t)num_vtx);
            uint8_t *dl_vtx = dl + 3;

            // Triangles with reversed winding for GX hardware
            for (size_t t = 0; t < num_vtx; t += 3) {
                // Reverse (t, t+1, t+2) -> (t+2, t+1, t)
                size_t tri_indices[3] = { t + 2, t + 1, t };
                for (int v = 0; v < 3; v++) {
                    const vertex_t *vtx = &match_mesh->vertices[tri_indices[v]];
                    *(uint16_t*)dl_vtx = to_be16((uint16_t)vtx->position_idx); dl_vtx += 2;
                    if (has_nrm) {
                        *(uint16_t*)dl_vtx = to_be16((uint16_t)vtx->normal_idx); dl_vtx += 2;
                    }
                    if (has_uv) {
                        *(uint16_t*)dl_vtx = to_be16((uint16_t)vtx->texcoord_idx); dl_vtx += 2;
                    }
                }
            }

            new_objs[i].obj_buf = obuf;
            new_objs[i].obj_size = obj_total_len;
        } else {
            // Keep original object geometry
            // (Clone existing object bytes)
            size_t orig_len = to_be32(orig_obj->totalLength);
            uint8_t *obuf = MALLOC(orig_len);
            memcpy(obuf, orig_obj, orig_len);
            new_objs[i].obj_buf = obuf;
            new_objs[i].obj_size = orig_len;
        }
    }

    // Now assemble new MDL0 binary
    // Layout:
    // Header (0x80 for v8/v9, 0x88 for v10, 0x8c for v11)
    size_t header_len = (version == 11) ? 0x8c : (version == 10) ? 0x88 : 0x80;

    // Defs & Bones (from parent)
    size_t defs_len = (off_bones > off_defs && off_defs > 0) ? (size_t)(off_bones - off_defs) : 0;
    size_t bones_len = (off_mat > off_bones && off_bones > 0) ? (size_t)(off_mat - off_bones) : 0;

    // Materials and Shaders (from parent, before Objects)
    size_t mat_shd_len = (off_obj > off_mat && off_mat > 0) ? (size_t)(off_obj - off_mat) : 0;

    // Textures, Palettes, UserData, String Pool (from parent, after Objects)
    size_t tex_pool_len = (off_tex > 0 && (size_t)in_size > (size_t)off_tex) ? (size_t)(in_size - off_tex) : 0;

    // Build Position ResourceGroup
    const char *pos_names[256];
    int32_t pos_offsets[256];
    size_t pos_group_hdr_size = 0;
    size_t cur_pos_data_off = sizeof(ResGroup) + (num_objs + 1) * sizeof(ResEntry);
    for (uint32_t i = 0; i < num_objs; i++) {
        pos_names[i] = new_objs[i].name;
        cur_pos_data_off += 4 + align4(strlen(new_objs[i].name) + 1);
    }
    cur_pos_data_off = align32(cur_pos_data_off);
    for (uint32_t i = 0; i < num_objs; i++) {
        pos_offsets[i] = (int32_t)cur_pos_data_off;
        cur_pos_data_off += new_objs[i].pos_size;
    }
    uint8_t *pos_grp_buf = build_resource_group(num_objs, pos_names, pos_offsets, &pos_group_hdr_size);
    size_t total_pos_section_size = cur_pos_data_off;

    // Build Normal ResourceGroup
    size_t num_nrms = 0;
    const char *nrm_names[256];
    int32_t nrm_offsets[256];
    for (uint32_t i = 0; i < num_objs; i++) {
        if (new_objs[i].nrm_size > 0) {
            nrm_names[num_nrms++] = new_objs[i].name;
        }
    }
    uint8_t *nrm_grp_buf = NULL;
    size_t nrm_group_hdr_size = 0, total_nrm_section_size = 0;
    if (num_nrms > 0) {
        size_t cur_nrm_data_off = sizeof(ResGroup) + (num_nrms + 1) * sizeof(ResEntry);
        for (size_t i = 0; i < num_nrms; i++) cur_nrm_data_off += 4 + align4(strlen(nrm_names[i]) + 1);
        cur_nrm_data_off = align32(cur_nrm_data_off);
        size_t n_idx = 0;
        for (uint32_t i = 0; i < num_objs; i++) {
            if (new_objs[i].nrm_size > 0) {
                nrm_offsets[n_idx++] = (int32_t)cur_nrm_data_off;
                cur_nrm_data_off += new_objs[i].nrm_size;
            }
        }
        nrm_grp_buf = build_resource_group(num_nrms, nrm_names, nrm_offsets, &nrm_group_hdr_size);
        total_nrm_section_size = cur_nrm_data_off;
    }

    // Build UV ResourceGroup
    size_t num_uvs = 0;
    const char *uv_names[256];
    int32_t uv_offsets[256];
    for (uint32_t i = 0; i < num_objs; i++) {
        if (new_objs[i].uv_size > 0) {
            uv_names[num_uvs++] = new_objs[i].name;
        }
    }
    uint8_t *uv_grp_buf = NULL;
    size_t uv_group_hdr_size = 0, total_uv_section_size = 0;
    if (num_uvs > 0) {
        size_t cur_uv_data_off = sizeof(ResGroup) + (num_uvs + 1) * sizeof(ResEntry);
        for (size_t i = 0; i < num_uvs; i++) cur_uv_data_off += 4 + align4(strlen(uv_names[i]) + 1);
        cur_uv_data_off = align32(cur_uv_data_off);
        size_t u_idx = 0;
        for (uint32_t i = 0; i < num_objs; i++) {
            if (new_objs[i].uv_size > 0) {
                uv_offsets[u_idx++] = (int32_t)cur_uv_data_off;
                cur_uv_data_off += new_objs[i].uv_size;
            }
        }
        uv_grp_buf = build_resource_group(num_uvs, uv_names, uv_offsets, &uv_group_hdr_size);
        total_uv_section_size = cur_uv_data_off;
    }

    // Build Objects ResourceGroup
    const char *obj_names[256];
    int32_t obj_offsets[256];
    size_t obj_group_hdr_size = 0;
    size_t cur_obj_data_off = sizeof(ResGroup) + (num_objs + 1) * sizeof(ResEntry);
    for (uint32_t i = 0; i < num_objs; i++) {
        obj_names[i] = new_objs[i].name;
        cur_obj_data_off += 4 + align4(strlen(new_objs[i].name) + 1);
    }
    cur_obj_data_off = align32(cur_obj_data_off);
    for (uint32_t i = 0; i < num_objs; i++) {
        obj_offsets[i] = (int32_t)cur_obj_data_off;
        cur_obj_data_off += new_objs[i].obj_size;
    }
    uint8_t *obj_grp_buf = build_resource_group(num_objs, obj_names, obj_offsets, &obj_group_hdr_size);
    size_t total_obj_section_size = cur_obj_data_off;

    // Compute absolute section offsets in new MDL0
    size_t offset_cursor = header_len;

    int32_t new_off_defs = 0;
    if (defs_len > 0) { new_off_defs = (int32_t)offset_cursor; offset_cursor += align32(defs_len); }

    int32_t new_off_bones = 0;
    if (bones_len > 0) { new_off_bones = (int32_t)offset_cursor; offset_cursor += align32(bones_len); }

    int32_t new_off_pos = (int32_t)offset_cursor; offset_cursor += align32(total_pos_section_size);
    int32_t new_off_nrm = (num_nrms > 0) ? (int32_t)offset_cursor : 0;
    if (num_nrms > 0) offset_cursor += align32(total_nrm_section_size);

    int32_t new_off_clr = 0;
    int32_t new_off_uv = (num_uvs > 0) ? (int32_t)offset_cursor : 0;
    if (num_uvs > 0) offset_cursor += align32(total_uv_section_size);

    int32_t new_off_mat = (mat_shd_len > 0) ? (int32_t)offset_cursor : 0;
    int32_t diff_mat = new_off_mat ? (new_off_mat - off_mat) : 0;
    int32_t new_off_shd = (off_shd > 0) ? (off_shd + diff_mat) : 0;
    if (mat_shd_len > 0) offset_cursor += align32(mat_shd_len);

    int32_t new_off_obj = (int32_t)offset_cursor; offset_cursor += align32(total_obj_section_size);

    int32_t new_off_tex = (tex_pool_len > 0) ? (int32_t)offset_cursor : 0;
    int32_t diff_tex = new_off_tex ? (new_off_tex - off_tex) : 0;
    int32_t new_off_plt = (off_plt > 0) ? (off_plt + diff_tex) : 0;
    int32_t new_off_usr = (off_usr > 0) ? (off_usr + diff_tex) : 0;
    if (tex_pool_len > 0) offset_cursor += align32(tex_pool_len);

    size_t total_mdl0_size = align32(offset_cursor);
    uint8_t *new_mdl0 = CALLOC(1, total_mdl0_size);
    if (!new_mdl0) {
        FREE(pos_grp_buf); FREE(nrm_grp_buf); FREE(uv_grp_buf); FREE(obj_grp_buf);
        for (uint32_t i = 0; i < num_objs; i++) {
            FREE(new_objs[i].pos_buf); FREE(new_objs[i].nrm_buf);
            FREE(new_objs[i].uv_buf); FREE(new_objs[i].obj_buf);
        }
        FREE(new_objs);
        return 0;
    }

    // Write Header
    memcpy(new_mdl0, mdl0_data, header_len);
    MDL0Header *out_hdr = (MDL0Header*)new_mdl0;
    out_hdr->size = to_be32((uint32_t)total_mdl0_size);
    out_hdr->sectionOffsets[0] = to_be32((uint32_t)new_off_defs);
    out_hdr->sectionOffsets[1] = to_be32((uint32_t)new_off_bones);
    out_hdr->sectionOffsets[2] = to_be32((uint32_t)new_off_pos);
    out_hdr->sectionOffsets[3] = to_be32((uint32_t)new_off_nrm);
    out_hdr->sectionOffsets[4] = to_be32((uint32_t)new_off_clr);
    out_hdr->sectionOffsets[5] = to_be32((uint32_t)new_off_uv);
    out_hdr->sectionOffsets[6] = 0;
    out_hdr->sectionOffsets[7] = 0;
    out_hdr->sectionOffsets[8] = to_be32((uint32_t)new_off_mat);
    out_hdr->sectionOffsets[9] = to_be32((uint32_t)new_off_shd);
    out_hdr->sectionOffsets[10] = to_be32((uint32_t)new_off_obj);
    out_hdr->sectionOffsets[11] = to_be32((uint32_t)new_off_tex);
    out_hdr->sectionOffsets[12] = to_be32((uint32_t)new_off_plt);
    if (version >= 10 && header_len >= 0x88)
        *(uint32_t*)(new_mdl0 + 0x84) = to_be32((uint32_t)new_off_usr);

    // Update overall extents in header (offset 0x74)
    if (header_len >= 0x80 && overall_min[0] < overall_max[0]) {
        float *ext_hdr = (float*)(new_mdl0 + 0x74);
        for (int c = 0; c < 3; c++) {
            ext_hdr[c] = to_bef(overall_min[c]);
            ext_hdr[c + 3] = to_bef(overall_max[c]);
        }
    }

    // Write Defs & Bones
    if (defs_len > 0) memcpy(new_mdl0 + new_off_defs, mdl0_data + off_defs, defs_len);
    if (bones_len > 0) memcpy(new_mdl0 + new_off_bones, mdl0_data + off_bones, bones_len);

    // Write Positions Section
    memcpy(new_mdl0 + new_off_pos, pos_grp_buf, pos_group_hdr_size);
    for (uint32_t i = 0; i < num_objs; i++) {
        uint8_t *dst = new_mdl0 + new_off_pos + pos_offsets[i];
        memcpy(dst, new_objs[i].pos_buf, new_objs[i].pos_size);
        MDL0VertexHeader *vh = (MDL0VertexHeader*)dst;
        vh->mdl0Offset = to_be32((int32_t)-(new_off_pos + pos_offsets[i]));
        vh->stringOffset = to_be32((int32_t)-(pos_offsets[i] - ((const ResEntry*)(pos_grp_buf + sizeof(ResGroup)))[i + 1].stringOffset));
    }

    // Write Normals Section
    if (num_nrms > 0) {
        memcpy(new_mdl0 + new_off_nrm, nrm_grp_buf, nrm_group_hdr_size);
        size_t n_idx = 0;
        for (uint32_t i = 0; i < num_objs; i++) {
            if (new_objs[i].nrm_size > 0) {
                uint8_t *dst = new_mdl0 + new_off_nrm + nrm_offsets[n_idx];
                memcpy(dst, new_objs[i].nrm_buf, new_objs[i].nrm_size);
                MDL0NormalHeader *nh = (MDL0NormalHeader*)dst;
                nh->mdl0Offset = to_be32((int32_t)-(new_off_nrm + nrm_offsets[n_idx]));
                nh->stringOffset = to_be32((int32_t)-(nrm_offsets[n_idx] - ((const ResEntry*)(nrm_grp_buf + sizeof(ResGroup)))[n_idx + 1].stringOffset));
                n_idx++;
            }
        }
    }

    // Write UVs Section
    if (num_uvs > 0) {
        memcpy(new_mdl0 + new_off_uv, uv_grp_buf, uv_group_hdr_size);
        size_t u_idx = 0;
        for (uint32_t i = 0; i < num_objs; i++) {
            if (new_objs[i].uv_size > 0) {
                uint8_t *dst = new_mdl0 + new_off_uv + uv_offsets[u_idx];
                memcpy(dst, new_objs[i].uv_buf, new_objs[i].uv_size);
                MDL0UVHeader *uh = (MDL0UVHeader*)dst;
                uh->mdl0Offset = to_be32((int32_t)-(new_off_uv + uv_offsets[u_idx]));
                uh->stringOffset = to_be32((int32_t)-(uv_offsets[u_idx] - ((const ResEntry*)(uv_grp_buf + sizeof(ResGroup)))[u_idx + 1].stringOffset));
                u_idx++;
            }
        }
    }

    // Write Materials and Shaders
    if (mat_shd_len > 0)
        memcpy(new_mdl0 + new_off_mat, mdl0_data + off_mat, mat_shd_len);

    // Write Objects Section
    memcpy(new_mdl0 + new_off_obj, obj_grp_buf, obj_group_hdr_size);
    for (uint32_t i = 0; i < num_objs; i++) {
        uint8_t *dst = new_mdl0 + new_off_obj + obj_offsets[i];
        memcpy(dst, new_objs[i].obj_buf, new_objs[i].obj_size);
        MDL0ObjHeader *oh = (MDL0ObjHeader*)dst;
        oh->mdl0Offset = to_be32((int32_t)-(new_off_obj + obj_offsets[i]));
        oh->stringOffset = to_be32((int32_t)-(obj_offsets[i] - ((const ResEntry*)(obj_grp_buf + sizeof(ResGroup)))[i + 1].stringOffset));
    }

    // Write Textures, Palettes, UserData, and String Pool
    if (tex_pool_len > 0)
        memcpy(new_mdl0 + new_off_tex, mdl0_data + off_tex, tex_pool_len);

    // Free temporary buffers
    FREE(pos_grp_buf); FREE(nrm_grp_buf); FREE(uv_grp_buf); FREE(obj_grp_buf);
    for (uint32_t i = 0; i < num_objs; i++) {
        FREE(new_objs[i].pos_buf); FREE(new_objs[i].nrm_buf);
        FREE(new_objs[i].uv_buf); FREE(new_objs[i].obj_buf);
    }
    FREE(new_objs);

    *out_data = new_mdl0;
    *out_size = total_mdl0_size;
    return 1;
}

int InjectDAEIntoBRRES(const uint8_t *brres_data, size_t brres_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size)
{
    if (!brres_data || brres_size < sizeof(BRESHeader) + sizeof(ResGroup) || !dae_model || !out_data || !out_size)
        return 0;

    // Check if input is raw MDL0 directly
    if (!memcmp(brres_data, "MDL0", 4))
        return InjectDAEIntoMDL0(brres_data, brres_size, dae_model, out_data, out_size);

    if (memcmp(brres_data, "bres", 4)) return 0;

    szs_file_t szs;
    InitializeSZS(&szs);

    szs.data = (u8*)brres_data;
    szs.size = brres_size;
    szs.file_size = brres_size;
    szs.fform_file = FF_BRRES;
    szs.fform_arch = FF_BRRES;
    szs.fform_current = FF_BRRES;
    szs.data_alloced = false;

    CollectFilesSZS(&szs, true, 0, -1, SORT_BRRES);

    if (!szs.subfile.used) {
        ResetSZS(&szs);
        return 0;
    }

    // Filter out .string-pool.bin since CreateBRRES generates its own string pool
    for (uint i = 0; i < szs.subfile.used; ) {
        szs_subfile_t *f = szs.subfile.list + i;
        if (f->path && strstr(f->path, ".string-pool.bin")) {
            if (f->data_alloced && f->data) FREE(f->data);
            FreeString(f->path);
            FreeString(f->load_path);
            memmove(szs.subfile.list + i, szs.subfile.list + i + 1, (szs.subfile.used - i - 1) * sizeof(szs_subfile_t));
            szs.subfile.used--;
        } else {
            i++;
        }
    }

    uint total_data_size = 0;
    bool injected = false;
    for (uint i = 0; i < szs.subfile.used; i++) {
        szs_subfile_t *f = szs.subfile.list + i;
        if (!f->is_dir) {
            if (f->fform == FF_MDL || (f->size >= 4 && !memcmp(f->data, "MDL0", 4)) || (f->path && strstr(f->path, "3DModels"))) {
                uint8_t *new_mdl0 = NULL;
                size_t new_mdl0_size = 0;
                if (InjectDAEIntoMDL0(f->data, f->size, dae_model, &new_mdl0, &new_mdl0_size)) {
                    f->data = new_mdl0;
                    f->size = new_mdl0_size;
                    f->data_alloced = true;
                    f->fform = FF_MDL;
                injected = true;
                }
            }
            total_data_size += ALIGN32(f->size, opt_align_brres);
        }
    }

    SortSubFilesSZS(&szs, SORT_BRRES);

    // Assign dir_id for all subfiles according to directory hierarchy
    for (uint i = 0; i < szs.subfile.used; i++) {
        szs_subfile_t *f = szs.subfile.list + i;
        if (f->is_dir) {
            f->dir_id = 0; // Directory entries live in root group (dir_id 0)
        }
    }

    for (uint i = 0; i < szs.subfile.used; i++) {
        szs_subfile_t *f = szs.subfile.list + i;
        if (!f->is_dir) {
            f->dir_id = 0;
            if (f->path) {
                uint best_dir_id = 0;
                size_t best_len = 0;
                uint dir_idx = 0;
                for (uint j = 0; j < szs.subfile.used; j++) {
                    szs_subfile_t *d = szs.subfile.list + j;
                    if (d->is_dir) {
                        dir_idx++;
                        if (d->path) {
                            size_t dlen = strlen(d->path);
                            if (dlen > best_len && !strncmp(f->path, d->path, dlen)) {
                                best_len = dlen;
                                best_dir_id = dir_idx;
                            }
                        }
                    }
                }
                f->dir_id = best_dir_id;
            }
        }
    }

    if (!injected) {
        ResetSZS(&szs);
        return 0;
    }

    enumError err = CreateBRRES(&szs, NULL, NULL, total_data_size);
    if (err > ERR_WARNING || !szs.data) {
        ResetSZS(&szs);
        return 0;
    }

    uint8_t *res = MALLOC(szs.size);
    if (!res) {
        ResetSZS(&szs);
        return 0;
    }
    memcpy(res, szs.data, szs.size);
    *out_data = res;
    *out_size = szs.size;

    ResetSZS(&szs);
    return 1;
}

//-----------------------------------------------------------------------------
// Endian & alignment helper macros
//-----------------------------------------------------------------------------

#define ALIGN_4(x) (((size_t)(x) + 3) & ~(size_t)3)
#define SWP16(v) ((((uint16_t)(v) >> 8) & 0xff) | (((uint16_t)(v) << 8) & 0xff00))
#define SWP32(v) ((((uint32_t)(v) >> 24) & 0xff) | (((uint32_t)(v) >> 8) & 0xff00) | (((uint32_t)(v) << 8) & 0xff0000) | (((uint32_t)(v) << 24) & 0xff000000))
#define RDL16(p) ((uint16_t)(p)[0] | ((uint16_t)(p)[1] << 8))
#define RDL32(p) ((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24))
#define WRL16(p, v) do { (p)[0] = (uint8_t)(v); (p)[1] = (uint8_t)((v) >> 8); } while(0)
#define WRL32(p, v) do { (p)[0] = (uint8_t)(v); (p)[1] = (uint8_t)((v) >> 8); (p)[2] = (uint8_t)((v) >> 16); (p)[3] = (uint8_t)((v) >> 24); } while(0)

//-----------------------------------------------------------------------------
// BFRES (Wii U FRES / FMDL) Injection
//-----------------------------------------------------------------------------

#define BFRES_REL(base, addr) ((size_t)(addr) + (size_t)(int32_t)SWP32(*(const uint32_t*)((base) + (addr))))

int InjectDAEIntoBFRES(const uint8_t *bfres_data, size_t bfres_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size)
{
    if (!bfres_data || bfres_size < 0x60 || !dae_model || !out_data || !out_size || !dae_model->num_meshes)
        return 0;

    if (memcmp(bfres_data, "FRES", 4) != 0 || bfres_data[4] != 3 || SWP16(*(const uint16_t*)(bfres_data + 8)) != 0xfeff)
        return 0;

    // Locate FMDL group at 0x20
    size_t grp = BFRES_REL(bfres_data, 0x20);
    if (grp + 8 > bfres_size) return 0;
    uint32_t entries = SWP32(*(const uint32_t*)(bfres_data + grp + 4));
    if (!entries || grp + 8 + (size_t)(entries + 1) * 16 > bfres_size) return 0;

    size_t fmdl_ent = grp + 8 + 16;
    size_t fmdl = BFRES_REL(bfres_data, fmdl_ent + 12);
    if (fmdl + 0x30 > bfres_size || memcmp(bfres_data + fmdl, "FMDL", 4) != 0)
        return 0;

    // FMDL offsets: FSKL (0x0C), FVTX (0x10), FSHP (0x14), FMAT (0x18)
    size_t fshp_grp = BFRES_REL(bfres_data, fmdl + 0x14);
    uint16_t n_fvtx = SWP16(*(const uint16_t*)(bfres_data + fmdl + 0x20));
    uint16_t n_fshp = SWP16(*(const uint16_t*)(bfres_data + fmdl + 0x22));
    if (!n_fvtx || !n_fshp) return 0;

    const mesh_t *mesh = &dae_model->meshes[0];
    if (!mesh->num_vertices) return 0;

    // Create interleaved vertex buffer: pos (12) + norm (12) + uv (8) = 32 bytes
    uint32_t vtx_count = (uint32_t)mesh->num_vertices;
    uint32_t vtx_stride = 32;
    uint32_t vtx_buf_size = vtx_count * vtx_stride;

    uint8_t *vtx_buf = CALLOC(vtx_count, vtx_stride);
    if (!vtx_buf) return 0;

    for (uint32_t i = 0; i < vtx_count; i++) {
        uint8_t *v = vtx_buf + i * vtx_stride;
        const vertex_t *vx = &mesh->vertices[i];
        vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions) ? mesh->positions[vx->position_idx] : (vec3_t){0,0,0};
        vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals) ? mesh->normals[vx->normal_idx] : (vec3_t){0,1,0};
        vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords) ? mesh->texcoords[vx->texcoord_idx] : (vec2_t){0,0};

        *(uint32_t*)(v + 0)  = SWP32(*(uint32_t*)&p.x);
        *(uint32_t*)(v + 4)  = SWP32(*(uint32_t*)&p.y);
        *(uint32_t*)(v + 8)  = SWP32(*(uint32_t*)&p.z);
        *(uint32_t*)(v + 12) = SWP32(*(uint32_t*)&n.x);
        *(uint32_t*)(v + 16) = SWP32(*(uint32_t*)&n.y);
        *(uint32_t*)(v + 20) = SWP32(*(uint32_t*)&n.z);
        *(uint32_t*)(v + 24) = SWP32(*(uint32_t*)&t.u);
        *(uint32_t*)(v + 28) = SWP32(*(uint32_t*)&t.v);
    }

    // Create index buffer (16-bit big-endian unsigned integers)
    uint32_t idx_count = vtx_count;
    uint32_t idx_buf_size = ALIGN_4(idx_count * 2);
    uint16_t *idx_buf = CALLOC(idx_buf_size / 2, sizeof(uint16_t));
    if (!idx_buf) {
        FREE(vtx_buf);
        return 0;
    }
    for (uint32_t i = 0; i < idx_count; i++)
        idx_buf[i] = SWP16((uint16_t)i);

    // Build new BFRES buffer
    size_t base_size = ALIGN_4(bfres_size);
    size_t vtx_offset = ALIGN_4(base_size);
    size_t idx_offset = ALIGN_4(vtx_offset + vtx_buf_size);
    size_t total_size = ALIGN_4(idx_offset + idx_buf_size);

    uint8_t *out = CALLOC(1, total_size);
    if (!out) {
        FREE(vtx_buf);
        FREE(idx_buf);
        return 0;
    }

    memcpy(out, bfres_data, bfres_size);
    memcpy(out + vtx_offset, vtx_buf, vtx_buf_size);
    memcpy(out + idx_offset, idx_buf, idx_buf_size);
    FREE(vtx_buf);
    FREE(idx_buf);

    // Update FVTX
    size_t fv0 = BFRES_REL(out, fmdl + 0x10);
    *(uint32_t*)(out + fv0 + 8) = SWP32(vtx_count); // num_vertices
    size_t bufs = BFRES_REL(out, fv0 + 0x18);
    *(uint32_t*)(out + bufs + 4) = SWP32(vtx_buf_size);
    *(uint16_t*)(out + bufs + 0x0c) = SWP16((uint16_t)vtx_stride);
    int32_t rel_vtx = (int32_t)(vtx_offset - (bufs + 0x14));
    *(int32_t*)(out + bufs + 0x14) = (int32_t)SWP32((uint32_t)rel_vtx);

    // Update FSHP LOD
    if (fshp_grp + 8 <= bfres_size) {
        size_t fshp_ent = fshp_grp + 8 + 16;
        size_t fshp = BFRES_REL(out, fshp_ent + 12);
        if (fshp + 0x20 <= bfres_size && !memcmp(out + fshp, "FSHP", 4)) {
            size_t lods = BFRES_REL(out, fshp + 0x1c);
            if (lods + 0x18 <= bfres_size) {
                *(uint32_t*)(out + lods + 8) = SWP32(idx_count); // index count
                int32_t rel_idx = (int32_t)(idx_offset - (lods + 0x10));
                *(int32_t*)(out + lods + 0x10) = (int32_t)SWP32((uint32_t)rel_idx);
            }
        }
    }

    // Update FRES total file size
    *(uint32_t*)(out + 0x0c) = SWP32((uint32_t)total_size);

    *out_data = out;
    *out_size = total_size;
    return 1;
}

//-----------------------------------------------------------------------------
// BCH (3DS H3D) Injection
//-----------------------------------------------------------------------------

int InjectDAEIntoBCH(const uint8_t *bch_data, size_t bch_size,
                     const model_t *dae_model,
                     uint8_t **out_data, size_t *out_size)
{
    if (!bch_data || bch_size < 0x44 || !dae_model || !out_data || !out_size || !dae_model->num_meshes)
        return 0;

    if (memcmp(bch_data, "BCH", 3) != 0 || bch_data[3] != 0)
        return 0;

    const mesh_t *mesh = &dae_model->meshes[0];
    if (!mesh->num_vertices) return 0;

    uint32_t vtx_count = (uint32_t)mesh->num_vertices;
    uint32_t vtx_stride = 32; // pos(12) + norm(12) + uv(8)
    uint32_t vtx_buf_size = vtx_count * vtx_stride;

    uint8_t *vtx_buf = CALLOC(vtx_count, vtx_stride);
    if (!vtx_buf) return 0;

    for (uint32_t i = 0; i < vtx_count; i++) {
        uint8_t *v = vtx_buf + i * vtx_stride;
        const vertex_t *vx = &mesh->vertices[i];
        vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions) ? mesh->positions[vx->position_idx] : (vec3_t){0,0,0};
        vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals) ? mesh->normals[vx->normal_idx] : (vec3_t){0,1,0};
        vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords) ? mesh->texcoords[vx->texcoord_idx] : (vec2_t){0,0};

        memcpy(v + 0, &p.x, 4);
        memcpy(v + 4, &p.y, 4);
        memcpy(v + 8, &p.z, 4);
        memcpy(v + 12, &n.x, 4);
        memcpy(v + 16, &n.y, 4);
        memcpy(v + 20, &n.z, 4);
        memcpy(v + 24, &t.u, 4);
        memcpy(v + 28, &t.v, 4);
    }

    uint32_t idx_count = vtx_count;
    uint32_t idx_buf_size = ALIGN_4(idx_count * 2);
    uint16_t *idx_buf = CALLOC(idx_buf_size / 2, sizeof(uint16_t));
    if (!idx_buf) {
        FREE(vtx_buf);
        return 0;
    }
    for (uint32_t i = 0; i < idx_count; i++)
        idx_buf[i] = (uint16_t)i;

    size_t base_size = ALIGN_4(bch_size);
    size_t vtx_offset = ALIGN_4(base_size);
    size_t idx_offset = ALIGN_4(vtx_offset + vtx_buf_size);
    size_t total_size = ALIGN_4(idx_offset + idx_buf_size);

    uint8_t *out = CALLOC(1, total_size);
    if (!out) {
        FREE(vtx_buf);
        FREE(idx_buf);
        return 0;
    }

    memcpy(out, bch_data, bch_size);
    memcpy(out + vtx_offset, vtx_buf, vtx_buf_size);
    memcpy(out + idx_offset, idx_buf, idx_buf_size);
    FREE(vtx_buf);
    FREE(idx_buf);

    // Expand raw_data_len to cover the appended buffers
    uint8_t bc = bch_data[4];
    const bool has_ext = bc >= 0x21;
    uint o_raw = has_ext ? 0x2c : 0x28;
    if (o_raw + 4 <= bch_size) {
        uint32_t old_raw_len = RDL32(out + o_raw);
        WRL32(out + o_raw, old_raw_len + (uint32_t)(total_size - base_size));
    }

    *out_data = out;
    *out_size = total_size;
    return 1;
}

//-----------------------------------------------------------------------------
// BCRES / CGFX (3DS CGFX) Injection
//-----------------------------------------------------------------------------

int InjectDAEIntoBCRES(const uint8_t *bcres_data, size_t bcres_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size)
{
    if (!bcres_data || bcres_size < 0x20 || !dae_model || !out_data || !out_size || !dae_model->num_meshes)
        return 0;

    if (memcmp(bcres_data, "CGFX", 4) != 0)
        return 0;

    const mesh_t *mesh = &dae_model->meshes[0];
    if (!mesh->num_vertices) return 0;

    uint32_t vtx_count = (uint32_t)mesh->num_vertices;
    uint32_t vtx_stride = 32;
    uint32_t vtx_buf_size = vtx_count * vtx_stride;

    uint8_t *vtx_buf = CALLOC(vtx_count, vtx_stride);
    if (!vtx_buf) return 0;

    for (uint32_t i = 0; i < vtx_count; i++) {
        uint8_t *v = vtx_buf + i * vtx_stride;
        const vertex_t *vx = &mesh->vertices[i];
        vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions) ? mesh->positions[vx->position_idx] : (vec3_t){0,0,0};
        vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals) ? mesh->normals[vx->normal_idx] : (vec3_t){0,1,0};
        vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords) ? mesh->texcoords[vx->texcoord_idx] : (vec2_t){0,0};

        memcpy(v + 0, &p.x, 4);
        memcpy(v + 4, &p.y, 4);
        memcpy(v + 8, &p.z, 4);
        memcpy(v + 12, &n.x, 4);
        memcpy(v + 16, &n.y, 4);
        memcpy(v + 20, &n.z, 4);
        memcpy(v + 24, &t.u, 4);
        memcpy(v + 28, &t.v, 4);
    }

    uint32_t idx_count = vtx_count;
    uint32_t idx_buf_size = ALIGN_4(idx_count * 2);
    uint16_t *idx_buf = CALLOC(idx_buf_size / 2, sizeof(uint16_t));
    if (!idx_buf) {
        FREE(vtx_buf);
        return 0;
    }
    for (uint32_t i = 0; i < idx_count; i++)
        idx_buf[i] = (uint16_t)i;

    size_t base_size = ALIGN_4(bcres_size);
    size_t vtx_offset = ALIGN_4(base_size);
    size_t idx_offset = ALIGN_4(vtx_offset + vtx_buf_size);
    size_t total_size = ALIGN_4(idx_offset + idx_buf_size);

    uint8_t *out = CALLOC(1, total_size);
    if (!out) {
        FREE(vtx_buf);
        FREE(idx_buf);
        return 0;
    }

    memcpy(out, bcres_data, bcres_size);
    memcpy(out + vtx_offset, vtx_buf, vtx_buf_size);
    memcpy(out + idx_offset, idx_buf, idx_buf_size);
    FREE(vtx_buf);
    FREE(idx_buf);

    // Update CGFX header total size at 0x0c
    WRL32(out + 0x0c, (uint32_t)total_size);

    *out_data = out;
    *out_size = total_size;
    return 1;
}

//-----------------------------------------------------------------------------
// NSBMD (Nintendo DS BMD0) Injection
//-----------------------------------------------------------------------------

// Encode float (-8..+7.999) to 1:3:12 fixed-point s16
static inline int16_t fx12_enc(float v) {
    int val = (int)roundf(v * 4096.0f);
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    return (int16_t)val;
}

// Encode float (-1..+0.999) to 1:0:9 fixed-point 10-bit signed
static inline uint32_t fx9_enc(float v) {
    int val = (int)roundf(v * 511.0f);
    if (val > 511) val = 511;
    if (val < -512) val = -512;
    return (uint32_t)(val & 0x3FF);
}

// Encode float (-2048..+2047.9) to 1:11:4 fixed-point s16
static inline int16_t fx4_enc(float v) {
    int val = (int)roundf(v * 16.0f);
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    return (int16_t)val;
}

int InjectDAEIntoNSBMD(const uint8_t *nsbmd_data, size_t nsbmd_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size)
{
    if (!nsbmd_data || nsbmd_size < 0x20 || !dae_model || !out_data || !out_size || !dae_model->num_meshes)
        return 0;

    if (memcmp(nsbmd_data, "BMD0", 4) != 0)
        return 0;

    const mesh_t *mesh = &dae_model->meshes[0];
    if (!mesh->num_vertices) return 0;

    // Encode DS Geometry display list
    // Capacity: 4 command bytes per 4-byte command word + param words
    size_t max_dl_words = (mesh->num_vertices + 10) * 8;
    uint32_t *dl_words = CALLOC(max_dl_words, sizeof(uint32_t));
    if (!dl_words) return 0;

    size_t word_idx = 0;

    // Command 1: BEGIN_VTXS(0 = GL_TRIANGLES)
    // Opcode 0x40 with param = 0
    dl_words[word_idx++] = 0x00000040; // 0x40, 0x00, 0x00, 0x00
    dl_words[word_idx++] = 0;          // GL_TRIANGLES

    // Vertices: each has NORMAL (0x21), TEXCOORD (0x22), VTX_16 (0x20)
    for (size_t i = 0; i < mesh->num_vertices; i++) {
        const vertex_t *vx = &mesh->vertices[i];
        vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions) ? mesh->positions[vx->position_idx] : (vec3_t){0,0,0};
        vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals) ? mesh->normals[vx->normal_idx] : (vec3_t){0,1,0};
        vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords) ? mesh->texcoords[vx->texcoord_idx] : (vec2_t){0,0};

        uint32_t norm_packed = fx9_enc(n.x) | (fx9_enc(n.y) << 10) | (fx9_enc(n.z) << 20);
        uint32_t tex_packed = (uint16_t)fx4_enc(t.u) | ((uint32_t)(uint16_t)fx4_enc(t.v) << 16);
        uint32_t vtx_xy = (uint16_t)fx12_enc(p.x) | ((uint32_t)(uint16_t)fx12_enc(p.y) << 16);
        uint32_t vtx_z = (uint16_t)fx12_enc(p.z);

        // Pack commands: 0x21 (NORMAL), 0x22 (TEXCOORD), 0x20 (VTX_16), 0x00 (NOP)
        dl_words[word_idx++] = 0x00202221;
        dl_words[word_idx++] = norm_packed;
        dl_words[word_idx++] = tex_packed;
        dl_words[word_idx++] = vtx_xy;
        dl_words[word_idx++] = vtx_z;
    }

    // Command: END_VTXS (0x41)
    dl_words[word_idx++] = 0x00000041;

    size_t dl_bytes = word_idx * sizeof(uint32_t);

    // Build new NSBMD buffer
    size_t base_size = ALIGN_4(nsbmd_size);
    size_t total_size = ALIGN_4(base_size + dl_bytes);

    uint8_t *out = CALLOC(1, total_size);
    if (!out) {
        FREE(dl_words);
        return 0;
    }

    memcpy(out, nsbmd_data, nsbmd_size);
    memcpy(out + base_size, dl_words, dl_bytes);
    FREE(dl_words);

    // Update BMD0 total file size at offset 0x08
    WRL32(out + 0x08, (uint32_t)total_size);

    *out_data = out;
    *out_size = total_size;
    return 1;
}

//-----------------------------------------------------------------------------
// Universal Dispatcher
//-----------------------------------------------------------------------------

int InjectDAEIntoModel(const uint8_t *parent_data, size_t parent_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size)
{
    if (!parent_data || parent_size < 4 || !dae_model || !out_data || !out_size)
        return 0;

    if (parent_size >= 4 && !memcmp(parent_data, "bres", 4))
        return InjectDAEIntoBRRES(parent_data, parent_size, dae_model, out_data, out_size);
    if (parent_size >= 4 && !memcmp(parent_data, "MDL0", 4))
        return InjectDAEIntoMDL0(parent_data, parent_size, dae_model, out_data, out_size);
    if (parent_size >= 4 && !memcmp(parent_data, "FRES", 4))
        return InjectDAEIntoBFRES(parent_data, parent_size, dae_model, out_data, out_size);
    if (parent_size >= 4 && !memcmp(parent_data, "BCH", 3) && parent_data[3] == 0)
        return InjectDAEIntoBCH(parent_data, parent_size, dae_model, out_data, out_size);
    if (parent_size >= 4 && !memcmp(parent_data, "CGFX", 4))
        return InjectDAEIntoBCRES(parent_data, parent_size, dae_model, out_data, out_size);
    if (parent_size >= 4 && !memcmp(parent_data, "BMD0", 4))
        return InjectDAEIntoNSBMD(parent_data, parent_size, dae_model, out_data, out_size);

    return 0;
}
