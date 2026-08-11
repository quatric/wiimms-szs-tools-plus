#include "lib-plt0.h"
#include <stdlib.h>
#include <string.h>

enumError LoadPLT0 ( Image_t *img, const u8 *data, uint data_size )
{
    if ( !data || data_size < 0x20 || memcmp(data, "PLT0", 4) != 0 )
        return ERR_INVALID_DATA;

    u32 pal_offset = (data[0x10] << 24) | (data[0x11] << 16) | (data[0x12] << 8) | data[0x13];
    u32 pform = (data[0x18] << 24) | (data[0x19] << 16) | (data[0x1A] << 8) | data[0x1B];
    u16 num_colors = (data[0x1C] << 8) | data[0x1D];

    if ( pal_offset + num_colors * 2 > data_size )
        return ERR_WARNING;

    img->iform = IMG_RGBA32;
    img->width = num_colors;
    img->height = 1;
    img->data_size = num_colors * 4;
    img->data = MALLOC(img->data_size);
    if ( !img->data )
        return ERR_OUT_OF_MEMORY;
    img->data_alloced = true;

    if ( pform == 0 ) img->pform = PAL_IA8;
    else if ( pform == 1 ) img->pform = PAL_RGB565;
    else if ( pform == 2 ) img->pform = PAL_RGB5A3;
    else img->pform = PAL_INVALID;

    const u8 *p = data + pal_offset;
    u8 *d = img->data;

    for ( u16 i = 0; i < num_colors; i++ )
    {
        u16 c = (p[0] << 8) | p[1];
        p += 2;
        u8 r, g, b, a;

        if ( pform == 0 ) // IA8
        {
            a = c >> 8;
            u8 intensity = c & 0xFF;
            r = g = b = intensity;
        }
        else if ( pform == 1 ) // RGB565
        {
            r = (c >> 11) & 0x1F; r = (r << 3) | (r >> 2);
            g = (c >> 5) & 0x3F;  g = (g << 2) | (g >> 4);
            b = c & 0x1F;         b = (b << 3) | (b >> 2);
            a = 0xFF;
        }
        else if ( pform == 2 ) // RGB5A3
        {
            if ( c & 0x8000 )
            {
                r = (c >> 10) & 0x1F; r = (r << 3) | (r >> 2);
                g = (c >> 5) & 0x1F;  g = (g << 3) | (g >> 2);
                b = c & 0x1F;         b = (b << 3) | (b >> 2);
                a = 0xFF;
            }
            else
            {
                a = (c >> 12) & 0x07; a = (a << 5) | (a << 2) | (a >> 1);
                r = (c >> 8) & 0x0F;  r = (r << 4) | r;
                g = (c >> 4) & 0x0F;  g = (g << 4) | g;
                b = c & 0x0F;         b = (b << 4) | b;
            }
        }
        else
        {
            r = g = b = a = 0;
        }

        *d++ = r;
        *d++ = g;
        *d++ = b;
        *d++ = a;
    }

    return ERR_OK;
}
