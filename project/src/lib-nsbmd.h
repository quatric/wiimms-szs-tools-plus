#ifndef LIB_NSBMD_H
#define LIB_NSBMD_H

#include "lib-model-dae.h"
#include <stdint.h>
#include <stddef.h>

model_t* ParseNSBMD(const uint8_t *data, size_t size);

#endif
