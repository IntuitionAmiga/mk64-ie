/*
 * Golden tests for the portable audio mixer (RSP audio reimplementation)
 * that replaced the SH4 asm helpers. The ADPCM predictor semantics are the
 * unrolled RSP form:
 *
 *   acc[j] = ins[j] + prev2 * tbl[0][j] + prev1 * tbl[1][j]
 *          + sum(k < j) ins[k] * tbl[1][j - 1 - k]
 *
 * where ins[] are the shift-scaled nybbles of the frame and tbl is the
 * 2x8 float predictor table loaded by aLoadADPCM (book / 2048).
 */
#include <stdint.h>

#include "test_support.h"

#include "audio/mixer.h"

void n64_memcpy(void* dst, const void* src, size_t size);

/* Matches the scale constant used by aLoadADPCMImpl. */
#define TEST_RECIP2048 0.00048828f

/* One ADPCM frame: header byte + 8 data bytes -> 16 samples. */
static const uint8_t kFrame[9] = { 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };

/* Signed nybbles of kFrame's data bytes, high nybble first. */
static const int kNybbles[16] = { 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1, 0 };

static int16_t adpcm_state[16];

static int16_t swap16(int16_t v) {
    return (int16_t) __builtin_bswap16((uint16_t) v);
}

static void load_book(const int16_t entries[16]) {
    int16_t book[16];
    for (int i = 0; i < 16; i++) {
        book[i] = swap16(entries[i]); /* book data is stored big-endian */
    }
    aLoadADPCMImpl(32, book);
}

static void test_adpcm_zero_table(void) {
    /* Zero predictor table: output is just the shifted nybbles. */
    int16_t book[16] = { 0 };
    int16_t out[16];

    load_book(book);
    aSetBufferImpl(0, 0x000, 0x400, 32);
    aLoadBufferImpl(kFrame, 0x000, 16);
    aADPCMdecImpl(A_INIT, adpcm_state);

    /* Skip the 16-short state header at the output address. */
    aSaveBufferImpl(0x400 + 32, out, 32);

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ_INT(out[i], kNybbles[i]);
    }
}

static void test_adpcm_first_order(void) {
    /*
     * Book entry 8 maps to tbl[1][0] (the "previous sample" coefficient).
     * With c = 2048 * (1/2048) each output becomes
     * ins[j] + c * previous_sample, truncated toward zero by the final
     * float-to-s16 conversion.
     */
    int16_t book[16] = { 0 };
    int16_t out[16];
    float c = 2048 * TEST_RECIP2048;

    book[8] = 2048;
    load_book(book);
    aSetBufferImpl(0, 0x000, 0x400, 32);
    aLoadBufferImpl(kFrame, 0x000, 16);
    aADPCMdecImpl(A_INIT, adpcm_state);
    aSaveBufferImpl(0x400 + 32, out, 32);

    float prev = 0.0f;
    for (int i = 0; i < 16; i++) {
        /* Group boundaries re-read prev from the clamped output. */
        if (i == 8) {
            prev = out[7];
        }
        int16_t expected = (int16_t) (kNybbles[i] + c * prev);
        EXPECT_EQ_INT(out[i], expected);
        prev = (i % 8 == 7) ? out[i] : kNybbles[i];
    }
}

static void test_adpcm_shift(void) {
    /* Header 0x40: shift multiplier 16, zero table. */
    uint8_t frame[9];
    int16_t book[16] = { 0 };
    int16_t out[16];

    memcpy(frame, kFrame, sizeof(frame));
    frame[0] = 0x40;

    load_book(book);
    aSetBufferImpl(0, 0x000, 0x400, 32);
    aLoadBufferImpl(frame, 0x000, 16);
    aADPCMdecImpl(A_INIT, adpcm_state);
    aSaveBufferImpl(0x400 + 32, out, 32);

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ_INT(out[i], kNybbles[i] * 16);
    }
}

static void test_resample_constant(void) {
    /* Pitch 0x8000 advances exactly one input sample per output sample. */
    int16_t in[64];
    int16_t out[32];
    int16_t state[16] = { 0 };

    for (int i = 0; i < 64; i++) {
        in[i] = 1000;
    }

    aSetBufferImpl(0, 0x040, 0x400, 32);
    aLoadBufferImpl(in, 0x040, sizeof(in));
    aResampleImpl(A_INIT, 0x8000, state);
    aSaveBufferImpl(0x400, out, 32);

    static const int16_t expected_head[4] = { 0, -1, 102, 904 };
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ_INT(out[i], expected_head[i]);
    }
    for (int i = 4; i < 16; i++) {
        EXPECT_EQ_INT(out[i], 1000);
    }
}

static void test_env_mixer(void) {
    int16_t in[16];
    int16_t dryl[16] = { 0 };
    int16_t dryr[16] = { 0 };
    int16_t out[16];

    for (int i = 0; i < 16; i++) {
        in[i] = 100;
    }

    aSetBufferImpl(0, 0, 0, 0);
    aLoadBufferImpl(in, 0x000, sizeof(in));
    aLoadBufferImpl(dryl, 0x100, sizeof(dryl));
    aLoadBufferImpl(dryr, 0x200, sizeof(dryr));
    aEnvSetup1Impl(0, 0, 0, 0); /* rates: 0 */
    aEnvSetup2Impl(0x8000, 0x4000);
    aEnvMixerImpl(0x000, 16, 0, 0, 0, 0x100, 0x200, 0, 0);

    aSaveBufferImpl(0x100, out, sizeof(out));
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ_INT(out[i], 100 * 0x8000 >> 16);
    }
    aSaveBufferImpl(0x200, out, sizeof(out));
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ_INT(out[i], 100 * 0x4000 >> 16);
    }
}

static void test_interleave(void) {
    int16_t left[16], right[16], out[32];

    for (int i = 0; i < 16; i++) {
        left[i] = i + 1;
        right[i] = 101 + i;
    }

    aSetBufferImpl(0, 0, 0x400, 32);
    aLoadBufferImpl(left, 0x100, sizeof(left));
    aLoadBufferImpl(right, 0x200, sizeof(right));
    aInterleaveImpl(0x100, 0x200);
    aSaveBufferImpl(0x400, out, sizeof(out));

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ_INT(out[2 * i], left[i]);
        EXPECT_EQ_INT(out[2 * i + 1], right[i]);
    }
}

static void test_dmem_move(void) {
    uint8_t src[32];
    uint8_t out[32];

    for (int i = 0; i < 32; i++) {
        src[i] = (uint8_t) (i * 3 + 1);
    }

    aLoadBufferImpl(src, 0x000, sizeof(src));
    aDMEMMoveImpl(0x000, 0x300, 32);
    aSaveBufferImpl(0x300, (int16_t*) out, sizeof(out));

    EXPECT_TRUE(memcmp(src, out, 32) == 0);
}

int main(void) {
    test_adpcm_zero_table();
    test_adpcm_first_order();
    test_adpcm_shift();
    test_resample_constant();
    test_env_mixer();
    test_interleave();
    test_dmem_move();
    return test_finish("test_mixer");
}
