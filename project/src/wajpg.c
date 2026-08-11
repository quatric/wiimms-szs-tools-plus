// wajpg - AJPG (GBA "ODH") still-image codec tool.
// Ported from the mobipeg fork's libavcodec/ajpg_core.cpp (our own prior
// work) directly into this project; no external tool or subprocess.
// This CLI works on raw RGBA8 buffers via simple PPM/raw I/O so the codec
// can be smoke-tested without pulling in a PNG library.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ajpg/ajpg.h"
#include "lib-std.h"

#define AJPG_FORMAT_RGBA8 1

static uint8_t *read_ppm ( const char *fname, int *w, int *h )
{
    FILE *f = fopen(fname,"rb");
    if (!f) return 0;
    char magic[3] = {0};
    if ( fscanf(f,"%2s",magic) != 1 || strcmp(magic,"P6") )
    {
	fclose(f);
	return 0;
    }
    int maxval;
    if ( fscanf(f,"%d %d %d",w,h,&maxval) != 3 )
    {
	fclose(f);
	return 0;
    }
    fgetc(f); // single whitespace before binary data
    int n = (*w) * (*h);
    uint8_t *rgb = (uint8_t*)MALLOC(n*3);
    if ( fread(rgb,1,n*3,f) != (size_t)(n*3) )
    {
	fclose(f);
	FREE(rgb);
	return 0;
    }
    fclose(f);

    uint8_t *rgba = (uint8_t*)MALLOC(n*4);
    for ( int i = 0; i < n; i++ )
    {
	rgba[i*4+0] = rgb[i*3+0];
	rgba[i*4+1] = rgb[i*3+1];
	rgba[i*4+2] = rgb[i*3+2];
	rgba[i*4+3] = 0xff;
    }
    FREE(rgb);
    return rgba;
}

static int write_ppm ( const char *fname, const uint8_t *rgba, int w, int h )
{
    FILE *f = fopen(fname,"wb");
    if (!f) return 0;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for ( int i = 0; i < w*h; i++ )
	fwrite(rgba+i*4,1,3,f);
    fclose(f);
    return 1;
}

int main ( int argc, char **argv )
{
    if ( argc < 4 )
    {
	printf("wajpg - AJPG (GBA ODH) still-image codec tool\n"
	       "Usage: %s encode <in.ppm> <out.ajpg> [quality]\n"
	       "       %s decode <in.ajpg> <out.ppm>\n",
	       argv[0], argv[0] );
	return 1;
    }

    // Buffer sized generously; the codec's own comments state ~112.5KB of
    // scratch space is enough for a 240x160 image, so 4 MiB covers larger
    // sizes with headroom.
    static uint8_t work[4*1024*1024];
    static uint8_t out_buf[8*1024*1024];

    if ( !strcmp(argv[1],"encode") )
    {
	if ( argc < 4 ) { fprintf(stderr,"wajpg: need <in.ppm> <out.ajpg>\n"); return 1; }
	int w,h;
	uint8_t *rgba = read_ppm(argv[2],&w,&h);
	if (!rgba) { fprintf(stderr,"wajpg: can't read PPM %s\n",argv[2]); return 1; }
	int quality = argc > 4 ? atoi(argv[4]) : 80;

	// NOTE: ajpg_encode's header names its 6th param "sampRate", but it is
	// actually forwarded positionally into CArGBAOdh::compressGbaOdh's
	// real 'sizeLimit' (output buffer capacity) parameter -- and the
	// header's own "sizeLimit" (last param) is what actually selects the
	// pixel format (RGB565=0/RGBA8=1/Y8U8V8=2). The header's parameter
	// names for this function do not match what the implementation does.
	int size = ajpg_encode(rgba,out_buf,w,h,quality,(uint32_t)sizeof(out_buf),work,AJPG_FORMAT_RGBA8);
	FREE(rgba);
	if ( size <= 0 )
	{
	    fprintf(stderr,"wajpg: encode failed (%d)\n",size);
	    return 1;
	}
	FILE *f = fopen(argv[3],"wb");
	if (!f) { fprintf(stderr,"wajpg: can't write %s\n",argv[3]); return 1; }
	fwrite(out_buf,1,(size_t)size,f);
	fclose(f);
	printf("wajpg: encoded %s (%dx%d) -> %s (%d bytes)\n",argv[2],w,h,argv[3],size);
	return 0;
    }

    if ( !strcmp(argv[1],"decode") )
    {
	if ( argc < 4 ) { fprintf(stderr,"wajpg: need <in.ajpg> <out.ppm>\n"); return 1; }
	FILE *f = fopen(argv[2],"rb");
	if (!f) { fprintf(stderr,"wajpg: can't read %s\n",argv[2]); return 1; }
	fseek(f,0,SEEK_END);
	long fsize = ftell(f);
	fseek(f,0,SEEK_SET);
	static uint8_t in_buf[8*1024*1024];
	if ( fread(in_buf,1,(size_t)fsize,f) != (size_t)fsize )
	{
	    fclose(f);
	    fprintf(stderr,"wajpg: short read on %s\n",argv[2]);
	    return 1;
	}
	fclose(f);

	int w = ajpg_get_width(in_buf);
	int h = ajpg_get_height(in_buf);
	int ret = ajpg_decode(in_buf,out_buf,work,AJPG_FORMAT_RGBA8);
	if ( ret < 0 )
	{
	    fprintf(stderr,"wajpg: decode failed (%d)\n",ret);
	    return 1;
	}
	if ( !write_ppm(argv[3],out_buf,w,h) )
	{
	    fprintf(stderr,"wajpg: can't write %s\n",argv[3]);
	    return 1;
	}
	printf("wajpg: decoded %s (%dx%d) -> %s\n",argv[2],w,h,argv[3]);
	return 0;
    }

    fprintf(stderr,"wajpg: unknown command '%s'\n",argv[1]);
    return 1;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
