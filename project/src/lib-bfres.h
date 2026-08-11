#ifndef LIB_BFRES_H
#define LIB_BFRES_H

#include "lib-model-dae.h"
#include <stdint.h>
#include <stddef.h>

model_t* ParseBFRES(const uint8_t *data, size_t size);

#endif
