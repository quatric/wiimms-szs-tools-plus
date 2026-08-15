#ifndef LIB_BRRES_INJECT_H
#define LIB_BRRES_INJECT_H 1

#include <stddef.h>
#include <stdint.h>
#include "lib-model-dae.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent MDL0 binary.
// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
int InjectDAEIntoMDL0(const uint8_t *mdl0_data, size_t mdl0_size,
                      const model_t *dae_model,
                      uint8_t **out_data, size_t *out_size);

// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent BRRES archive.
// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
int InjectDAEIntoBRRES(const uint8_t *brres_data, size_t brres_size,
                       const model_t *dae_model,
                       uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif // LIB_BRRES_INJECT_H
