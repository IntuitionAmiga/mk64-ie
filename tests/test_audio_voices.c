#include "test_support.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "audio/audio_api.h"
#include "audio/data.h"
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/synthesis.h"
#include "ie/ie_mmio.h"
#include "ie/ie_perf_counters.h"
#include "ie_audio_sample_table.h"

extern struct AudioAPI ie_audio_api;
void ie_audio_voice_frame_begin(void);
void ie_audio_voice_update(s32 updateIndex);
void ie_audio_voice_reset_all(void);

#define MAX_WRITES 256

struct MmioWrite {
    uint32_t addr;
    uint32_t value;
};

static struct MmioWrite writes[MAX_WRITES];
static size_t num_writes;
static uint32_t ctrl_readback[IE_SFX_CHANNELS];

static struct NoteSubEu note_subs[1];
static struct Note notes[1];
static s16 synth_waves[IE_SYNTH_WAVEFORM_COUNT][IE_SYNTH_WAVE_SAMPLES];

struct NoteSubEu* gNoteSubsEu = note_subs;
struct Note* gNotes = notes;
s32 gMaxSimultaneousNotes = 1;
struct AudioBufferParametersEU gAudioBufferParameters = { 0 };
ALSeqFile* gAlTbl = NULL;
u8* _audio_tablesPcm16SegmentRomStart = NULL;
s16* gWaveSamples[IE_SYNTH_WAVEFORM_COUNT] = {
    synth_waves[0], synth_waves[1], synth_waves[2],
    synth_waves[3], synth_waves[4], synth_waves[5],
};
const IEAudioSampleDesc gIEAudioSampleTable[IE_AUDIO_SAMPLE_COUNT] = { { 0 } };
const uint8_t gIESynthWaveLE[IE_SYNTH_WAVEFORM_COUNT][IE_SYNTH_WAVE_BYTES] = { { 0 } };

void platform_log(const char* fmt, ...) {
    (void) fmt;
}

void platform_fatal(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    printf("platform_fatal called: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    exit(2);
}

uint64_t ie_time_usec(void) {
    return 0;
}

void func_800B6FB4(s32 updateIndexStart, s32 noteIndex) {
    (void) updateIndexStart;
    (void) noteIndex;
}

void ie_mmio_host_write32(uint32_t addr, uint32_t value) {
    if (num_writes < MAX_WRITES) {
        writes[num_writes].addr = addr;
        writes[num_writes].value = value;
        num_writes++;
    }
    if (addr >= IE_SFX_CH_BASE(0) && addr < IE_SFX_CH_BASE(IE_SFX_CHANNELS)) {
        uint32_t ch = (addr - IE_SFX_CH_BASE(0)) / IE_SFX_CHANNEL_STRIDE;
        uint32_t off = (addr - IE_SFX_CH_BASE(0)) % IE_SFX_CHANNEL_STRIDE;
        if (off == IE_SFX_OFF_CTRL) {
            ctrl_readback[ch] = value;
        }
    }
}

uint32_t ie_mmio_host_read32(uint32_t addr) {
    if (addr >= IE_SFX_CH_BASE(0) && addr < IE_SFX_CH_BASE(IE_SFX_CHANNELS)) {
        uint32_t ch = (addr - IE_SFX_CH_BASE(0)) / IE_SFX_CHANNEL_STRIDE;
        uint32_t off = (addr - IE_SFX_CH_BASE(0)) % IE_SFX_CHANNEL_STRIDE;
        if (off == IE_SFX_OFF_CTRL) {
            return ctrl_readback[ch];
        }
    }
    return 0;
}

static void reset_capture(void) {
    num_writes = 0;
    ie_perf_reset();
}

static size_t count_writes_for(uint32_t addr) {
    size_t i;
    size_t count = 0;

    for (i = 0; i < num_writes; i++) {
        if (writes[i].addr == addr) {
            count++;
        }
    }
    return count;
}

static uint32_t last_value_for(uint32_t addr) {
    size_t i;

    for (i = num_writes; i > 0; i--) {
        if (writes[i - 1].addr == addr) {
            return writes[i - 1].value;
        }
    }
    printf("FAIL %s:%d: no write for addr 0x%08x\n", __FILE__, __LINE__, addr);
    test_failures++;
    return 0;
}

static uint32_t ch_reg(uint32_t off) {
    return IE_SFX_CH_BASE(0) + off;
}

static uint32_t expected_freq(uint16_t rate_fixed) {
    return (uint32_t)(((uint64_t) rate_fixed * gAudioBufferParameters.frequency) >> 15);
}

static uint32_t expected_vol_pan(uint16_t left, uint16_t right) {
    uint32_t sum = (uint32_t) left + right;
    uint32_t vol = (sum * 255u) / (8192u * 16u);
    uint32_t pan = sum == 0 ? 128u : ((uint32_t) right * 255u) / sum;

    if (vol == 0 && sum != 0) {
        vol = 1;
    }
    if (vol > 255u) {
        vol = 255u;
    }
    return vol | (pan << 16);
}

static void set_note(uint16_t rate_fixed, uint16_t left, uint16_t right, uint8_t needs_init) {
    memset(note_subs, 0, sizeof(note_subs));
    memset(notes, 0, sizeof(notes));
    note_subs[0].enabled = 1;
    note_subs[0].needsInit = needs_init;
    note_subs[0].isSyntheticWave = 1;
    note_subs[0].sound.samples = synth_waves[0];
    note_subs[0].resamplingRateFixedPoint = rate_fixed;
    note_subs[0].targetVolLeft = left;
    note_subs[0].targetVolRight = right;
}

static void start_one_voice(void) {
    set_note(0x8000, 4096, 4096, 1);
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);
}

static void test_voice_start_writes_full_register_sequence(void) {
    uint32_t ptr = (uint32_t)(uintptr_t)&gIESynthWaveLE[0][0];
    uint32_t freq = expected_freq(0x8000);
    uint32_t vol_pan = expected_vol_pan(4096, 4096);

    ie_audio_api.init();
    reset_capture();
    start_one_voice();

    EXPECT_EQ_INT(num_writes, 9);
    EXPECT_EQ_INT(writes[0].addr, ch_reg(IE_SFX_OFF_CTRL));
    EXPECT_EQ_INT(writes[0].value, IE_SFX_CTRL_STOP);
    EXPECT_EQ_INT(writes[1].addr, ch_reg(IE_SFX_OFF_PTR));
    EXPECT_EQ_INT(writes[1].value, ptr);
    EXPECT_EQ_INT(writes[2].addr, ch_reg(IE_SFX_OFF_LEN));
    EXPECT_EQ_INT(writes[2].value, IE_SYNTH_WAVE_SEGMENT_BYTES);
    EXPECT_EQ_INT(writes[3].addr, ch_reg(IE_SFX_OFF_LOOP_PTR));
    EXPECT_EQ_INT(writes[3].value, ptr);
    EXPECT_EQ_INT(writes[4].addr, ch_reg(IE_SFX_OFF_LOOP_LEN));
    EXPECT_EQ_INT(writes[4].value, IE_SYNTH_WAVE_SEGMENT_BYTES);
    EXPECT_EQ_INT(writes[5].addr, ch_reg(IE_SFX_OFF_FREQ));
    EXPECT_EQ_INT(writes[5].value, freq);
    EXPECT_EQ_INT(writes[6].addr, ch_reg(IE_SFX_OFF_VOL));
    EXPECT_EQ_INT(writes[6].value, vol_pan);
    EXPECT_EQ_INT(writes[7].addr, ch_reg(IE_SFX_OFF_FORMAT));
    EXPECT_EQ_INT(writes[7].value, IE_SFX_FORMAT_SIGNED16);
    EXPECT_EQ_INT(writes[8].addr, ch_reg(IE_SFX_OFF_CTRL));
    EXPECT_EQ_INT(writes[8].value, IE_SFX_CTRL_TRIGGER | IE_SFX_CTRL_LOOP_EN);
}

static void test_unchanged_voice_update_skips_freq_and_vol_writes(void) {
    ie_audio_api.init();
    start_one_voice();

    set_note(0x8000, 4096, 4096, 0);
    reset_capture();
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);

    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_FREQ)), 0);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_VOL)), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_AUDIO_VOICE_WRITES), 0);
}

static void test_changed_voice_update_writes_only_changed_register(void) {
    ie_audio_api.init();
    start_one_voice();

    set_note(0x9000, 4096, 4096, 0);
    reset_capture();
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_FREQ)), 1);
    EXPECT_EQ_INT(last_value_for(ch_reg(IE_SFX_OFF_FREQ)), expected_freq(0x9000));
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_VOL)), 0);

    set_note(0x9000, 8192, 8192, 0);
    reset_capture();
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_FREQ)), 0);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_VOL)), 1);
    EXPECT_EQ_INT(last_value_for(ch_reg(IE_SFX_OFF_VOL)), expected_vol_pan(8192, 8192));
}

static void test_stop_clears_voice_shadows_before_restart(void) {
    ie_audio_api.init();
    start_one_voice();

    memset(note_subs, 0, sizeof(note_subs));
    reset_capture();
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);
    EXPECT_EQ_INT(last_value_for(ch_reg(IE_SFX_OFF_CTRL)), IE_SFX_CTRL_STOP);

    set_note(0x8000, 4096, 4096, 1);
    reset_capture();
    ie_audio_voice_frame_begin();
    ie_audio_voice_update(0);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_FREQ)), 1);
    EXPECT_EQ_INT(count_writes_for(ch_reg(IE_SFX_OFF_VOL)), 1);
}

int main(void) {
    gAudioBufferParameters.frequency = 26800;
    test_voice_start_writes_full_register_sequence();
    test_unchanged_voice_update_skips_freq_and_vol_writes();
    test_changed_voice_update_writes_only_changed_register();
    test_stop_clears_voice_shadows_before_restart();
    return test_finish("test_audio_voices");
}
