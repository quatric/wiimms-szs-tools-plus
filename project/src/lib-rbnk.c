#include "lib-rbnk.h"
#include <string.h>

static u32 rd_u32 ( const u8 *p ) { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static u16 rd_u16 ( const u8 *p ) { return (u16)((u16)p[0]<<8 | p[1]); }
static float rd_f32 ( const u8 *p )
{
    u32 bits = rd_u32(p);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// -----------------------------------------------------------------------------
///////////////		    ruint helpers			///////////////
// -----------------------------------------------------------------------------
// A 'ruint' (NW4R's own name) is an 8 byte tagged reference: refType(u8),
// dataType(u8), reserved(u16BE), dataOffset(s32BE). See lib-rbnk.h's header
// comment: every ruint's dataOffset in the Data section, at ANY nesting
// depth, is relative to the same top-of-section RuintList base -- ported
// from BrawlLib's RBNKDataGroupNode/RangeTable/IndexTable Initialize() call
// chain, not obvious from the raw struct layout alone.

typedef struct rbnk_ctx_t
{
    const u8 *data;
    uint      size;
    const u8 *base;   // top of the Data section's RuintList (list_base)
    uint      depth;
    bool      error;
}
rbnk_ctx_t;

static bool in_bounds ( const rbnk_ctx_t *ctx, const u8 *p, uint len )
{
    return p >= ctx->data && (uint)(p - ctx->data) + len <= ctx->size;
}

static rbnk_entry_type_t ruint_type ( const u8 *r ) { return (rbnk_entry_type_t)r[1]; }
static s32 ruint_offset ( const u8 *r ) { return (s32)rd_u32(r+4); }

static void parse_entry ( rbnk_ctx_t *ctx, const u8 *addr, rbnk_entry_type_t type, rbnk_node_t *out );

// RangeTable: tableCount(u8) + tableCount key bytes, aligned to 4, then a
// tableCount-entry ruint array (the "RuintCollection").
static void parse_range_table ( rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out )
{
    if ( ctx->error || !in_bounds(ctx,addr,1) ) { ctx->error = true; return; }
    uint n = addr[0];
    const u8 *keys = addr + 1;
    const u8 *coll = addr + ((1+n+3) & ~3u);
    if ( !in_bounds(ctx,keys,n) || !in_bounds(ctx,coll,8u*n) ) { ctx->error = true; return; }

    out->type = RBNK_ENTRY_RANGE;
    out->n_child = n;
    out->keys = n ? MALLOC(n) : 0;
    out->child = n ? CALLOC(n,sizeof(*out->child)) : 0;
    memcpy(out->keys, keys, n);

    for ( uint i = 0; i < n && !ctx->error; i++ )
    {
        const u8 *r = coll + 8*i;
        rbnk_entry_type_t t = ruint_type(r);
        if ( t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX )
        {
            const u8 *child_addr = ctx->base + ruint_offset(r);
            parse_entry(ctx, child_addr, t, out->child+i);
        }
        else
            out->child[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
    }
}

// IndexTable: min(u8) + max(u8) + reserved(u16) + a dense (max-min+1)-entry
// ruint array, indexed directly by (note - min).
static void parse_index_table ( rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out )
{
    if ( ctx->error || !in_bounds(ctx,addr,4) ) { ctx->error = true; return; }
    u8 kmin = addr[0], kmax = addr[1];
    if ( kmax < kmin ) { ctx->error = true; return; }
    uint n = (uint)kmax - kmin + 1;
    const u8 *coll = addr + 4;
    if ( !in_bounds(ctx,coll,8u*n) ) { ctx->error = true; return; }

    out->type = RBNK_ENTRY_INDEX;
    out->key_min = kmin;
    out->n_child = n;
    out->keys = 0;
    out->child = CALLOC(n,sizeof(*out->child));

    for ( uint i = 0; i < n && !ctx->error; i++ )
    {
        const u8 *r = coll + 8*i;
        rbnk_entry_type_t t = ruint_type(r);
        if ( t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX )
        {
            const u8 *child_addr = ctx->base + ruint_offset(r);
            parse_entry(ctx, child_addr, t, out->child+i);
        }
        else
            out->child[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
    }
}

static void parse_inst ( rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out )
{
    if ( ctx->error || !in_bounds(ctx,addr,0x30) ) { ctx->error = true; return; }
    out->type = RBNK_ENTRY_INST;
    rbnk_inst_t *p = &out->inst;
    p->wave_index               = rd_u32(addr);
    p->attack                   = addr[4];
    p->decay                    = addr[5];
    p->sustain                  = addr[6];
    p->release                  = addr[7];
    p->hold                     = addr[8];
    p->wave_data_location_type  = addr[9];
    p->note_off_type            = addr[10];
    p->alternate_assign         = addr[11];
    p->original_key             = addr[12];
    p->volume                   = addr[13];
    p->pan                      = addr[14];
    p->surround_pan             = addr[15];
    p->pitch                    = rd_f32(addr+16);
}

static void parse_entry ( rbnk_ctx_t *ctx, const u8 *addr, rbnk_entry_type_t type, rbnk_node_t *out )
{
    memset(out,0,sizeof(*out));
    if ( ctx->error || ++ctx->depth > 16 ) { ctx->error = true; --ctx->depth; return; }

    switch (type)
    {
        case RBNK_ENTRY_INST:  parse_inst(ctx,addr,out); break;
        case RBNK_ENTRY_RANGE: parse_range_table(ctx,addr,out); break;
        case RBNK_ENTRY_INDEX: parse_index_table(ctx,addr,out); break;
        default:                out->type = type; break;
    }
    --ctx->depth;
}

static void reset_node ( rbnk_node_t *n )
{
    if (!n) return;
    for ( uint i = 0; i < n->n_child; i++ )
        reset_node(n->child+i);
    FREE(n->keys);
    FREE(n->child);
    memset(n,0,sizeof(*n));
}

// -----------------------------------------------------------------------------
///////////////		    top-level scan			///////////////
// -----------------------------------------------------------------------------

enumError ScanRBNK ( rbnk_t *rbnk, const u8 *data, uint size )
{
    memset(rbnk,0,sizeof(*rbnk));

    if ( size < 0x20 || memcmp(data,"RBNK",4) )
        return ERROR0(ERR_INVALID_DATA, "ScanRBNK: not a RBNK file\n");

    u16 version = rd_u16(data+6);
    rbnk->version_major = version >> 8;
    rbnk->version_minor = version & 0xff;

    u32 data_off  = rd_u32(data+0x10);
    u32 wave_off  = rd_u32(data+0x18);

    if ( (u64)data_off + 12 > size || memcmp(data+data_off,"DATA",4) )
        return ERROR0(ERR_INVALID_DATA, "ScanRBNK: missing DATA chunk\n");

    const u8 *dlist = data + data_off + 8; // RuintList: numEntries(4) + ruint[]
    u32 n_program = rd_u32(dlist);
    if ( (u64)(dlist - data) + 4 + 8ull*n_program > size )
        return ERROR0(ERR_INVALID_DATA, "ScanRBNK: Data program list exceeds buffer (%u entries)\n", n_program);

    rbnk_ctx_t ctx = { .data = data, .size = size, .base = dlist };

    rbnk->n_program = n_program;
    rbnk->program = n_program ? CALLOC(n_program,sizeof(*rbnk->program)) : 0;

    const u8 *entries = dlist + 4;
    for ( u32 i = 0; i < n_program && !ctx.error; i++ )
    {
        const u8 *r = entries + 8*i;
        rbnk_entry_type_t t = ruint_type(r);
        if ( t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX )
        {
            const u8 *addr = ctx.base + ruint_offset(r);
            parse_entry(&ctx, addr, t, rbnk->program+i);
        }
        else
            rbnk->program[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
    }

    if (ctx.error)
    {
        ResetRBNK(rbnk);
        return ERROR0(ERR_INVALID_DATA, "ScanRBNK: malformed Data section (offset out of range or nesting too deep)\n");
    }

    // Wave section: only the version < 2 direct-WaveInfo-list layout is
    // handled (see lib-rbnk.h); version >= 2 banks reference an embedded
    // RWAR archive instead, a different, unverified-against-a-real-sample
    // structure this doesn't guess at.
    if ( rbnk->version_minor < 2 && wave_off )
    {
        if ( (u64)wave_off + 12 > size || memcmp(data+wave_off,"WAVE",4) )
            return ERROR0(ERR_INVALID_DATA, "ScanRBNK: missing WAVE chunk\n");

        const u8 *wlist = data + wave_off + 8;
        u32 n_wave = rd_u32(wlist);
        if ( (u64)(wlist - data) + 4 + 8ull*n_wave > size )
            return ERROR0(ERR_INVALID_DATA, "ScanRBNK: Wave list exceeds buffer (%u entries)\n", n_wave);

        rbnk->n_wave = n_wave;
        rbnk->wave = n_wave ? CALLOC(n_wave,sizeof(*rbnk->wave)) : 0;

        const u8 *wentries = wlist + 4;
        for ( u32 i = 0; i < n_wave; i++ )
        {
            const u8 *r = wentries + 8*i;
            const u8 *wi = wlist + ruint_offset(r);
            if ( (u64)(wi - data) + 0x1C > size )
            {
                ResetRBNK(rbnk);
                return ERROR0(ERR_INVALID_DATA, "ScanRBNK: WaveInfo %u out of range\n", i);
            }

            u8 encoding = wi[0];
            rbnk->wave[i].encoding    = encoding;
            rbnk->wave[i].looped      = wi[1];
            rbnk->wave[i].channels    = wi[2];
            rbnk->wave[i].sample_rate = rd_u16(wi+4);
            s32 loop_start_raw = (s32)rd_u32(wi+8);
            s32 nibbles        = (s32)rd_u32(wi+0xC);
            rbnk->wave[i].n_samples  = encoding == 2 ? (s64)nibbles/16*14 + (nibbles%16-2) : nibbles;
            rbnk->wave[i].loop_start = encoding == 2 ? (s64)loop_start_raw/16*14 + (loop_start_raw%16-2) : loop_start_raw;
        }
    }

    return ERR_OK;
}

void ResetRBNK ( rbnk_t *rbnk )
{
    if (!rbnk) return;
    for ( uint i = 0; i < rbnk->n_program; i++ )
        reset_node(rbnk->program+i);
    FREE(rbnk->program);
    FREE(rbnk->wave);
    memset(rbnk,0,sizeof(*rbnk));
}

// -----------------------------------------------------------------------------
///////////////		    XML dump				///////////////
// -----------------------------------------------------------------------------

static void dump_node ( const rbnk_node_t *n, FILE *f, int indent )
{
    switch (n->type)
    {
        case RBNK_ENTRY_INST:
            fprintf(f, "%*s<inst wave-index=\"%u\" attack=\"%u\" decay=\"%u\" sustain=\"%u\""
                       " release=\"%u\" hold=\"%u\" note-off=\"%u\" alt-assign=\"%u\""
                       " original-key=\"%u\" volume=\"%u\" pan=\"%u\" surround-pan=\"%u\" pitch=\"%.6f\"/>\n",
                indent, "", n->inst.wave_index, n->inst.attack, n->inst.decay, n->inst.sustain,
                n->inst.release, n->inst.hold, n->inst.note_off_type, n->inst.alternate_assign,
                n->inst.original_key, n->inst.volume, n->inst.pan, n->inst.surround_pan, n->inst.pitch );
            break;

        case RBNK_ENTRY_RANGE:
            fprintf(f, "%*s<range-table n=\"%u\">\n", indent, "", n->n_child);
            for ( uint i = 0; i < n->n_child; i++ )
            {
                fprintf(f, "%*s<entry key=\"%u\">\n", indent+2, "", n->keys[i]);
                dump_node(n->child+i, f, indent+4);
                fprintf(f, "%*s</entry>\n", indent+2, "");
            }
            fprintf(f, "%*s</range-table>\n", indent, "");
            break;

        case RBNK_ENTRY_INDEX:
            fprintf(f, "%*s<index-table min=\"%u\" max=\"%u\">\n", indent, "",
                n->key_min, n->key_min + (n->n_child ? n->n_child-1 : 0));
            for ( uint i = 0; i < n->n_child; i++ )
            {
                fprintf(f, "%*s<entry key=\"%u\">\n", indent+2, "", n->key_min + i);
                dump_node(n->child+i, f, indent+4);
                fprintf(f, "%*s</entry>\n", indent+2, "");
            }
            fprintf(f, "%*s</index-table>\n", indent, "");
            break;

        case RBNK_ENTRY_NULL:
            fprintf(f, "%*s<null/>\n", indent, "");
            break;

        default:
            fprintf(f, "%*s<invalid/>\n", indent, "");
            break;
    }
}

enumError DumpRBNK_XML ( const rbnk_t *rbnk, FILE *f, ccp source_name )
{
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<rbnk source=\"%s\" version=\"%u.%u\" n-program=\"%u\" n-wave=\"%u\">\n",
        source_name ? source_name : "", rbnk->version_major, rbnk->version_minor,
        rbnk->n_program, rbnk->n_wave );

    fprintf(f, "  <programs>\n");
    for ( uint i = 0; i < rbnk->n_program; i++ )
    {
        if ( rbnk->program[i].type == RBNK_ENTRY_INVALID )
            continue;
        fprintf(f, "    <program index=\"%u\">\n", i);
        dump_node(rbnk->program+i, f, 6);
        fprintf(f, "    </program>\n");
    }
    fprintf(f, "  </programs>\n");

    if ( rbnk->wave )
    {
        fprintf(f, "  <waves>\n");
        for ( uint i = 0; i < rbnk->n_wave; i++ )
        {
            const rbnk_wave_t *w = rbnk->wave+i;
            ccp enc = w->encoding==2 ? "ADPCM_THP" : w->encoding==1 ? "PCM16" : "PCM8";
            fprintf(f, "    <wave index=\"%u\" encoding=\"%s\" channels=\"%u\" sample-rate=\"%u\""
                       " samples=\"%lld\" loop=\"%s\"", i, enc, w->channels, w->sample_rate,
                (long long)w->n_samples, w->looped ? "yes" : "no" );
            if (w->looped)
                fprintf(f, " loop-start=\"%lld\"", (long long)w->loop_start);
            fprintf(f, "/>\n");
        }
        fprintf(f, "  </waves>\n");
    }

    fprintf(f, "</rbnk>\n");
    return ERR_OK;
}
