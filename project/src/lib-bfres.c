#include "lib-bfres.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Utility for reading little-endian data
static uint32_t read32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t read16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static float read_float_le(const uint8_t *p) {
    union { uint32_t u; float f; } v;
    v.u = read32_le(p);
    return v.f;
}

model_t* ParseBFRES(const uint8_t *data, size_t size) {
    if (!data || size < 0x40) return NULL;

    // "FRES" or "FRES    "
    if (memcmp(data, "FRES", 4) != 0) {
        return NULL;
    }

    // FRES typically has a model dictionary (Index Group) around offset 0x20 or
    // 0x40 depending on version (Wii U vs Switch). Full BFRES parsing (FMDL
    // index-group traversal, FSKL bones, FVTX vertex-buffer decoding, FSHP
    // polygons) is not implemented yet. Returning NULL here (instead of an
    // empty model_t) is deliberate: it makes the caller report a real error
    // rather than silently writing an empty/misleading .dae file.
    (void)read32_le; (void)read16_le; (void)read_float_le;
    return NULL;
}
