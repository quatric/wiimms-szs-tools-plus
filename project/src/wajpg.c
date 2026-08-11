// wajpg - AJPG / ODH still-image codec tool.
//
// The codec core (src/ajpg/odh_core.c) is our own validated port from the
// mobipeg FFmpeg fork's libavcodec/odh.c -- it fixes three big-endian traps
// and two out-of-bounds bugs present in the original cdbackup reference and
// was verified bit-exact against an oracle built from the unmodified
// reference decoder. No external tool or subprocess is used.
//
// This CLI works on binary PPM (P6) so the codec can be exercised without
// pulling a PNG dependency into a tool that does not otherwise need one;
// `wimgt` handles PNG for the formats it decodes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ajpg/ajpg.h"
#include "lib-std.h"

static uint8_t *read_ppm ( const char *fname, int *w, int *h )
{
    FILE *f = fopen(fname,"rb");
    if (!f) return 0;
    char magic[3] = {0};
    int maxval = 0;
    if ( fscanf(f,"%2s",magic) != 1 || strcmp(magic,"P6")
	|| fscanf(f,"%d %d %d",w,h,&maxval) != 3 || *w <= 0 || *h <= 0 )
    {
	fclose(f);
	return 0;
    }
    fgetc(f); // single whitespace byte before the binary data

    const size_t n = (size_t)*w * *h;
    uint8_t *rgb = (uint8_t*)MALLOC(n*3);
    if ( fread(rgb,1,n*3,f) != n*3 )
    {
	fclose(f);
	FREE(rgb);
	return 0;
    }
    fclose(f);

    uint8_t *rgba = (uint8_t*)MALLOC(n*4);
    for ( size_t i = 0; i < n; i++ )
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
    for ( size_t i = 0; i < (size_t)w*h; i++ )
	fwrite(rgba+i*4,1,3,f);
    fclose(f);
    return 1;
}

static uint8_t *read_file ( const char *fname, size_t *out_size )
{
    FILE *f = fopen(fname,"rb");
    if (!f) return 0;
    fseek(f,0,SEEK_END);
    long size = ftell(f);
    fseek(f,0,SEEK_SET);
    if ( size < 0 ) { fclose(f); return 0; }
    uint8_t *buf = (uint8_t*)MALLOC(size ? size : 1);
    if ( size && fread(buf,1,size,f) != (size_t)size )
    {
	fclose(f);
	FREE(buf);
	return 0;
    }
    fclose(f);
    *out_size = (size_t)size;
    return buf;
}

int main ( int argc, char **argv )
{
    if ( argc < 3 )
    {
	printf("wajpg - AJPG (ODH) still-image codec tool\n"
	       "Usage: %s encode <in.ppm> <out.ajpg> [quality 1-100, default 80]\n"
	       "       %s decode <in.ajpg> <out.ppm>\n"
	       "       %s info   <in.ajpg>\n",
	       argv[0], argv[0], argv[0] );
	return 1;
    }

    if ( !strcmp(argv[1],"info") )
    {
	size_t size = 0;
	uint8_t *data = read_file(argv[2],&size);
	if (!data) { fprintf(stderr,"wajpg: can't read %s\n",argv[2]); return 1; }
	int w = 0, h = 0;
	const int ok = AjpgGetInfo(data,size,&w,&h);
	FREE(data);
	if (!ok) { fprintf(stderr,"wajpg: %s is not an AJPG image\n",argv[2]); return 1; }
	printf("AJPG %dx%d, %zu bytes\n",w,h,size);
	return 0;
    }

    if ( !strcmp(argv[1],"encode") )
    {
	if ( argc < 4 ) { fprintf(stderr,"wajpg: need <in.ppm> <out.ajpg>\n"); return 1; }
	int w = 0, h = 0;
	uint8_t *rgba = read_ppm(argv[2],&w,&h);
	if (!rgba) { fprintf(stderr,"wajpg: can't read PPM %s\n",argv[2]); return 1; }
	const int quality = argc > 4 ? atoi(argv[4]) : 80;

	uint8_t *out = 0;
	size_t out_size = 0;
	const int ok = AjpgEncodeRGBA(rgba,w,h,quality,&out,&out_size);
	FREE(rgba);
	if (!ok)
	{
	    fprintf(stderr,"wajpg: encode failed"
		    " (AJPG requires even dimensions and max 2047x2047)\n");
	    return 1;
	}
	FILE *f = fopen(argv[3],"wb");
	if (!f) { fprintf(stderr,"wajpg: can't write %s\n",argv[3]); AjpgFree(out); return 1; }
	fwrite(out,1,out_size,f);
	fclose(f);
	AjpgFree(out);
	printf("wajpg: encoded %s (%dx%d, q%d) -> %s (%zu bytes)\n",
		argv[2],w,h,quality,argv[3],out_size);
	return 0;
    }

    if ( !strcmp(argv[1],"decode") )
    {
	if ( argc < 4 ) { fprintf(stderr,"wajpg: need <in.ajpg> <out.ppm>\n"); return 1; }
	size_t size = 0;
	uint8_t *data = read_file(argv[2],&size);
	if (!data) { fprintf(stderr,"wajpg: can't read %s\n",argv[2]); return 1; }

	uint8_t *rgba = 0;
	int w = 0, h = 0;
	const int ok = AjpgDecodeRGBA(data,size,&rgba,&w,&h);
	FREE(data);
	if (!ok) { fprintf(stderr,"wajpg: decode failed for %s\n",argv[2]); return 1; }

	const int wrote = write_ppm(argv[3],rgba,w,h);
	AjpgFree(rgba);
	if (!wrote) { fprintf(stderr,"wajpg: can't write %s\n",argv[3]); return 1; }
	printf("wajpg: decoded %s (%dx%d) -> %s\n",argv[2],w,h,argv[3]);
	return 0;
    }

    fprintf(stderr,"wajpg: unknown command '%s'\n",argv[1]);
    return 1;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
