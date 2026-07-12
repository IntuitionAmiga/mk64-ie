/*
 * IE backend for the neutral audio sink (struct AudioAPI from
 * src/audio/audio_api.h), on the engine's SFX sample-trigger block.
 *
 * The full game uses the host-side voice path: the N64 sequence player
 * still owns note timing, envelopes and pitch, while each live note is
 * mapped 1:1 onto an engine SFX voice. The old mono ring sink remains
 * only for the small standalone ie/audio_main.c smoke image, which does
 * not link the game sequence-player state.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef IE_AUDIO_VOICES
#include "audio/data.h"
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/synthesis.h"
#include "ie_audio_sample_table.h"

/* The recovered N64 headers define bool/true/false as macros. Keep those
 * definitions local to the audio-engine declarations and let platform APIs use
 * the hosted C bool type. */
#undef bool
#undef true
#undef false
#undef ssize_t

extern u8 *_audio_tablesPcm16SegmentRomStart;
#endif

#include <stdbool.h>

#include "audio/audio_api.h"
#include "platform/platform.h"

#include "ie_mmio.h"
#include "ie_perf_counters.h"

uint64_t ie_time_usec(void);

#define IE_AUDIO_RATE 13400u
#define IE_RING_SAMPLES 16384u
#define IE_AUDIO_VOICE_WINDOW_USEC 16667u
#define IE_AUDIO_VOICE_MAX_CATCHUP_TICKS 120u

#define IE_AUDIO_STATUS_POLLS 6u
#define IE_AUDIO_STATUS_PUMPS 7u
#define IE_AUDIO_STATUS_LIVE 8u
#define IE_AUDIO_STATUS_MAX_LIVE 24u
#define IE_AUDIO_STATUS_STARTS 25u
#define IE_AUDIO_STATUS_STOPS 26u
#define IE_AUDIO_STATUS_LOOP_STARTS 27u
#define IE_AUDIO_STATUS_ONESHOT_STARTS 28u
#define IE_AUDIO_STATUS_SYNTH_STARTS 29u
#define IE_AUDIO_STATUS_NATURAL_FINISHES 30u
#define IE_AUDIO_STATUS_DEFERRED_STOPS 31u
#define IE_AUDIO_STATUS_ERROR_STOPS 32u
#define IE_AUDIO_STATUS_RESETS 33u
#define IE_AUDIO_STATUS_RETRIGGERS 34u
#define IE_AUDIO_STATUS_ONESHOT_STOPS 35u

#ifndef IE_AUDIO_VOICES
static uint8_t ring[IE_RING_SAMPLES * 2]; /* mono s16, stored little-endian */
static uint32_t write_pos;                /* sample index into the ring */
static uint64_t start_usec;
#endif
static uint32_t total_written;            /* mono samples written since init */
static int audio_running;

/*
 * Audio poll: the platform contract (src/platform/platform.h) wants
 * the audio pump to run once per 60Hz vblank even while the game loop
 * is busy. The game is single-threaded, so instead of one pump per
 * rendered frame, hot mid-frame paths (renderer flushes, asset loads,
 * the frame hook) call ie_audio_poll(), which tops the ring back up
 * whenever it has fallen below the desired headroom. The pump
 * callback is the game's game_audio_pump, registered at boot.
 */
static void (*audio_pump_fn)(void);

void ie_audio_set_pump(void (*pump)(void)) {
    audio_pump_fn = pump;
}

static int ie_audio_buffered(void);
static int ie_audio_get_desired_buffered(void);
#ifdef IE_AUDIO_VOICES
static uint32_t ie_audio_live_voice_count(void);
static void ie_audio_voice_reset(void);
void ie_audio_voice_reset_all(void);
#endif

#ifdef IE_MMIO_HOST_CAPTURE
static uint32_t host_audio_status[64];
static volatile uint32_t* const audio_status = host_audio_status;
#else
static volatile uint32_t* const audio_status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
#endif

void ie_audio_poll(void) {
    audio_status[IE_AUDIO_STATUS_POLLS]++;
#ifdef IE_AUDIO_VOICES
    {
        static uint64_t next_window;
        uint64_t entry = ie_time_usec();
        uint32_t ticks = 0;

        if (!audio_running || audio_pump_fn == NULL) {
            return;
        }
        if (next_window == 0) {
            next_window = entry;
        }
        if (entry < next_window) {
            return;
        }
        while (entry >= next_window) {
            audio_status[IE_AUDIO_STATUS_PUMPS]++;
            audio_pump_fn();
            next_window += IE_AUDIO_VOICE_WINDOW_USEC;
            ticks++;
            if (ticks >= IE_AUDIO_VOICE_MAX_CATCHUP_TICKS) {
                next_window = entry + IE_AUDIO_VOICE_WINDOW_USEC;
                break;
            }
        }
        audio_status[IE_AUDIO_STATUS_LIVE] = ie_audio_live_voice_count();
    }
#else
    /*
     * Wall-time budget: soft-float synthesis can run slower than
     * real-time when many notes play (title music), and an unbounded
     * catch-up loop would starve the game loop entirely - the frame
     * counter freezes while the mixer grinds. Spend at most ~8ms per
     * poll; if the ring is still short afterwards, resync the
     * consumption baseline (an audible dropout) instead of chasing a
     * deficit that synthesis can never close.
     */
    static uint64_t next_window;
    uint64_t entry = ie_time_usec();

    if (!audio_running || audio_pump_fn == NULL) {
        return;
    }
    /* One budgeted window per 60Hz period of wall time, no matter how
     * many call sites poll (the renderer polls at every triangle
     * flush): audio gets at most ~half of real time, the game loop
     * keeps the rest. */
    if (entry < next_window) {
        return;
    }
    next_window = entry + 16667u;
    while (ie_audio_buffered() < ie_audio_get_desired_buffered()) {
        audio_status[IE_AUDIO_STATUS_PUMPS]++;
        audio_pump_fn();
        if (ie_time_usec() - entry > 8000u) {
            if (ie_audio_buffered() < ie_audio_get_desired_buffered()) {
                uint32_t keep = (uint32_t) ie_audio_get_desired_buffered() / 2;
                uint32_t held = total_written < keep ? total_written : keep;

                start_usec = ie_time_usec()
                           - ((uint64_t)(total_written - held) * 1000000u) / IE_AUDIO_RATE;
            }
            break;
        }
    }
#endif
}

/* Exposed for the audio smoke program. */
uint32_t ie_audio_total_written(void) {
    return total_written;
}

uint32_t ie_audio_status(void) {
#ifdef IE_AUDIO_VOICES
    return ie_audio_live_voice_count();
#else
    return ie_mmio_read32(IE_SFX_CTRL);
#endif
}

static bool ie_audio_init(void) {
#ifdef IE_AUDIO_VOICES
    ie_mmio_write32(IE_AUDIO_CTRL, IE_AUDIO_CTRL_ENABLE);
    /* Stage 4 reverb decision: keep the offload path dry by default.
     * The engine reverb is global across all SFX voices, while MK64's
     * original sends are per-note; dry is parity with the current port. */
    ie_mmio_write32(IE_AUDIO_REVERB_MIX, 0);
    ie_mmio_write32(IE_AUDIO_REVERB_DECAY, 0);
    ie_audio_voice_reset();
    audio_running = 1;
    return true;
#else
    uint32_t status;

    ie_mmio_write32(IE_AUDIO_CTRL, IE_AUDIO_CTRL_ENABLE);

    ie_mmio_write32(IE_SFX_PTR, (uint32_t) (uintptr_t) ring);
    ie_mmio_write32(IE_SFX_LEN, sizeof(ring));
    ie_mmio_write32(IE_SFX_LOOP_PTR, (uint32_t) (uintptr_t) ring);
    ie_mmio_write32(IE_SFX_LOOP_LEN, sizeof(ring));
    ie_mmio_write32(IE_SFX_FREQ, IE_AUDIO_RATE);
    ie_mmio_write32(IE_SFX_VOL, 255);
    ie_mmio_write32(IE_SFX_FORMAT, IE_SFX_FORMAT_SIGNED16);
    ie_mmio_write32(IE_SFX_CTRL, IE_SFX_CTRL_TRIGGER | IE_SFX_CTRL_LOOP_EN);

    status = ie_mmio_read32(IE_SFX_CTRL);
    if ((status & IE_SFX_STATUS_ERROR) != 0 || (status & IE_SFX_STATUS_PLAYING) == 0) {
        /* The game ignores audio_api->init()'s return value and would
         * run on with silent audio - a quiet degradation. The SFX
         * channel is a core engine feature; failing to start it means
         * the platform is misconfigured, so fail loudly instead. */
        platform_fatal("audio sink failed to start (sfx status 0x%x)", status);
    }

    start_usec = ie_time_usec();
    audio_running = 1;
    return true;
#endif
}

#ifndef IE_AUDIO_VOICES
static uint32_t samples_consumed(void) {
    uint64_t elapsed = ie_time_usec() - start_usec;

    return (uint32_t)((elapsed * IE_AUDIO_RATE) / 1000000u);
}
#endif

static int ie_audio_buffered(void) {
#ifdef IE_AUDIO_VOICES
    return 0;
#else
    uint32_t consumed;

    if (!audio_running) {
        return 0;
    }
    consumed = samples_consumed();
    if (total_written <= consumed) {
        return 0;
    }
    return (int)(total_written - consumed);
#endif
}

static int ie_audio_get_desired_buffered(void) {
#ifdef IE_AUDIO_VOICES
    return 0;
#else
    /* Half the ring (~0.3s): the game pumps in 448-sample chunks and
     * catches up after slow frames (see audio_frame_hook), so the
     * target needs to cover several frames of consumption. */
    return (int) (IE_RING_SAMPLES / 2);
#endif
}

static void ie_audio_play(uint8_t* buf, size_t len) {
#ifdef IE_AUDIO_VOICES
    (void) buf;
    (void) len;
#else
    /* Interleaved stereo s16 in the guest's native (big-endian)
     * layout, as produced by the mixer. */
    size_t frames = len / 4;
    size_t i;

    for (i = 0; i < frames; i++) {
        int16_t left = (int16_t)(((uint16_t) buf[i * 4] << 8) | buf[i * 4 + 1]);
        int16_t right = (int16_t)(((uint16_t) buf[i * 4 + 2] << 8) | buf[i * 4 + 3]);
        int16_t mono = (int16_t)(((int32_t) left + right) / 2);

        /* Device reads little-endian. */
        ring[write_pos * 2] = (uint8_t) mono;
        ring[write_pos * 2 + 1] = (uint8_t)((uint16_t) mono >> 8);
        write_pos = (write_pos + 1) % IE_RING_SAMPLES;
    }
    total_written += (uint32_t) frames;
#endif
}

#ifdef IE_AUDIO_VOICES
#define IE_VOICE_KEY_EMPTY 0xFFFFFFFFu
#define IE_VOICE_SYNTH_KEY(wf, harmonic) (0x80000000u | ((uint32_t)(wf) << 8) | (uint32_t)(harmonic))
#define IE_VOICE_VOL_HEADROOM 16u

typedef struct {
    uint32_t key;
    uint32_t last_freq;
    uint32_t last_vol_pan;
    uint8_t active;
    uint8_t looping;
    uint8_t started_this_pump;
    uint8_t deferred_stop;
    uint8_t have_freq;
    uint8_t have_vol_pan;
} IEVoice;

typedef struct {
    uint32_t key;
    uint32_t ptr;
    uint32_t len;
    uint32_t loop_ptr;
    uint32_t loop_len;
    uint8_t looping;
} IEVoiceResolved;

static IEVoice voices[IE_SFX_CHANNELS];
static uint32_t live_voice_count;

static void ie_audio_voice_diag_reset(void) {
    uint32_t i;

    audio_status[IE_AUDIO_STATUS_LIVE] = 0;
    for (i = IE_AUDIO_STATUS_MAX_LIVE; i <= IE_AUDIO_STATUS_ONESHOT_STOPS; i++) {
        audio_status[i] = 0;
    }
    live_voice_count = 0;
}

static uint32_t sfx_reg(uint32_t ch, uint32_t off) {
    return IE_SFX_CH_BASE(ch) + off;
}

static void sfx_write(uint32_t ch, uint32_t off, uint32_t value) {
    IE_PERF_INC(IE_PERF_AUDIO_VOICE_WRITES);
    IE_PERF_INC(IE_PERF_MMIO_WRITES);
    ie_mmio_write32(sfx_reg(ch, off), value);
}

static uint32_t sfx_read(uint32_t ch, uint32_t off) {
    return ie_mmio_read32(sfx_reg(ch, off));
}

static void voice_forget(uint32_t ch) {
    voices[ch].key = IE_VOICE_KEY_EMPTY;
    voices[ch].active = 0;
    voices[ch].looping = 0;
    voices[ch].started_this_pump = 0;
    voices[ch].deferred_stop = 0;
    voices[ch].last_freq = 0;
    voices[ch].last_vol_pan = 0;
    voices[ch].have_freq = 0;
    voices[ch].have_vol_pan = 0;
}

static void voice_stop(uint32_t ch) {
    if (voices[ch].active) {
        audio_status[IE_AUDIO_STATUS_STOPS]++;
        if (!voices[ch].looping) {
            audio_status[IE_AUDIO_STATUS_ONESHOT_STOPS]++;
        }
    }
    sfx_write(ch, IE_SFX_OFF_CTRL, IE_SFX_CTRL_STOP);
    voice_forget(ch);
}

void ie_audio_voice_reset_all(void) {
    uint32_t ch;

    for (ch = 0; ch < IE_SFX_CHANNELS; ch++) {
        voice_stop(ch);
    }
    live_voice_count = 0;
    audio_status[IE_AUDIO_STATUS_LIVE] = 0;
    audio_status[IE_AUDIO_STATUS_RESETS]++;
}

void ie_audio_voice_note_reset_drained(void) {
    audio_status[IE_AUDIO_STATUS_RESETS]++;
}

static void ie_audio_voice_reset(void) {
    ie_audio_voice_diag_reset();
    ie_audio_voice_reset_all();
    audio_status[IE_AUDIO_STATUS_RESETS] = 0;
}

static const IEAudioSampleDesc* sample_desc_lookup(uint32_t key) {
    int lo = 0;
    int hi = IE_AUDIO_SAMPLE_COUNT - 1;

    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t got = gIEAudioSampleTable[mid].src_offset;
        if (got == key) {
            return &gIEAudioSampleTable[mid];
        }
        if (got < key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static int resolve_synth_wave(struct NoteSubEu* sub, IEVoiceResolved* out) {
    s16* addr = sub->sound.samples;
    uint32_t wf;

    for (wf = 0; wf < IE_SYNTH_WAVEFORM_COUNT; wf++) {
        s16* start = gWaveSamples[wf];
        s16* end = start + IE_SYNTH_WAVE_SAMPLES;
        if (addr >= start && addr < end) {
            uint32_t sample_index = (uint32_t)(addr - start);
            uint32_t harmonic = sample_index / IE_SYNTH_WAVE_SEGMENT_SAMPLES;
            if (harmonic >= IE_SYNTH_WAVE_HARMONICS) {
                harmonic = IE_SYNTH_WAVE_HARMONICS - 1;
            }
            out->key = IE_VOICE_SYNTH_KEY(wf, harmonic);
            out->ptr = (uint32_t)(uintptr_t)&gIESynthWaveLE[wf][harmonic * IE_SYNTH_WAVE_SEGMENT_BYTES];
            out->len = IE_SYNTH_WAVE_SEGMENT_BYTES;
            out->loop_ptr = out->ptr;
            out->loop_len = out->len;
            out->looping = 1;
            return 1;
        }
    }
    return 0;
}

static int resolve_bank_sample(struct NoteSubEu* sub, IEVoiceResolved* out) {
    struct AudioBankSample* sample;
    uintptr_t table_base;
    uintptr_t sample_addr;
    uint32_t key;
    const IEAudioSampleDesc* desc;

    if (gAlTbl == NULL || gAlTbl->seqArray[0].offset == NULL || _audio_tablesPcm16SegmentRomStart == NULL ||
        sub->sound.audioBankSound == NULL) {
        return 0;
    }
    sample = sub->sound.audioBankSound->sample;
    if (sample == NULL || sample->sampleAddr == NULL) {
        return 0;
    }

    table_base = (uintptr_t)gAlTbl->seqArray[0].offset;
    sample_addr = (uintptr_t)sample->sampleAddr;
    if (sample_addr < table_base) {
        return 0;
    }
    key = (uint32_t)(sample_addr - table_base);
    desc = sample_desc_lookup(key);
    if (desc == NULL) {
        return 0;
    }

    out->key = key;
    out->ptr = (uint32_t)(uintptr_t)(_audio_tablesPcm16SegmentRomStart + desc->pcm_offset);
    out->len = desc->byte_len;
    out->loop_ptr = out->ptr + desc->loop_start_bytes;
    out->loop_len = desc->loop_len_bytes;
    out->looping = (desc->loop_count != 0 && desc->loop_len_bytes != 0);
    return 1;
}

static int resolve_voice(struct NoteSubEu* sub, IEVoiceResolved* out) {
    if (sub->isSyntheticWave) {
        return resolve_synth_wave(sub, out);
    }
    return resolve_bank_sample(sub, out);
}

static uint32_t voice_freq(struct NoteSubEu* sub) {
    uint64_t freq = ((uint64_t)sub->resamplingRateFixedPoint * (uint64_t)gAudioBufferParameters.frequency) >> 15;

    if (sub->hasTwoAdpcmParts) {
        freq <<= 1;
    }
    if (freq == 0) {
        freq = 1;
    }
    if (freq > 0xFFFFFFFFu) {
        freq = 0xFFFFFFFFu;
    }
    return (uint32_t)freq;
}

static uint32_t voice_vol(struct NoteSubEu* sub) {
    uint32_t sum = (uint32_t)sub->targetVolLeft + (uint32_t)sub->targetVolRight;
    uint32_t vol = (sum * 255u) / (8192u * IE_VOICE_VOL_HEADROOM);

    if (vol == 0 && sum != 0) {
        vol = 1;
    }
    if (vol > 255u) {
        vol = 255u;
    }
    return vol;
}

static uint32_t voice_pan(struct NoteSubEu* sub) {
    uint32_t left = sub->targetVolLeft;
    uint32_t right = sub->targetVolRight;
    uint32_t sum = left + right;

    if (sum == 0) {
        return 128u;
    }
    return (right * 255u) / sum;
}

static void voice_start(uint32_t ch, const IEVoiceResolved* res, uint32_t freq, uint32_t vol, uint32_t pan) {
    uint32_t ctrl = IE_SFX_CTRL_TRIGGER;

    sfx_write(ch, IE_SFX_OFF_CTRL, IE_SFX_CTRL_STOP);
    sfx_write(ch, IE_SFX_OFF_PTR, res->ptr);
    sfx_write(ch, IE_SFX_OFF_LEN, res->len);
    sfx_write(ch, IE_SFX_OFF_LOOP_PTR, res->loop_ptr);
    sfx_write(ch, IE_SFX_OFF_LOOP_LEN, res->loop_len);
    sfx_write(ch, IE_SFX_OFF_FREQ, freq);
    sfx_write(ch, IE_SFX_OFF_VOL, vol | (pan << 16));
    sfx_write(ch, IE_SFX_OFF_FORMAT, IE_SFX_FORMAT_SIGNED16);
    if (res->looping) {
        ctrl |= IE_SFX_CTRL_LOOP_EN;
        audio_status[IE_AUDIO_STATUS_LOOP_STARTS]++;
    } else {
        audio_status[IE_AUDIO_STATUS_ONESHOT_STARTS]++;
    }
    if ((res->key & 0x80000000u) != 0) {
        audio_status[IE_AUDIO_STATUS_SYNTH_STARTS]++;
    }
    sfx_write(ch, IE_SFX_OFF_CTRL, ctrl);
    voices[ch].last_freq = freq;
    voices[ch].last_vol_pan = vol | (pan << 16);
    voices[ch].have_freq = 1;
    voices[ch].have_vol_pan = 1;
    audio_status[IE_AUDIO_STATUS_STARTS]++;
}

static void voice_update(uint32_t ch, uint32_t freq, uint32_t vol, uint32_t pan) {
    uint32_t vol_pan = vol | (pan << 16);

    if (!voices[ch].have_freq || voices[ch].last_freq != freq) {
        sfx_write(ch, IE_SFX_OFF_FREQ, freq);
        voices[ch].last_freq = freq;
        voices[ch].have_freq = 1;
    }
    if (!voices[ch].have_vol_pan || voices[ch].last_vol_pan != vol_pan) {
        sfx_write(ch, IE_SFX_OFF_VOL, vol_pan);
        voices[ch].last_vol_pan = vol_pan;
        voices[ch].have_vol_pan = 1;
    }
}

static void mark_note_finished(uint32_t updateIndex, uint32_t noteIndex) {
    struct NoteSubEu* sub = &gNoteSubsEu[gMaxSimultaneousNotes * updateIndex + noteIndex];

    sub->finished = 1;
    sub->enabled = 0;
    gNotes[noteIndex].noteSubEu.finished = 1;
    gNotes[noteIndex].noteSubEu.enabled = 0;
    func_800B6FB4((s32)updateIndex, (s32)noteIndex);
    audio_status[IE_AUDIO_STATUS_NATURAL_FINISHES]++;
}

void ie_audio_voice_frame_begin(void) {
    uint32_t ch;

    for (ch = 0; ch < IE_SFX_CHANNELS; ch++) {
        voices[ch].started_this_pump = 0;
        if (voices[ch].deferred_stop) {
            voice_stop(ch);
        }
    }
}

void ie_audio_voice_update(s32 updateIndex) {
    uint32_t i;
    uint32_t active = 0;
    uint32_t count = (uint32_t)gMaxSimultaneousNotes;

    if (count > IE_SFX_CHANNELS) {
        count = IE_SFX_CHANNELS;
    }

    for (i = 0; i < count; i++) {
        struct NoteSubEu* sub = &gNoteSubsEu[gMaxSimultaneousNotes * updateIndex + i];
        IEVoice* voice = &voices[i];
        IEVoiceResolved res;
        uint32_t freq;
        uint32_t vol;
        uint32_t pan;
        int retrigger;

        if (voice->active && !voice->looping) {
            uint32_t status = sfx_read(i, IE_SFX_OFF_CTRL);
            if ((status & IE_SFX_STATUS_ERROR) != 0) {
                audio_status[IE_AUDIO_STATUS_ERROR_STOPS]++;
                voice_stop(i);
            } else if ((status & IE_SFX_STATUS_PLAYING) == 0) {
                mark_note_finished((uint32_t)updateIndex, i);
                voice_forget(i);
                continue;
            }
        }

        if (!sub->enabled) {
            if (voice->active) {
                if (voice->started_this_pump) {
                    voice->deferred_stop = 1;
                    audio_status[IE_AUDIO_STATUS_DEFERRED_STOPS]++;
                    active++;
                } else {
                    voice_stop(i);
                }
            }
            continue;
        }

        if (!resolve_voice(sub, &res)) {
            if (voice->active) {
                voice_stop(i);
            }
            continue;
        }

        freq = voice_freq(sub);
        vol = voice_vol(sub);
        pan = voice_pan(sub);
        retrigger = !voice->active || voice->key != res.key || sub->needsInit;

        if (retrigger) {
            if (voice->active) {
                audio_status[IE_AUDIO_STATUS_RETRIGGERS]++;
                voice_stop(i);
            }
            voice_start(i, &res, freq, vol, pan);
            voice->key = res.key;
            voice->active = 1;
            voice->looping = res.looping;
            voice->started_this_pump = 1;
            voice->deferred_stop = 0;
        } else {
            voice_update(i, freq, vol, pan);
        }
        active++;
    }

    for (; i < IE_SFX_CHANNELS; i++) {
        if (voices[i].active) {
            voice_stop(i);
        }
    }
    live_voice_count = active;
    audio_status[IE_AUDIO_STATUS_LIVE] = active;
    if (active > audio_status[IE_AUDIO_STATUS_MAX_LIVE]) {
        audio_status[IE_AUDIO_STATUS_MAX_LIVE] = active;
    }
}

static uint32_t ie_audio_live_voice_count(void) {
    return live_voice_count;
}
#endif

struct AudioAPI ie_audio_api = {
    ie_audio_init,
    ie_audio_buffered,
    ie_audio_get_desired_buffered,
    ie_audio_play,
};
