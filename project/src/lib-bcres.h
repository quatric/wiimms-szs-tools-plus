#ifndef LIB_BCRES_H
#define LIB_BCRES_H

#include "lib-model-dae.h"
#include <stdint.h>
#include <stddef.h>

model_t* ParseBCRES(const uint8_t *data, size_t size);

#endif
