#ifndef SZS_LATTE_BRIDGE_H
#define SZS_LATTE_BRIDGE_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns a malloc-owned UTF-8 disassembly, or NULL for invalid input.
char * szs_latte_disassemble ( const unsigned char *data, size_t size );
unsigned char * szs_latte_assemble
    ( const char *text, size_t size, size_t *output_size );
void szs_latte_free ( void *data );

#ifdef __cplusplus
}
#endif
#endif
