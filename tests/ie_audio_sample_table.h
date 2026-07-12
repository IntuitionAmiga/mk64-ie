/* Test-only stand-in for the generated IE audio sample table header. */
#ifndef TEST_IE_AUDIO_SAMPLE_TABLE_H
#define TEST_IE_AUDIO_SAMPLE_TABLE_H

#include <stdint.h>

typedef struct {
    uint32_t src_offset;
    uint32_t pcm_offset;
    uint32_t byte_len;
    uint32_t sample_count;
    uint32_t loop_start_bytes;
    uint32_t loop_len_bytes;
    uint32_t loop_count;
} IEAudioSampleDesc;

#define IE_AUDIO_SAMPLE_COUNT 1
#define IE_AUDIO_PCM16_SIZE 0u
#define IE_AUDIO_TABLE_BASE_OFFSET 0u
#define IE_SYNTH_WAVEFORM_COUNT 6
#define IE_SYNTH_WAVE_SAMPLES 256
#define IE_SYNTH_WAVE_BYTES (IE_SYNTH_WAVE_SAMPLES * 2)
#define IE_SYNTH_WAVE_HARMONICS 4
#define IE_SYNTH_WAVE_SEGMENT_SAMPLES 64
#define IE_SYNTH_WAVE_SEGMENT_BYTES (IE_SYNTH_WAVE_SEGMENT_SAMPLES * 2)

extern const IEAudioSampleDesc gIEAudioSampleTable[IE_AUDIO_SAMPLE_COUNT];
extern const uint8_t gIESynthWaveLE[IE_SYNTH_WAVEFORM_COUNT][IE_SYNTH_WAVE_BYTES];

#endif /* TEST_IE_AUDIO_SAMPLE_TABLE_H */
