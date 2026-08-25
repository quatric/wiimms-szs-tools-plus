#include "lib-rwav.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Local growable byte buffer + big-endian helpers (RWAV is Wii-only, always
// big-endian -- no endian-selectable variant like BFSTM/BCSTM). Mirrors
// lib-brstm.c's own local helpers of the same names/shapes.

typedef struct membuf_t { u8 *data; size_t size, capacity; } membuf_t;

static void mb_init ( membuf_t *mb ) { mb->data = 0; mb->size = mb->capacity = 0; }

static void mb_reserve ( membuf_t *mb, size_t need )
{
    if ( mb->size + need <= mb->capacity )
        return;
    size_t cap = mb->capacity ? mb->capacity : 0x100;
    while ( cap < mb->size + need )
        cap *= 2;
    mb->data = REALLOC(mb->data, cap);
    mb->capacity = cap;
}

static void mb_append ( membuf_t *mb, const void *src, size_t len )
{
    mb_reserve(mb, len);
    memcpy(mb->data + mb->size, src, len);
    mb->size += len;
}

static void mb_u8  ( membuf_t *mb, u8  v ) { mb_append(mb, &v, 1); }
static void mb_u16 ( membuf_t *mb, u16 v ) { u8 b[2] = { (u8)(v>>8), (u8)v }; mb_append(mb, b, 2); }
static void mb_u32 ( membuf_t *mb, u32 v ) { u8 b[4] = { (u8)(v>>24),(u8)(v>>16),(u8)(v>>8),(u8)v }; mb_append(mb, b, 4); }
static void mb_tag ( membuf_t *mb, const char *tag ) { mb_append(mb, tag, 4); }

static void mb_pad_to ( membuf_t *mb, size_t target )
{
    if ( mb->size < target )
    {
        mb_reserve(mb, target - mb->size);
        memset(mb->data + mb->size, 0, target - mb->size);
        mb->size = target;
    }
}

static void mb_free ( membuf_t *mb ) { if ( mb->data ) FREE(mb->data); mb->data = 0; mb->size = mb->capacity = 0; }

// Endian-selectable writers for FWAV/CWAV (RWAV is always big-endian, but
// CWAV -- the 3DS variant of the same container -- is little-endian).
static void mb_u16x ( membuf_t *mb, u16 v, bool be )
{
    u8 b[2];
    if (be) { b[0] = (u8)(v>>8); b[1] = (u8)v; }
    else    { b[0] = (u8)v; b[1] = (u8)(v>>8); }
    mb_append(mb, b, 2);
}

static void mb_u32x ( membuf_t *mb, u32 v, bool be )
{
    u8 b[4];
    if (be) { b[0]=(u8)(v>>24); b[1]=(u8)(v>>16); b[2]=(u8)(v>>8); b[3]=(u8)v; }
    else    { b[3]=(u8)(v>>24); b[2]=(u8)(v>>16); b[1]=(u8)(v>>8); b[0]=(u8)v; }
    mb_append(mb, b, 4);
}

static u32 rd_u32 ( const u8 *p ) { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static u16 rd_u16 ( const u8 *p ) { return (u16)((u16)p[0]<<8 | p[1]); }

// -----------------------------------------------------------------------------
///////////////		    decode			///////////////
// -----------------------------------------------------------------------------

enumError DecodeRWAV ( rwav_audio_t *audio, const u8 *data, size_t size )
{
    memset(audio, 0, sizeof(*audio));

    // NW4RCommonHeader(0x10) + infoOffset/infoLength/dataOffset/dataLength.
    if ( size < 0x20 || memcmp(data, "RWAV", 4) )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: not a RWAV file\n");

    u32 info_off = rd_u32(data + 0x10);
    u32 data_off = rd_u32(data + 0x18);
    if ( (size_t)info_off + 8 > size || memcmp(data + info_off, "INFO", 4) )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: missing INFO chunk\n");
    if ( (size_t)data_off + 8 > size || memcmp(data + data_off, "DATA", 4) )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: missing DATA chunk\n");

    // WaveInfo starts right after INFO's SSBBEntryHeader (tag+length, 8 bytes).
    const u8 *wi = data + info_off + 8;
    if ( (size_t)(wi - data) + 0x1C > size )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: INFO chunk too small\n");

    u8  encoding = wi[0];
    u8  looped   = wi[1];
    u8  channels = wi[2];
    if ( !channels || channels > DSP_ADPCM_MAX_CHANNELS )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: bad channel count %d\n", channels);
    if ( encoding > 2 )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: unsupported encoding %d\n", encoding);

    u16 sample_rate      = rd_u16(wi + 4);
    s32 loop_start_nibble = (s32)rd_u32(wi + 8);
    s32 nibbles           = (s32)rd_u32(wi + 0xC); // includes the 2-nibble ADPCM frame header, ALL data
    u32 ch_table_off      = rd_u32(wi + 0x10);      // relative to 'wi'

    // WaveInfo.Get(): ADPCM nibble count -> sample count (matches BrawlLib's
    // WaveInfo.NumSamples getter exactly: 16 nibbles/frame -> 14 samples/frame).
    s64 n_samples = encoding == 2
        ? (s64)nibbles / 16 * 14 + ( nibbles % 16 - 2 )
        : nibbles;
    if ( n_samples < 0 )
        n_samples = 0;
    s64 loop_start = encoding == 2
        ? (s64)loop_start_nibble / 16 * 14 + ( loop_start_nibble % 16 - 2 )
        : loop_start_nibble;

    if ( (size_t)(wi - data) + ch_table_off + 4u * channels > size )
        return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: channel table exceeds INFO chunk\n");
    const u8 *ch_table = wi + ch_table_off;

    s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
    const u8 *ch_data[DSP_ADPCM_MAX_CHANNELS];

    const u8 *data_base = data + data_off + 8; // DATA content, right after its SSBBEntryHeader

    for ( int ch = 0; ch < channels; ch++ )
    {
        u32 ci_off = rd_u32(ch_table + 4 * ch); // relative to 'wi'
        if ( (size_t)(wi - data) + ci_off + 0x1C > size )
            return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: ChannelInfo %d exceeds INFO chunk\n", ch);
        const u8 *ci = wi + ci_off;

        u32 ch_data_off = rd_u32(ci); // relative to 'wi', per BrawlLib's ChannelInfo._channelDataOffset
        if ( (size_t)(data_base - data) + ch_data_off > size )
            return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: channel %d data offset out of range\n", ch);
        ch_data[ch] = data_base + ch_data_off;

        if ( encoding == 2 )
        {
            u32 ai_off = rd_u32(ci + 4); // relative to 'wi', per ChannelInfo._adpcmInfoOffset
            if ( (size_t)(wi - data) + ai_off + 0x20 > size )
                return ERROR0(ERR_INVALID_DATA, "DecodeRWAV: ADPCMInfo %d exceeds INFO chunk\n", ch);
            const u8 *ai = wi + ai_off;
            for ( int i = 0; i < 16; i++ )
                coefs[ch][i] = (s16)rd_u16(ai + i * 2);
        }
    }

    audio->channels    = channels;
    audio->sample_rate = sample_rate;
    audio->n_samples   = n_samples;
    audio->encoding    = encoding;
    audio->loop        = looped != 0;
    audio->loop_start  = loop_start;

    for ( int ch = 0; ch < channels; ch++ )
    {
        audio->pcm[ch] = MALLOC(n_samples ? n_samples * sizeof(s16) : sizeof(s16));

        if ( encoding == 2 )
        {
            int h1 = 0, h2 = 0;
            s64 nframes = DspAdpcmFrameCount(n_samples);
            for ( s64 f = 0; f < nframes; f++ )
            {
                s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
                int count = (int)( n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
                                    ? n_samples - off : DSP_ADPCM_SAMPLES_PER_FRAME );
                DspAdpcmDecodeBlock(ch_data[ch] + f * DSP_ADPCM_BYTES_PER_FRAME, count,
                                     audio->pcm[ch] + off, coefs[ch], &h1, &h2);
            }
        }
        else if ( encoding == 1 ) // PCM16
        {
            for ( s64 i = 0; i < n_samples; i++ )
                audio->pcm[ch][i] = (s16)rd_u16(ch_data[ch] + i * 2);
        }
        else // PCM8, signed 8-bit per BrawlLib/real Wii samples (not the WAV-style unsigned convention)
        {
            for ( s64 i = 0; i < n_samples; i++ )
                audio->pcm[ch][i] = (s16)( (s8)ch_data[ch][i] ) << 8;
        }
    }

    return ERR_OK;
}

// -----------------------------------------------------------------------------
///////////////		    encode			///////////////
// -----------------------------------------------------------------------------

enumError EncodeRWAV
(
    u8            **out_data,
    size_t         *out_size,
    const rwav_audio_t *audio_in,
    bool            use_adpcm
)
{
    if ( !audio_in->channels || audio_in->channels > DSP_ADPCM_MAX_CHANNELS )
        return ERROR0(ERR_INVALID_DATA, "EncodeRWAV: bad channel count %d\n", audio_in->channels);

    rwav_audio_t audio = *audio_in;
    audio.encoding = use_adpcm ? 2 : 1;

    s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
    u8 *adpcm_data[DSP_ADPCM_MAX_CHANNELS] = {0};
    s64 adpcm_bytes = 0;

    if ( use_adpcm )
    {
        adpcm_bytes = DspAdpcmByteCount(audio.n_samples);
        for ( int ch = 0; ch < audio.channels; ch++ )
        {
            DspAdpcmCorrelateCoefs(audio.pcm[ch], audio.n_samples, coefs[ch]);
            adpcm_data[ch] = MALLOC( adpcm_bytes ? adpcm_bytes : 1 );
            int h1 = 0, h2 = 0;
            s64 nframes = DspAdpcmFrameCount(audio.n_samples);
            for ( s64 f = 0; f < nframes; f++ )
            {
                s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
                int count = (int)( audio.n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
                                    ? audio.n_samples - off : DSP_ADPCM_SAMPLES_PER_FRAME );
                DspAdpcmEncodeBlock(audio.pcm[ch] + off, count,
                                     adpcm_data[ch] + f * DSP_ADPCM_BYTES_PER_FRAME,
                                     coefs[ch], &h1, &h2);
            }
        }
    }

    // -- INFO chunk: SSBBEntryHeader(8) + WaveInfo(0x1C) + per-channel offset
    // table (4 bytes each) + per-channel ChannelInfo(0x1C) [+ ADPCMInfo(0x30)].
    const uint ch_table_off = 0x1C; // right after WaveInfo, matches BrawlLib's own layout
    const uint ci_size = 0x1C;
    const uint ai_size = use_adpcm ? 0x30 : 0;
    const uint ci_base = ch_table_off + 4 * audio.channels;
    const uint ai_base = ci_base + ci_size * audio.channels;
    const uint info_body_size = ai_base + ai_size * audio.channels;
    const uint info_chunk_size = ((8 + info_body_size) + 3) & ~3u;

    s64 nibbles = use_adpcm ? DspAdpcmNibbleCount(audio.n_samples) : 0;

    membuf_t out;
    mb_init(&out);

    // NW4RCommonHeader + 4 following u32 offsets/lengths (RWAV header, 0x20).
    mb_tag(&out, "RWAV");
    mb_u16(&out, 0xFEFF);
    mb_u8(&out, 1); mb_u8(&out, 0); // version 1.0
    mb_u32(&out, 0); // total file length, patched below
    mb_u16(&out, 0x20); mb_u16(&out, 2); // header size, chunk count
    mb_u32(&out, 0x20); mb_u32(&out, info_chunk_size);
    mb_u32(&out, 0); mb_u32(&out, 0); // data offset/length, patched below

    // -- INFO chunk
    const size_t info_start = out.size;
    mb_tag(&out, "INFO");
    mb_u32(&out, info_chunk_size);
    mb_u8(&out, audio.encoding); mb_u8(&out, audio.loop ? 1 : 0);
    mb_u8(&out, (u8)audio.channels); mb_u8(&out, 0);
    mb_u16(&out, (u16)audio.sample_rate);
    mb_u8(&out, 0); mb_u8(&out, 0); // dataLocationType, pad
    mb_u32(&out, use_adpcm ? (u32)audio.loop_start : (u32)audio.loop_start);
    mb_u32(&out, use_adpcm ? (u32)nibbles : (u32)audio.n_samples);
    mb_u32(&out, ch_table_off);
    mb_u32(&out, 0); mb_u32(&out, 0); // dataLocation, reserved

    for ( int ch = 0; ch < audio.channels; ch++ )
        mb_u32(&out, ci_base + ci_size * ch);

    s64 pcm16_bytes_per_ch = audio.n_samples * 2;
    s64 pcm8_bytes_per_ch  = audio.n_samples;
    s64 ch_bytes = use_adpcm ? adpcm_bytes
                 : audio.encoding == 1 ? pcm16_bytes_per_ch : pcm8_bytes_per_ch;

    for ( int ch = 0; ch < audio.channels; ch++ )
    {
        mb_u32(&out, (u32)(ch * ch_bytes));                                   // channelDataOffset (rel. WaveInfo)
        mb_u32(&out, use_adpcm ? (u32)(ai_base + ai_size * ch) : 0);          // adpcmInfoOffset
        mb_u32(&out, 1); mb_u32(&out, 1); mb_u32(&out, 1); mb_u32(&out, 1);  // volumes, unused by this encoder
        mb_u32(&out, 0);
    }

    if ( use_adpcm )
        for ( int ch = 0; ch < audio.channels; ch++ )
        {
            for ( int i = 0; i < 16; i++ )
                mb_u16(&out, (u16)coefs[ch][i]);
            mb_u16(&out, 0);                    // gain
            mb_u16(&out, adpcm_bytes ? (u16)adpcm_data[ch][0] : 0); // ps: frame 0's own predictor/scale byte
            mb_u16(&out, 0); mb_u16(&out, 0);  // yn1/yn2 (stream starts from silence)
            mb_u16(&out, 0); mb_u16(&out, 0); mb_u16(&out, 0); // loop ps/yn1/yn2, unused (no ADPCM loop-history support here)
            mb_u16(&out, 0);                    // pad
        }

    mb_pad_to(&out, info_start + info_chunk_size);

    // -- DATA chunk
    const size_t data_start = out.size;
    mb_tag(&out, "DATA");
    mb_u32(&out, 0); // length, patched below
    for ( int ch = 0; ch < audio.channels; ch++ )
    {
        if ( use_adpcm )
            mb_append(&out, adpcm_data[ch], adpcm_bytes);
        else if ( audio.encoding == 1 )
            for ( s64 i = 0; i < audio.n_samples; i++ )
                mb_u16(&out, (u16)audio.pcm[ch][i]);
        else
            for ( s64 i = 0; i < audio.n_samples; i++ )
                mb_u8(&out, (u8)(s8)(audio.pcm[ch][i] >> 8));
    }
    mb_pad_to(&out, (out.size + 0x1F) & ~(size_t)0x1F);
    const size_t data_chunk_size = out.size - data_start;

    // Patch the file-length and DATA offset/length fields now that both
    // chunks' real sizes are known.
    u8 *d = out.data;
    d[8]  = (u8)(out.size>>24); d[9]  = (u8)(out.size>>16); d[10] = (u8)(out.size>>8); d[11] = (u8)out.size;
    d[0x18] = (u8)(data_start>>24); d[0x19] = (u8)(data_start>>16); d[0x1a] = (u8)(data_start>>8); d[0x1b] = (u8)data_start;
    d[0x1c] = (u8)(data_chunk_size>>24); d[0x1d] = (u8)(data_chunk_size>>16); d[0x1e] = (u8)(data_chunk_size>>8); d[0x1f] = (u8)data_chunk_size;
    d[info_start+4] = (u8)(info_chunk_size>>24); d[info_start+5] = (u8)(info_chunk_size>>16);
    d[info_start+6] = (u8)(info_chunk_size>>8);  d[info_start+7] = (u8)info_chunk_size;
    d[data_start+4] = (u8)(data_chunk_size>>24); d[data_start+5] = (u8)(data_chunk_size>>16);
    d[data_start+6] = (u8)(data_chunk_size>>8);  d[data_start+7] = (u8)data_chunk_size;

    for ( int ch = 0; ch < audio.channels; ch++ )
        FREE(adpcm_data[ch]);

    *out_data = out.data;
    *out_size = out.size;
    return ERR_OK;
}

// -----------------------------------------------------------------------------
// FWAV (Wii U/Switch, big-endian) / CWAV (3DS, little-endian) encode
 // -----------------------------------------------------------------------------

enumError EncodeBXWAV
(
    u8            **out_data,
    size_t         *out_size,
    const rwav_audio_t *audio_in,
    bool            use_adpcm,
    bool            cwav
)
{
    if ( !audio_in->channels || audio_in->channels > DSP_ADPCM_MAX_CHANNELS )
        return ERROR0(ERR_INVALID_DATA, "EncodeBXWAV: bad channel count %d\n", audio_in->channels);

    const bool be = !cwav;

    rwav_audio_t audio = *audio_in;
    audio.encoding = use_adpcm ? 2 : 1;

    s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
    u8 *adpcm_data[DSP_ADPCM_MAX_CHANNELS] = {0};
    s64 adpcm_bytes = 0;

    if ( use_adpcm )
    {
        adpcm_bytes = DspAdpcmByteCount(audio.n_samples);
        for ( int ch = 0; ch < audio.channels; ch++ )
        {
            DspAdpcmCorrelateCoefs(audio.pcm[ch], audio.n_samples, coefs[ch]);
            adpcm_data[ch] = MALLOC( adpcm_bytes ? adpcm_bytes : 1 );
            int h1 = 0, h2 = 0;
            s64 nframes = DspAdpcmFrameCount(audio.n_samples);
            for ( s64 f = 0; f < nframes; f++ )
            {
                s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
                int count = (int)( audio.n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
                                    ? audio.n_samples - off : DSP_ADPCM_SAMPLES_PER_FRAME );
                DspAdpcmEncodeBlock(audio.pcm[ch] + off, count,
                                     adpcm_data[ch] + f * DSP_ADPCM_BYTES_PER_FRAME,
                                     coefs[ch], &h1, &h2);
            }
        }
    }

    // -- Layout constants observed in every inspected retail file:
    // header is a fixed 0x40-byte NW4RHeader whose block table points at the
    // INFO/DATA blocks; INFO content holds WaveInfo (0x18), one reference
    // pair per channel, then per-channel ChannelInfo (0x14) [+ ADPCMInfo
    // (0x2E)]; each channel's stream inside DATA is preceded by 24 pad bytes.
    const uint info_off = 0x40;
    const uint wi_size      = 0x18;
    const uint entry_size   = 8;   // {u32 ref marker 0x71000000, u32 rel->ChannelInfo}
    const uint ci_size      = 0x14; // flags/pad/dataoff/marker/adpcm-ref
    const uint ai_size      = 0x2E; // coefs[16] + gain/ps/yn1/yn2 + loop ps/yn1/yn2
    const uint stream_pad   = 24;

    const uint entries_off  = wi_size;
    const uint ci_base      = entries_off + entry_size * audio.channels;
    const uint ai_base      = ci_base + ci_size * audio.channels;    uint info_body_size     = ai_base + ( use_adpcm ? ai_size * audio.channels : 0 );
    // Real files pad the whole INFO chunk (tag included) to a multiple of 32.
    const uint info_chunk_size = ( 8 + info_body_size + 31 ) & ~31u;
    info_body_size = info_chunk_size - 8;
    const uint data_off     = info_off + info_chunk_size;

    s64 stream_bytes = use_adpcm ? adpcm_bytes
                     : audio.encoding == 1 ? audio.n_samples * 2
                     : audio.n_samples;
    const u64 data_content = stream_pad * (u64)audio.channels + stream_bytes * (u64)audio.channels;
    const u64 data_chunk_size = 8 + data_content;
    const u64 file_size = (u64)data_off + data_chunk_size;

    membuf_t out;
    mb_init(&out);

    // -- NW4RHeader (fixed 0x40 bytes)
    mb_append(&out, cwav ? "CWAV" : "FWAV", 4);
    mb_u16x(&out, 0xFEFF, be);      // BOM, native byte order
    mb_u16x(&out, 0x0040, be);      // version 0.64 as seen in real files
    mb_u32x(&out, 0x00010100, be);  // constant marker in all inspected files
    mb_u32x(&out, 0, be);           // file size, patched below
    mb_u16x(&out, 2, be);           // block count: INFO + DATA
    mb_u16x(&out, 0, be);
    mb_u32x(&out, 0x70000000, be);  // INFO block reference marker
    mb_u32x(&out, info_off, be);
    mb_u32x(&out, info_chunk_size, be);
    mb_u32x(&out, 0x70010000, be);  // DATA block reference marker
    mb_u32x(&out, data_off, be);
    mb_u32x(&out, 0, be);           // DATA size, patched below
    while ( out.size < info_off )
        mb_u8(&out, 0);

    // -- INFO block
    const size_t info_start = out.size;
    mb_tag(&out, "INFO");
    mb_u32x(&out, info_chunk_size, be);
    mb_u8(&out, audio.encoding);
    mb_u8(&out, audio.loop ? 1 : 0);
    mb_u8(&out, 0); mb_u8(&out, 0);
    mb_u32x(&out, (u32)audio.sample_rate, be);
    mb_u32x(&out, (u32)audio.loop_start, be);   // loop start in samples
    mb_u32x(&out, (u32)audio.n_samples, be);
    mb_u32x(&out, 0, be);
    mb_u32x(&out, (u32)audio.channels, be);     // channel table anchor

    for ( int ch = 0; ch < audio.channels; ch++ )
    {
        mb_u32x(&out, 0x71000000, be);          // reference marker
        // stored relative to the channel-table anchor (content+0x14)
        mb_u32x(&out, 4 + entry_size * audio.channels + ci_size * ch, be);
    }

    for ( int ch = 0; ch < audio.channels; ch++ )
    {
        mb_u16x(&out, 0x1F00, be);              // flags present in all real files
        mb_u16x(&out, 0, be);
        mb_u32x(&out, (u32)( stream_pad + ch * ( stream_pad + stream_bytes ) ), be);
        mb_u32x(&out, 0x03000000, be);          // constant marker in all real files
        // ADPCM reference is stored relative to this ChannelInfo itself
        mb_u32x(&out, use_adpcm
                        ? (u32)( ai_base + ai_size * ch - ( ci_base + ci_size * ch ) )
                        : 0xFFFFFFFF, be);
        mb_u32x(&out, 0, be);                   // reserved word after every ChannelInfo
    }                                           // (real files stride the table by 0x14)

    if ( use_adpcm )
        for ( int ch = 0; ch < audio.channels; ch++ )
        {
            for ( int i = 0; i < 16; i++ )
                mb_u16x(&out, (u16)coefs[ch][i], be);
            mb_u16x(&out, 0, be);               // gain
            mb_u16x(&out, 0, be);               // predictor/scale (stream starts from silence)
            mb_u16x(&out, 0, be); mb_u16x(&out, 0, be);  // yn1/yn2
            mb_u16x(&out, 0, be);               // loop ps
            mb_u16x(&out, 0, be); mb_u16x(&out, 0, be);  // loop yn1/yn2
        }

    while ( out.size < info_start + info_chunk_size )
        mb_u8(&out, 0);

    // -- DATA block
    const size_t data_start = out.size;
    mb_tag(&out, "DATA");
    mb_u32x(&out, 0, be); // patched below
    for ( int ch = 0; ch < audio.channels; ch++ )
    {
        for ( uint i = 0; i < stream_pad; i++ )
            mb_u8(&out, 0);
        if ( use_adpcm )
            mb_append(&out, adpcm_data[ch], adpcm_bytes);
        else if ( audio.encoding == 1 )
            for ( s64 i = 0; i < audio.n_samples; i++ )
                mb_u16x(&out, (u16)audio.pcm[ch][i], be);
        else
            for ( s64 i = 0; i < audio.n_samples; i++ )
                mb_u8(&out, (u8)(s8)(audio.pcm[ch][i] >> 8));
    }
    if ( out.size != data_start + data_chunk_size )
        return ERROR0(ERR_INTERNAL, "EncodeBXWAV: DATA size mismatch (%zu != %llu)\n",
                        out.size - data_start, (unsigned long long)data_chunk_size);

    // Patch the file length and DATA size now that both blocks' sizes are known.
    #define PATCH32(off,val) do { \
        u32 v_ = (val); size_t o_ = (off); \
        if (be) { out.data[o_]  =(u8)(v_>>24); out.data[o_+1]=(u8)(v_>>16); \
                  out.data[o_+2]=(u8)(v_>>8);  out.data[o_+3]=(u8)v_; } \
        else    { out.data[o_+3]=(u8)(v_>>24); out.data[o_+2]=(u8)(v_>>16); \
                  out.data[o_+1]=(u8)(v_>>8);  out.data[o_]  =(u8)v_; } \
    } while(0)
    PATCH32(0x0C, file_size);
    PATCH32(0x28, data_chunk_size);
    PATCH32(data_start + 4, data_chunk_size);
    #undef PATCH32

    for ( int ch = 0; ch < audio.channels; ch++ )
        FREE(adpcm_data[ch]);

    *out_data = out.data;
    *out_size = out.size;
    return ERR_OK;
}

void FreeRWAVAudio ( rwav_audio_t *audio )
{
    for ( int ch = 0; ch < DSP_ADPCM_MAX_CHANNELS; ch++ )
    {
        FREE(audio->pcm[ch]);
        audio->pcm[ch] = 0;
    }
}
