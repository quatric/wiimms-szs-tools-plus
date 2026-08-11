#ifndef LIB_BRRES_MODEL_H
#define LIB_BRRES_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "lib-model-dae.h"

model_t* ParseMDL0(const uint8_t *data, size_t size);
void FreeModel(model_t *model);

#endif
