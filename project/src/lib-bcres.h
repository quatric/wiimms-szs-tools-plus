#ifndef LIB_BCRES_H
#define LIB_BCRES_H

#include "lib-model-dae.h"
#include <stdint.h>
#include <stddef.h>

// Parses a CGFX model's skeleton. Geometry is stored in PICA200 GPU command
// buffers, which are not decoded yet, so this returns NULL rather than an
// empty model when no meshes could be resolved.
model_t* ParseBCRES(const uint8_t *data, size_t size);

// CGFX ("BCRES") container enumeration: the DATA block holds a series of
// (count, DICT offset) pairs, one per resource kind. All offsets in a CGFX
// are self-relative.
typedef struct cgfx_entry_t { const char *name; uint32_t address; } cgfx_entry_t;
typedef struct cgfx_dict_t  { unsigned n; cgfx_entry_t *entries; } cgfx_dict_t;

#define CGFX_N_DICTS 16
typedef struct cgfx_t
{
    const uint8_t *data;
    size_t size;
    uint32_t revision;
    cgfx_dict_t dict[CGFX_N_DICTS];
}
cgfx_t;

const char *GetCGFXDictName ( int id );
void ResetCGFX ( cgfx_t *cgfx );
int  ScanCGFX  ( cgfx_t *cgfx, const uint8_t *data, size_t size );

#endif
