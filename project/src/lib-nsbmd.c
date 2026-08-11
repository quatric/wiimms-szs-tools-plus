#include "lib-nsbmd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Basic NSBMD Header structure
typedef struct {
    char magic[4]; // "BMD0"
    uint16_t byte_order;
    uint16_t version;
    uint32_t file_size;
    uint16_t header_size;
    uint16_t num_blocks;
} bmd0_header_t;

model_t* ParseNSBMD(const uint8_t *data, size_t size) {
    if (!data || size < sizeof(bmd0_header_t)) {
        return NULL;
    }

    bmd0_header_t *header = (bmd0_header_t*)data;
    if (memcmp(header->magic, "BMD0", 4) != 0) {
        return NULL;
    }

    // A real implementation would parse the MDL0 block here and extract
    // bones, vertex positions, normals, texcoords, and polygons into the
    // model_t structure. That is not implemented yet; returning NULL
    // (instead of an empty model_t) makes the caller report a real error
    // rather than silently writing an empty/misleading .dae file.
    return NULL;
}
