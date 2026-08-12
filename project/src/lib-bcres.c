#include "lib-bcres.h"
#include "lib-brres-model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} bcres_stream_t;

static uint32_t read_u32(bcres_stream_t *stream) {
    if (stream->pos + 4 > stream->size) return 0;
    uint32_t val = stream->data[stream->pos] | (stream->data[stream->pos + 1] << 8) | (stream->data[stream->pos + 2] << 16) | (stream->data[stream->pos + 3] << 24);
    stream->pos += 4;
    return val;
}

static uint16_t read_u16(bcres_stream_t *stream) {
    if (stream->pos + 2 > stream->size) return 0;
    uint16_t val = stream->data[stream->pos] | (stream->data[stream->pos + 1] << 8);
    stream->pos += 2;
    return val;
}

static float read_f32(bcres_stream_t *stream) {
    uint32_t uval = read_u32(stream);
    float fval;
    memcpy(&fval, &uval, sizeof(float));
    return fval;
}

static uint32_t get_rel_offset(bcres_stream_t *stream) {
    uint32_t pos = (uint32_t)stream->pos;
    uint32_t offset = read_u32(stream);
    if (offset != 0) offset += pos;
    return offset;
}

static void skip(bcres_stream_t *stream, size_t bytes) {
    stream->pos += bytes;
}

static void seek_pos(bcres_stream_t *stream, size_t pos) {
    stream->pos = pos;
}

// Reads a 4-byte magic into 'magic', bounds-checked. Returns false (and
// zero-fills 'magic') if the read would run past the buffer.
static bool read_magic(bcres_stream_t *stream, char magic[4]) {
    if (stream->pos + 4 > stream->size)
    {
	memset(magic,0,4);
	return false;
    }
    memcpy(magic,stream->data+stream->pos,4);
    stream->pos += 4;
    return true;
}

model_t* ParseBCRES(const uint8_t *data, size_t size) {
    if (!data || size < 0x14) return NULL;
    
    bcres_stream_t stream = {data, size, 0};
    
    char magic[4];
    if (!read_magic(&stream,magic)) return NULL;
    if (strncmp(magic, "CGFX", 4) != 0) return NULL;
    
    uint16_t endian = read_u16(&stream);
    uint16_t header_len = read_u16(&stream);
    uint32_t revision = read_u32(&stream);
    uint32_t file_len = read_u32(&stream);
    uint32_t entries = read_u32(&stream);
    
    seek_pos(&stream, header_len);
    
    if (stream.pos + 8 > stream.size) return NULL;
    if (!read_magic(&stream,magic)) return NULL;
    if (strncmp(magic, "DATA", 4) != 0) return NULL;
    
    uint32_t data_len = read_u32(&stream);
    
    uint32_t models_dict_entries = read_u32(&stream);
    uint32_t models_dict_offset = get_rel_offset(&stream);
    
    model_t *model = (model_t*)calloc(1, sizeof(model_t));
    if (!model) return NULL;
    
    if (models_dict_entries > 0 && models_dict_offset < stream.size) {
        seek_pos(&stream, models_dict_offset);
        char dict_magic[4];
        if (!read_magic(&stream,dict_magic)) return NULL;
        uint32_t dict_len = read_u32(&stream);
        uint32_t dict_num = read_u32(&stream);
        
        skip(&stream, 16); // dict root node data
        
        if (dict_num > 0) {
            skip(&stream, 8); // Skip ref bits and nodes
            uint32_t name_offset = get_rel_offset(&stream);
            uint32_t model_offset = get_rel_offset(&stream);
            
            seek_pos(&stream, model_offset);
            
            uint32_t flags = read_u32(&stream);
            int has_skeleton = (flags & 0x80) > 0;
            
            char cmdl_magic[4];
            if (!read_magic(&stream,cmdl_magic)) return NULL;
            uint32_t cmdl_revision = read_u32(&stream);
            uint32_t name_off = get_rel_offset(&stream);
            
            skip(&stream, 8);
            flags = read_u32(&stream);
            uint32_t child_count = read_u32(&stream);
            skip(&stream, 4);
            skip(&stream, 8); // anim group
            
            // transform... 9 floats
            skip(&stream, 9 * 4);
            skip(&stream, 16 * 4); // local mat
            skip(&stream, 16 * 4); // world mat
            
            uint32_t obj_entries = read_u32(&stream);
            uint32_t obj_ptr_table_off = get_rel_offset(&stream);
            
            skip(&stream, 8); // materials
            uint32_t shape_entries = read_u32(&stream);
            uint32_t shape_ptr_table_off = get_rel_offset(&stream);
            
            skip(&stream, 8); // object nodes dict
            
            flags = read_u32(&stream);
            uint32_t layer_id = read_u32(&stream);
            
            uint32_t skeleton_offset = 0;
            if (has_skeleton) {
                skeleton_offset = get_rel_offset(&stream);
            }
            
            if (has_skeleton && skeleton_offset != 0) {
                seek_pos(&stream, skeleton_offset);
                flags = read_u32(&stream);
                skip(&stream, 4); // sobj magic
                uint32_t rev = read_u32(&stream);
                skip(&stream, 4); // name offset
                skip(&stream, 8);
                uint32_t bone_dict_entries = read_u32(&stream);
                uint32_t bone_dict_offset = get_rel_offset(&stream);
                
                model->num_joints = bone_dict_entries;
                model->joints = (joint_t*)calloc(model->num_joints, sizeof(joint_t));
                
                seek_pos(&stream, bone_dict_offset);
                skip(&stream, 4 + 4 + 4 + 16);
                
                for (size_t b = 0; b < model->num_joints; b++) {
                    skip(&stream, 8);
                    uint32_t b_name_off = get_rel_offset(&stream);
                    uint32_t b_data_off = get_rel_offset(&stream);
                    
                    size_t ret = stream.pos;
                    seek_pos(&stream, b_data_off);
                    uint32_t b_name = get_rel_offset(&stream);
                    uint32_t b_flags = read_u32(&stream);
                    uint32_t bone_id = read_u32(&stream);
                    int32_t parent_id = (int32_t)read_u32(&stream);
                    skip(&stream, 16); // offsets
                    
                    float sx = read_f32(&stream);
                    float sy = read_f32(&stream);
                    float sz = read_f32(&stream);
                    
                    float rx = read_f32(&stream);
                    float ry = read_f32(&stream);
                    float rz = read_f32(&stream);
                    
                    float tx = read_f32(&stream);
                    float ty = read_f32(&stream);
                    float tz = read_f32(&stream);
                    
                    model->joints[b].parent_idx = bone_id;
                    model->joints[b].parent_idx = parent_id;
                    model->joints[b].scale.x = sx; model->joints[b].scale.y = sy; model->joints[b].scale.z = sz;
                    model->joints[b].rotate.x = rx; model->joints[b].rotate.y = ry; model->joints[b].rotate.z = rz;
                    model->joints[b].translate.x = tx; model->joints[b].translate.y = ty; model->joints[b].translate.z = tz;
                    snprintf(model->joints[b].name, sizeof(model->joints[b].name), "Bone%d", (int)b);
                    
                    seek_pos(&stream, ret);
                }
            }
            
            if (shape_entries > 0 && shape_ptr_table_off != 0) {
                model->num_meshes = shape_entries;
                model->meshes = (mesh_t*)calloc(model->num_meshes, sizeof(mesh_t));
                
                for (size_t s = 0; s < shape_entries; s++) {
                    seek_pos(&stream, shape_ptr_table_off + (s * 4));
                    uint32_t shape_off = get_rel_offset(&stream);
                    seek_pos(&stream, shape_off);
                    
                    flags = read_u32(&stream);
                    skip(&stream, 4); // sobj magic
                    uint32_t rev = read_u32(&stream);
                    uint32_t shape_name_off = get_rel_offset(&stream);
                    skip(&stream, 8); // user data
                    flags = read_u32(&stream);
                    skip(&stream, 4); // bbox off
                    float px = read_f32(&stream);
                    float py = read_f32(&stream);
                    float pz = read_f32(&stream);
                    
                    uint32_t faces_entries = read_u32(&stream);
                    uint32_t faces_off = get_rel_offset(&stream);
                    skip(&stream, 4);
                    uint32_t vtx_entries = read_u32(&stream);
                    uint32_t vtx_off = get_rel_offset(&stream);
                    
                    snprintf(model->meshes[s].name, sizeof(model->meshes[s].name), "Mesh%d", (int)s);
                    model->meshes[s].num_vertices = 0;
                    
                    if (faces_entries > 0 && faces_off != 0) {
                        seek_pos(&stream, faces_off);
                        uint32_t f_off = get_rel_offset(&stream);
                        seek_pos(&stream, f_off);
                        
                        skip(&stream, 8); // nodes
                        uint32_t skin_mode = read_u32(&stream);
                        uint32_t face_header_entries = read_u32(&stream);
                        uint32_t face_header_off = get_rel_offset(&stream);
                        
                        seek_pos(&stream, face_header_off);
                        uint32_t fh_off = get_rel_offset(&stream);
                        seek_pos(&stream, fh_off);
                        
                        uint32_t fd_entries = read_u32(&stream);
                        uint32_t fd_off = get_rel_offset(&stream);
                        
                        seek_pos(&stream, fd_off);
                        uint32_t fd = get_rel_offset(&stream);
                        seek_pos(&stream, fd);
                        
                        uint32_t idx_fmt = (read_u32(&stream) & 2) >> 1;
                        skip(&stream, 4);
                        uint32_t idx_len = read_u32(&stream);
                        uint32_t idx_off = get_rel_offset(&stream);
                        
                        model->meshes[s].num_vertices = idx_len;
                        model->meshes[s].vertices = (vertex_t*)calloc(model->meshes[s].num_vertices * 3, sizeof(vertex_t));
                        
                        seek_pos(&stream, idx_off);
                        for (size_t f = 0; f < model->meshes[s].num_vertices; f++) {
                            for (int i = 0; i < 3; i++) {
                                int idx = 0;
                                if (idx_fmt == 1) idx = read_u16(&stream);
                                else idx = read_u32(&stream) & 0xFF; // fallback
                                model->meshes[s].vertices[f].position_idx = idx;
                                model->meshes[s].vertices[f].normal_idx = idx;
                                model->meshes[s].vertices[f].texcoord_idx = idx;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // A model with no resolved geometry is not a success: returning an empty
    // model_t would make the caller write a valid-looking but empty DAE.
    // num_meshes is set from the shape count before the shapes are resolved,
    // and the shape reader fills index counts without ever resolving
    // positions, so require an actual position array before claiming success.
    // CGFX keeps its geometry in PICA200 command buffers, which this parser
    // does not decode yet -- see lib-bcres.h.
    size_t with_geometry = 0;
    for ( size_t i = 0; i < model->num_meshes; i++ )
        if ( model->meshes[i].num_positions )
            with_geometry++;
    if ( !with_geometry )
    {
        FreeModel(model);
        return NULL;
    }
    return model;
}


//-----------------------------------------------------------------------------
// CGFX container enumeration
//-----------------------------------------------------------------------------

static const char *cgfx_dict_names[CGFX_N_DICTS] =
{
    "Models", "Textures", "LUTs", "Materials", "Shaders", "Cameras",
    "Lights", "Fogs", "Scenes", "SkeletalAnimations", "MaterialAnimations",
    "VisibilityAnimations", "CameraAnimations", "LightAnimations",
    "FogAnimations", "Emitters"
};

const char *GetCGFXDictName ( int id )
{
    return id >= 0 && id < CGFX_N_DICTS ? cgfx_dict_names[id] : "?";
}

void ResetCGFX ( cgfx_t *cgfx )
{
    if (!cgfx) return;
    for ( int i = 0; i < CGFX_N_DICTS; i++ )
	free(cgfx->dict[i].entries);
    memset(cgfx,0,sizeof(*cgfx));
}

static uint32_t c_u32 ( const uint8_t *p )
    { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static int32_t  c_s32 ( const uint8_t *p ) { return (int32_t)c_u32(p); }
static uint16_t c_u16 ( const uint8_t *p ) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }

int ScanCGFX ( cgfx_t *cgfx, const uint8_t *data, size_t size )
{
    if ( !cgfx || !data || size < 0x20 || memcmp(data,"CGFX",4) )
	return 0;
    memset(cgfx,0,sizeof(*cgfx));
    cgfx->data = data;
    cgfx->size = size;
    cgfx->revision = c_u32(data+8);

    const uint16_t hdr_len = c_u16(data+6);
    if ( hdr_len < 0x14 || (size_t)hdr_len + 8 > size )
	return 0;
    // The DATA block follows the header; its (count, dict offset) pairs start
    // right after the block's own magic and size.
    if ( memcmp(data+hdr_len,"DATA",4) )
	return 0;
    const size_t base = (size_t)hdr_len + 8;

    for ( int i = 0; i < CGFX_N_DICTS; i++ )
    {
	const size_t o = base + (size_t)i*8;
	if ( o + 8 > size ) break;
	const uint32_t count = c_u32(data+o);
	if ( !count || count > 0x10000 ) continue;
	const size_t dic = o + 4 + (size_t)c_s32(data+o+4);
	if ( dic + 0x0c > size || memcmp(data+dic,"DICT",4) ) continue;
	const uint32_t n = c_u32(data+dic+8);
	if ( !n || n > 0x10000 || dic + 0x0c + (size_t)(n+1)*16 > size ) continue;

	cgfx_entry_t *ent = calloc(n,sizeof(*ent));
	if (!ent) continue;
	unsigned got = 0;
	for ( uint32_t k = 0; k < n; k++ )
	{
	    // Node: refBit(4) left(2) right(2) namePtr(4) dataPtr(4), all
	    // offsets self-relative. Node 0 is the tree root.
	    const size_t e = dic + 0x0c + (size_t)(k+1)*16;
	    const size_t np = e + 8 + (size_t)c_s32(data+e+8);
	    if ( np >= size ) continue;
	    size_t q = np;
	    while ( q < size && data[q] ) q++;
	    if ( q >= size ) continue;
	    ent[got].name = (const char*)(data+np);
	    ent[got].address = (uint32_t)( e + 12 + (size_t)c_s32(data+e+12) );
	    got++;
	}
	if (got) { cgfx->dict[i].entries = ent; cgfx->dict[i].n = got; }
	else free(ent);
    }
    return 1;
}
