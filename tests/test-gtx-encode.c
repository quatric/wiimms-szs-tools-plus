#include "lib-gtx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The project test build enables allocation tracing. Keep this standalone
// regression independent of the full CLI support graph.
void trace_free(ccp f,ccp p,uint l,void *v){(void)f;(void)p;(void)l;free(v);}
void *trace_malloc(ccp f,ccp p,uint l,size_t n){(void)f;(void)p;(void)l;return malloc(n);}
void *trace_calloc(ccp f,ccp p,uint l,size_t n,size_t s){(void)f;(void)p;(void)l;return calloc(n,s);}
void *trace_realloc(ccp f,ccp p,uint l,void *v,size_t n){(void)f;(void)p;(void)l;return realloc(v,n);}

static int check_rgba_formats(void)
{
    static const uint formats[]={0x01,0x02,0x07,0x08,0x0a,0x0b,0x11,0x19,0x1a};
    u8 rgba[7*5*4];
    for(uint i=0;i<7*5;i++) {
        rgba[4*i]=(u8)(i*7); rgba[4*i+1]=(u8)(255-i*3);
        rgba[4*i+2]=(u8)(i*11); rgba[4*i+3]=(u8)(i*17);
    }
    for(uint fi=0;fi<sizeof(formats)/sizeof(*formats);fi++)
    for(uint mode=0;mode<=15;mode++) {
        u8 *file=0,*decoded=0; uint file_size=0,w=0,h=0;
        if(EncodeGTX_RGBA_Format(&file,&file_size,rgba,7,5,formats[fi],mode)) return 1;
        gtx_t g={0};
        if(ScanGTX(&g,file,file_size)||DecodeGTX_RGBA(&decoded,&w,&h,&g,0)||w!=7||h!=5) return 2;
        ResetGTX(&g); free(decoded); free(file);
    }
    return 0;
}

static int check_subresources(void)
{
    enum { W=9,H=7,SLICES=3,SAMPLES=4 };
    u8 level0[W*H*SLICES*SAMPLES*4], level1[4*3*SLICES*SAMPLES*4];
    for(uint i=0;i<sizeof(level0);i++) level0[i]=(u8)(i*13+5);
    for(uint i=0;i<sizeof(level1);i++) level1[i]=(u8)(i*7+11);
    gtx_encode_level_t levels[]={{level0,sizeof(level0),SLICES},{level1,sizeof(level1),SLICES}};
    gtx_encode_texture_t texture={1,W,H,SLICES,2,0x1a,2,1,4,0,0,
        0,0,0,0,0,2,0,SLICES,{0,1,2,3},levels};
    u8 *file=0; uint file_size=0;
    if(EncodeGTXTextures(&file,&file_size,&texture,1)) return 3;
    gtx_t g={0}; if(ScanGTX(&g,file,file_size)||g.n_textures!=1) return 4;
    for(uint mip=0;mip<2;mip++) for(uint sample=0;sample<SAMPLES;sample++)
    for(uint slice=0;slice<SLICES;slice++) {
        u8 *got=0; uint w=0,h=0;
        if(DecodeGTXSubresource_RGBA(&got,&w,&h,&g,0,mip,slice,sample)) {
            fprintf(stderr,"subresource decode failed: mip=%u slice=%u sample=%u\n",mip,slice,sample);
            return 5;
        }
        const uint mw=mip?4:W,mh=mip?3:H;
        const u8 *want=(mip?level1:level0)+(((sample*SLICES+slice)*mh*mw)*4);
        if(w!=mw||h!=mh||memcmp(got,want,mw*mh*4)) return 6;
        free(got);
    }
    ResetGTX(&g); free(file); return 0;
}

int main(void)
{
    int err=check_rgba_formats();
    if(!err) err=check_subresources();
    if(err) fprintf(stderr,"GTX encoder test failed at stage %d\n",err);
    return err;
}
