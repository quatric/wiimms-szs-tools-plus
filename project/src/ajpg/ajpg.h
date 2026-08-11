#ifndef AVCODEC_AJPG_H
#define AVCODEC_AJPG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ajpg_decode(uint8_t *src, uint8_t *dst, uint8_t *work, int sizeLimit);
int ajpg_encode(uint8_t *src, uint8_t *dst, int width, int height, int quality, uint32_t sampRate, uint8_t *work, int sizeLimit);
uint32_t ajpg_get_filesize(const void *src);
int ajpg_get_width(const void *src);
int ajpg_get_height(const void *src);

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_AJPG_H */
