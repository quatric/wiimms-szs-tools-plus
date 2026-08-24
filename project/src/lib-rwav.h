#ifndef SZS_LIB_RWAV_H
#define SZS_LIB_RWAV_H 1

#include "lib-std.h"
#include "lib-dspadpcm.h"

// RWAV -- Nintendo's Wii single-wave-sample container (the individual
// sample entries a Wii RWAR wave archive or RBNK instrument bank actually
// points at; RSAR's own FILE block carries these as opaque blobs today,
// see lib-brsar.c). Same NW4R "chunked binary" shape as BRSTM (see
// lib-brstm.h) but one non-streamed sample instead of a streamed,
// blocked file: NW4RCommonHeader + INFO chunk (WaveInfo + one ChannelInfo/
// ADPCMInfo per channel) + DATA chunk (raw per-channel sample bytes).
//
// Field layout ported from BrawlLib (soopercool101/BrawlCrate)
// SSBB/Types/Audio/RWAV.cs -- RWAV/RWAVInfo/RWAVData/WaveInfo/ChannelInfo/
// ADPCMInfo structs, GPL v3, the real, actively maintained Brawl/RSAR
// modding library this format is documented nowhere else as precisely.
// Codec is the same DSP-ADPCM (lib-dspadpcm.h) BRSTM uses.

typedef struct rwav_audio_t
{
    int      channels;
    int      sample_rate;
    s64      n_samples;
    u8       encoding;      // 0: PCM8, 1: PCM16, 2: ADPCM_THP (WaveInfo._format._encoding)
    bool     loop;
    s64      loop_start;    // samples
    s16     *pcm[DSP_ADPCM_MAX_CHANNELS]; // decoded / to-encode samples, per channel
} rwav_audio_t;

// Encode 'audio' (audio->pcm[ch] must hold audio->n_samples samples each)
// into a binary RWAV. If 'use_adpcm' is set, coefficients are derived from
// the PCM via DspAdpcmCorrelateCoefs() and the stream is ADPCM_THP-encoded
// (audio->encoding is overwritten to 2); otherwise written as raw PCM16
// (audio->encoding overwritten to 1). PCM8 is never chosen by the encoder
// (lossy relative to the PCM16 source with no offsetting benefit) but is
// decoded when found in a real file.
enumError EncodeRWAV (
    u8            **out_data,
    size_t         *out_size,
    const rwav_audio_t *audio,
    bool            use_adpcm
);

// Decode a RWAV binary (DATA/size must start at the "RWAV" tag).
enumError DecodeRWAV (
    rwav_audio_t   *audio,
    const u8       *data,
    size_t          size
);

void FreeRWAVAudio ( rwav_audio_t *audio );

#endif // SZS_LIB_RWAV_H
