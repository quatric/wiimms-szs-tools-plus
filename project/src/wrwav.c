// wrwav - Wiimms RWAV Tool
// Converts Wii RWAV (single wave sample -- the format RWAR wave archives and
// RBNK instrument banks actually reference) to/from WAV, and reports info.
// The container/codec logic is a documented port of BrawlLib's real RWAV
// parser (soopercool101/BrawlCrate) -- see lib-rwav.h for provenance.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-rwav.h"

static u32 rd_le32 ( const u8 *p ) { return (u32)p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24; }
static u16 rd_le16 ( const u8 *p ) { return (u16)(p[0] | p[1]<<8); }

// Minimal PCM16 WAV reader: one fmt chunk (any channel count), one data chunk.
static enumError LoadWavPCM16 ( rwav_audio_t *audio, const u8 *data, size_t size )
{
    memset(audio, 0, sizeof(*audio));
    if ( size < 44 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4) )
        return ERROR0(ERR_INVALID_DATA, "LoadWavPCM16: not a RIFF/WAVE file\n");

    size_t pos = 12;
    int channels = 0, sample_rate = 0, bits = 0;
    const u8 *pcm = 0; size_t pcm_size = 0;

    while ( pos + 8 <= size )
    {
        char tag[5] = {0};
        memcpy(tag, data + pos, 4);
        u32 chunk_size = rd_le32(data + pos + 4);
        const u8 *body = data + pos + 8;
        if ( pos + 8 + chunk_size > size )
            break;

        if ( !memcmp(tag, "fmt ", 4) && chunk_size >= 16 )
        {
            channels = rd_le16(body + 2);
            sample_rate = rd_le32(body + 4);
            bits = rd_le16(body + 14);
        }
        else if ( !memcmp(tag, "data", 4) )
        {
            pcm = body;
            pcm_size = chunk_size;
        }

        pos += 8 + chunk_size + (chunk_size & 1);
    }

    if ( !channels || !pcm || bits != 16 )
        return ERROR0(ERR_INVALID_DATA, "LoadWavPCM16: need a 16-bit PCM fmt+data chunk\n");
    if ( channels > DSP_ADPCM_MAX_CHANNELS )
        return ERROR0(ERR_INVALID_DATA, "LoadWavPCM16: too many channels (%d)\n", channels);

    s64 n_samples = pcm_size / 2 / channels;
    audio->channels = channels;
    audio->sample_rate = sample_rate;
    audio->n_samples = n_samples;

    for ( int ch = 0; ch < channels; ch++ )
    {
        audio->pcm[ch] = MALLOC(n_samples * sizeof(s16));
        for ( s64 i = 0; i < n_samples; i++ )
            audio->pcm[ch][i] = (s16)rd_le16(pcm + (i * channels + ch) * 2);
    }
    return ERR_OK;
}

static void WriteWavPCM16 ( u8 **out_data, size_t *out_size, const rwav_audio_t *audio )
{
    s64 data_bytes = audio->n_samples * audio->channels * 2;
    size_t size = 44 + data_bytes;
    u8 *buf = MALLOC(size);
    u8 *p = buf;

    memcpy(p, "RIFF", 4); p += 4;
    u32 riff_size = (u32)(size - 8);
    memcpy(p, &riff_size, 4); p += 4;
    memcpy(p, "WAVEfmt ", 8); p += 8;
    u32 fmt_size = 16; memcpy(p, &fmt_size, 4); p += 4;
    u16 audio_fmt = 1; memcpy(p, &audio_fmt, 2); p += 2;
    u16 channels = (u16)audio->channels; memcpy(p, &channels, 2); p += 2;
    u32 rate = (u32)audio->sample_rate; memcpy(p, &rate, 4); p += 4;
    u32 byte_rate = rate * audio->channels * 2; memcpy(p, &byte_rate, 4); p += 4;
    u16 block_align = (u16)(audio->channels * 2); memcpy(p, &block_align, 2); p += 2;
    u16 bits = 16; memcpy(p, &bits, 2); p += 2;
    memcpy(p, "data", 4); p += 4;
    u32 dsize = (u32)data_bytes; memcpy(p, &dsize, 4); p += 4;

    for ( s64 i = 0; i < audio->n_samples; i++ )
        for ( int ch = 0; ch < audio->channels; ch++ )
        {
            s16 s = audio->pcm[ch][i];
            *p++ = (u8)s; *p++ = (u8)(s >> 8);
        }

    *out_data = buf;
    *out_size = size;
}

static void print_usage ( ccp prog )
{
    printf("wrwav - Wiimms RWAV Tool\n"
           "Converts Wii RWAV (single wave sample) audio to/from WAV.\n\n"
           "Usage:\n"
           "  %s to_wav   <input.rwav> [output.wav]\n"
           "  %s from_wav <input.wav> [output.rwav] [--pcm]\n"
           "  %s info     <input.rwav>\n\n"
           "from_wav encodes ADPCM_THP (compressed, matching real assets) by\n"
           "default; --pcm writes raw 16-bit PCM instead.\n", prog, prog, prog);
}

int main ( int argc, char **argv )
{
    stdlog = stderr; // unset otherwise (this tool skips wszst.c's usual startup init)

    if ( argc < 3 )
    {
        print_usage(argv[0]);
        return ERR_SYNTAX;
    }

    ccp cmd = argv[1];
    ccp input_path = argv[2];
    ccp output_path = 0;
    bool use_pcm = false;

    for ( int i = 3; i < argc; i++ )
    {
        if ( !strcmp(argv[i], "--pcm") ) use_pcm = true;
        else if ( !output_path ) output_path = argv[i];
    }

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
    if ( err )
    {
        fprintf(stderr, "Error: failed to load input file '%s'\n", input_path);
        return err;
    }

    if ( !strcasecmp(cmd, "to_wav") || !strcasecmp(cmd, "to-wav") )
    {
        char out_buf[PATH_MAX];
        if ( !output_path )
        {
            snprintf(out_buf, sizeof(out_buf), "%s.wav", input_path);
            output_path = out_buf;
        }

        rwav_audio_t audio;
        err = DecodeRWAV(&audio, raw, raw_size);
        if ( !err )
        {
            u8 *wav = 0; size_t wav_size = 0;
            WriteWavPCM16(&wav, &wav_size, &audio);

            File_t F;
            err = CreateFileOpt(&F, true, output_path, false, input_path);
            if ( F.f && fwrite(wav, 1, wav_size, F.f) != wav_size )
                err = FILEERROR1(&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", wav_size, output_path);
            ResetFile(&F, 0);
            FREE(wav);
            FreeRWAVAudio(&audio);
            if ( !err )
                printf("wrwav: decoded %s -> %s (%d ch, %d Hz, %lld samples, %s)\n",
                    input_path, output_path, audio.channels, audio.sample_rate,
                    (long long)audio.n_samples,
                    audio.encoding == 2 ? "ADPCM" : audio.encoding == 1 ? "PCM16" : "PCM8");
        }
    }
    else if ( !strcasecmp(cmd, "from_wav") || !strcasecmp(cmd, "from-wav") )
    {
        char out_buf[PATH_MAX];
        if ( !output_path )
        {
            snprintf(out_buf, sizeof(out_buf), "%s.rwav", input_path);
            output_path = out_buf;
        }

        rwav_audio_t audio;
        err = LoadWavPCM16(&audio, raw, raw_size);
        if ( !err )
        {
            u8 *bin = 0; size_t bin_size = 0;
            err = EncodeRWAV(&bin, &bin_size, &audio, !use_pcm);
            FreeRWAVAudio(&audio);
            if ( !err )
            {
                File_t F;
                err = CreateFileOpt(&F, true, output_path, false, input_path);
                if ( F.f && fwrite(bin, 1, bin_size, F.f) != bin_size )
                    err = FILEERROR1(&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", bin_size, output_path);
                ResetFile(&F, 0);
                FREE(bin);
                if ( !err )
                    printf("wrwav: encoded %s -> %s (%zu bytes, %s)\n",
                        input_path, output_path, bin_size, use_pcm ? "PCM16" : "ADPCM");
            }
        }
    }
    else if ( !strcasecmp(cmd, "info") )
    {
        rwav_audio_t audio;
        err = DecodeRWAV(&audio, raw, raw_size);
        if ( !err )
        {
            printf("File: %s\nChannels: %d\nSample rate: %d\nSamples: %lld\nCodec: %s\nLoop: %s",
                input_path, audio.channels, audio.sample_rate, (long long)audio.n_samples,
                audio.encoding == 2 ? "ADPCM_THP" : audio.encoding == 1 ? "PCM16" : "PCM8",
                audio.loop ? "yes" : "no");
            if ( audio.loop )
                printf(" (start sample %lld)", (long long)audio.loop_start);
            printf("\n");
            FreeRWAVAudio(&audio);
        }
    }
    else
    {
        fprintf(stderr, "Unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        err = ERR_SYNTAX;
    }

    FREE(raw);
    return err;
}

bool DefineIntVar ( VarMap_t * vm, ccp varname, int value ) { return false; }
