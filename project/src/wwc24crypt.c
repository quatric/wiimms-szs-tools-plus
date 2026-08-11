// wwc24crypt - WC24 (Wii Connect24) payload crypto utility.
// Native AES-128-CBC + HMAC-SHA1, matching the primitives RiiConnect24's
// wc24-tools use for WiiConnect24 mail/attachment payloads. No external
// crypto library or subprocess is used.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lib-aes.h"
#include "crypto/wiimm-sha.h"
#include "lib-std.h"

static int hex_nibble ( char c )
{
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return -1;
}

static int hex_decode ( const char *hex, uint8_t *out, int out_len )
{
    int len = (int)strlen(hex);
    if ( len != out_len*2 )
	return 0;
    for ( int i = 0; i < out_len; i++ )
    {
	int hi = hex_nibble(hex[i*2]);
	int lo = hex_nibble(hex[i*2+1]);
	if ( hi < 0 || lo < 0 )
	    return 0;
	out[i] = (uint8_t)((hi<<4)|lo);
    }
    return 1;
}

static void hex_print ( const uint8_t *data, int len )
{
    for ( int i = 0; i < len; i++ )
	printf("%02x",data[i]);
    printf("\n");
}

static uint8_t *read_file ( const char *fname, long *out_size )
{
    FILE *f = fopen(fname,"rb");
    if (!f) return 0;
    fseek(f,0,SEEK_END);
    long size = ftell(f);
    fseek(f,0,SEEK_SET);
    uint8_t *buf = (uint8_t*)MALLOC(size > 0 ? size : 1);
    if (!buf) { fclose(f); return 0; }
    if ( size > 0 && fread(buf,1,size,f) != (size_t)size )
    {
	fclose(f);
	FREE(buf);
	return 0;
    }
    fclose(f);
    *out_size = size;
    return buf;
}

// HMAC-SHA1 built on top of the vendored WIIMM_SHA1 one-shot digest.
static void hmac_sha1 ( const uint8_t *key, int key_len,
			 const uint8_t *msg, size_t msg_len,
			 uint8_t out[20] )
{
    uint8_t k[64];
    memset(k,0,sizeof(k));
    if ( key_len > 64 )
	WIIMM_SHA1(key,key_len,k);
    else
	memcpy(k,key,key_len);

    uint8_t ipad[64], opad[64];
    for ( int i = 0; i < 64; i++ )
    {
	ipad[i] = (uint8_t)(k[i] ^ 0x36);
	opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    uint8_t *inner_buf = (uint8_t*)MALLOC(64+msg_len);
    memcpy(inner_buf,ipad,64);
    memcpy(inner_buf+64,msg,msg_len);
    uint8_t inner_digest[20];
    WIIMM_SHA1(inner_buf,64+msg_len,inner_digest);
    FREE(inner_buf);

    uint8_t outer_buf[84];
    memcpy(outer_buf,opad,64);
    memcpy(outer_buf+64,inner_digest,20);
    WIIMM_SHA1(outer_buf,84,out);
}

static int cmd_selftest ( void )
{
    // FIPS-197 Appendix B: AES-128 known-answer test.
    uint8_t key[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
			0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    uint8_t block[16] = {0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
			  0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34};
    const uint8_t expect[16] = {0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
				 0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32};
    aes128_ctx_t ctx;
    AES128_Init(&ctx,key);
    AES128_EncryptBlock(&ctx,block);
    int aes_enc_ok = !memcmp(block,expect,16);
    AES128_DecryptBlock(&ctx,block);
    uint8_t plain[16] = {0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
			  0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34};
    int aes_dec_ok = !memcmp(block,plain,16);

    // RFC 2202 test case 1 for HMAC-SHA1: key=0x0b*20, data="Hi There"
    uint8_t hkey[20];
    memset(hkey,0x0b,20);
    const char *hmsg = "Hi There";
    uint8_t hmac_out[20];
    hmac_sha1(hkey,20,(const uint8_t*)hmsg,strlen(hmsg),hmac_out);
    const uint8_t hmac_expect[20] = {
	0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,
	0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00 };
    int hmac_ok = !memcmp(hmac_out,hmac_expect,20);

    printf("AES-128 encrypt (FIPS-197 KAT): %s\n", aes_enc_ok ? "PASS" : "FAIL");
    printf("AES-128 decrypt (round-trip):   %s\n", aes_dec_ok ? "PASS" : "FAIL");
    printf("HMAC-SHA1 (RFC 2202 case 1):    %s\n", hmac_ok ? "PASS" : "FAIL");

    // CBC round-trip self-test (not a KAT, just internal consistency).
    uint8_t cbc_key[16], iv[16];
    for (int i=0;i<16;i++) { cbc_key[i]=(uint8_t)i; iv[i]=(uint8_t)(0x10+i); }
    uint8_t data[32], orig[32];
    for (int i=0;i<32;i++) data[i]=orig[i]=(uint8_t)(i*7);
    AES128_CBC_Encrypt(cbc_key,iv,data,32);
    AES128_CBC_Decrypt(cbc_key,iv,data,32);
    int cbc_ok = !memcmp(data,orig,32);
    printf("AES-128-CBC round-trip:         %s\n", cbc_ok ? "PASS" : "FAIL");

    return aes_enc_ok && aes_dec_ok && hmac_ok && cbc_ok ? 0 : 1;
}

static void usage ( const char *prog )
{
    printf("wwc24crypt - WC24 payload crypto utility (AES-128-CBC + HMAC-SHA1)\n"
	   "Usage:\n"
	   "  %s selftest\n"
	   "  %s encrypt <key-hex32> <iv-hex32> <infile> <outfile>\n"
	   "  %s decrypt <key-hex32> <iv-hex32> <infile> <outfile>\n"
	   "  %s hmac-sha1 <key-hex> <infile>\n",
	   prog, prog, prog, prog );
}

int main ( int argc, char **argv )
{
    if ( argc < 2 )
    {
	usage(argv[0]);
	return 1;
    }

    if ( !strcmp(argv[1],"selftest") )
	return cmd_selftest();

    if ( !strcmp(argv[1],"encrypt") || !strcmp(argv[1],"decrypt") )
    {
	if ( argc != 6 )
	{
	    usage(argv[0]);
	    return 1;
	}
	uint8_t key[16], iv[16];
	if ( !hex_decode(argv[2],key,16) || !hex_decode(argv[3],iv,16) )
	{
	    fprintf(stderr,"wwc24crypt: key and iv must each be 32 hex chars (16 bytes)\n");
	    return 1;
	}
	long size = 0;
	uint8_t *data = read_file(argv[4],&size);
	if (!data)
	{
	    fprintf(stderr,"wwc24crypt: can't read %s\n",argv[4]);
	    return 1;
	}
	if ( size % 16 )
	{
	    fprintf(stderr,"wwc24crypt: input size %ld is not a multiple of 16 (no padding is applied)\n",size);
	    FREE(data);
	    return 1;
	}
	if ( !strcmp(argv[1],"encrypt") )
	    AES128_CBC_Encrypt(key,iv,data,(size_t)size);
	else
	    AES128_CBC_Decrypt(key,iv,data,(size_t)size);

	FILE *out = fopen(argv[5],"wb");
	if (!out)
	{
	    fprintf(stderr,"wwc24crypt: can't write %s\n",argv[5]);
	    FREE(data);
	    return 1;
	}
	fwrite(data,1,(size_t)size,out);
	fclose(out);
	FREE(data);
	printf("wwc24crypt: %sed %s -> %s (%ld bytes)\n",argv[1],argv[4],argv[5],size);
	return 0;
    }

    if ( !strcmp(argv[1],"hmac-sha1") )
    {
	if ( argc != 4 )
	{
	    usage(argv[0]);
	    return 1;
	}
	int key_len = (int)strlen(argv[2])/2;
	uint8_t *key = (uint8_t*)MALLOC(key_len ? key_len : 1);
	if ( !hex_decode(argv[2],key,key_len) )
	{
	    fprintf(stderr,"wwc24crypt: key must be valid hex\n");
	    FREE(key);
	    return 1;
	}
	long size = 0;
	uint8_t *data = read_file(argv[3],&size);
	if (!data)
	{
	    fprintf(stderr,"wwc24crypt: can't read %s\n",argv[3]);
	    FREE(key);
	    return 1;
	}
	uint8_t out[20];
	hmac_sha1(key,key_len,data,(size_t)size,out);
	hex_print(out,20);
	FREE(key);
	FREE(data);
	return 0;
    }

    usage(argv[0]);
    return 1;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
