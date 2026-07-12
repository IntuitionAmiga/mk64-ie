/*
 * x86 T&L coprocessor smoke program (plan phases P0-P1).
 *
 * Proves, in order: the service COSTARTs from the file root; the mailbox
 * round-trips a single transform; the batch kernel matches a locally computed
 * reference for the unlit and lit paths; the worker reads/writes guest RAM
 * above 0x10000000 directly on the shared bus (the memory-model gate the
 * game-image integration depends on); and the async enqueue/drain path
 * produces the same result as the synchronous one.
 *
 * All fixture values are chosen to be exactly representable in float32 so the
 * x86 kernel and the local reference must agree bit-for-bit, except the lit
 * colour channels which are compared within +/-1.
 *
 * Status block payload:
 *   +16  u32 check bitmask (want 0x3F)
 *   +20  u32 index of first failing check (1-6, 0 = none)
 *   +24  u32 debug value of the failing comparison
 */

#include <stdint.h>

#include "ie_mmio.h"
#include "coproc/ie_coproc.h"
#include "coproc/tnl_proto.h"

#define CHECK_INIT       0x01u
#define CHECK_XFORM      0x02u
#define CHECK_BATCH      0x04u
#define CHECK_HIGH_RAM   0x08u
#define CHECK_LIT        0x10u
#define CHECK_ASYNC      0x20u

#define HIGH_RAM_VTX 0x10000000u
#define HIGH_RAM_OUT 0x10001000u

/* Local mirrors of the wire structs (offsets guarded in tnl_proto.h against
 * the real game structs; this standalone image redeclares them). */
struct smoke_vtx {
    int16_t  ob[3];
    uint16_t flag;
    int16_t  tc[2];
    uint8_t  cn[4];
};

struct smoke_lv {
    float   x, y, z, w;
    float   _x, _y, _z, _w;
    float   u, v;
    uint8_t r, g, b, a;
    uint8_t clip_rej;
    uint8_t wlt0;
};

_Static_assert(sizeof(struct smoke_vtx) == VTX_SIZE, "smoke_vtx size");
_Static_assert(sizeof(struct smoke_lv) == LV_SIZE, "smoke_lv size");

/* Row-vector transform, same operation tree as gfx_transform_vertex. */
static void ref_xform(float out[4], const float in[3], const float m[16]) {
    out[0] = in[0] * m[0] + in[1] * m[4] + in[2] * m[8] + m[12];
    out[1] = in[0] * m[1] + in[1] * m[5] + in[2] * m[9] + m[13];
    out[2] = in[0] * m[2] + in[1] * m[6] + in[2] * m[10] + m[14];
    out[3] = in[0] * m[3] + in[1] * m[7] + in[2] * m[11] + m[15];
}

static uint8_t ref_clip_rej(float x, float y, float z, float w) {
    uint8_t cr = 0;
    cr = (uint8_t) !(z > w);
    cr = (uint8_t) ((cr << 1) | !(z < -w));
    cr = (uint8_t) ((cr << 1) | !(y > w));
    cr = (uint8_t) ((cr << 1) | !(y < -w));
    cr = (uint8_t) ((cr << 1) | !(x > w));
    cr = (uint8_t) ((cr << 1) | !(x < -w));
    return cr;
}

/* Exactly representable fixture matrix: scale + translate. */
static const float fix_m[16] = {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 4.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    8.0f, 16.0f, 32.0f, 1.0f,
};

static const struct smoke_vtx fix_vtx[2] = {
    { { 100, -50, 8 }, 0, { 512, -1024 }, { 10, 20, 30, 40 } },
    { { -4, 6, -12 }, 0, { 2048, 64 }, { 200, 100, 50, 25 } },
};

static int lv_matches_ref(const struct smoke_lv *got, const struct smoke_vtx *v,
                          uint32_t *dbg) {
    float in[3];
    float out[4];
    int u, tv;

    in[0] = (float) v->ob[0];
    in[1] = (float) v->ob[1];
    in[2] = (float) v->ob[2];
    ref_xform(out, in, fix_m);

    if (got->_x != out[0] || got->_y != out[1] || got->_z != out[2] ||
        got->_w != out[3]) {
        *dbg = (uint32_t) (int32_t) got->_x;
        return 0;
    }
    if (got->x != in[0] || got->y != in[1] || got->z != in[2] || got->w != 1.0f) {
        *dbg = (uint32_t) (int32_t) got->x;
        return 0;
    }
    /* ss = st = 0x8000 -> tc >> 1 */
    u = (v->tc[0] * 0x8000) >> 16;
    tv = (v->tc[1] * 0x8000) >> 16;
    if (got->u != (float) u || got->v != (float) tv) {
        *dbg = (uint32_t) (int32_t) got->u;
        return 0;
    }
    if (got->r != v->cn[0] || got->g != v->cn[1] || got->b != v->cn[2] ||
        got->a != v->cn[3]) {
        *dbg = ((uint32_t) got->r << 24) | ((uint32_t) got->g << 16) |
               ((uint32_t) got->b << 8) | got->a;
        return 0;
    }
    if (got->clip_rej != ref_clip_rej(out[0], out[1], out[2], out[3]) ||
        got->wlt0 != (out[3] < 0.0f)) {
        *dbg = ((uint32_t) got->clip_rej << 8) | got->wlt0;
        return 0;
    }
    return 1;
}

static void fill_params(ie_tnl_params *p, const void *verts, void *out,
                        unsigned n) {
    p->m = fix_m;
    p->ss = 0x8000u;
    p->st = 0x8000u;
    p->geomode = 0;
    p->num_lights = 0;
    p->amb_col[0] = p->amb_col[1] = p->amb_col[2] = 0;
    p->dir_col[0] = p->dir_col[1] = p->dir_col[2] = 0;
    p->light_coef = 0;
    p->lookat = 0;
    p->n = n;
    p->verts = verts;
    p->out = out;
}

static struct smoke_lv lv_out[2];

void ie_main(void) {
    volatile uint32_t *status = (volatile uint32_t *) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t checks = 0;
    uint32_t fail = 0;
    uint32_t dbg = 0;
    ie_tnl_params p;
    int i;

    /* 1: service COSTART + worker online */
    if (ie_coproc_init()) {
        checks |= CHECK_INIT;
    } else if (!fail) {
        fail = 1;
    }

    /* 2: single transform round-trip, bit-exact */
    if (checks & CHECK_INIT) {
        float out4[4] = { 0, 0, 0, 0 };
        float ref4[4];
        const float in3[3] = { 100.0f, -50.0f, 8.0f };
        ref_xform(ref4, in3, fix_m);
        if (ie_coproc_xform(fix_m, in3[0], in3[1], in3[2], out4) &&
            out4[0] == ref4[0] && out4[1] == ref4[1] && out4[2] == ref4[2] &&
            out4[3] == ref4[3]) {
            checks |= CHECK_XFORM;
        } else if (!fail) {
            fail = 2;
            dbg = (uint32_t) (int32_t) out4[0];
        }
    }

    /* 3: unlit batch in low RAM, bit-exact against the local reference */
    if (checks & CHECK_XFORM) {
        fill_params(&p, fix_vtx, lv_out, 2);
        if (ie_coproc_tnl_batch(&p) && lv_matches_ref(&lv_out[0], &fix_vtx[0], &dbg) &&
            lv_matches_ref(&lv_out[1], &fix_vtx[1], &dbg)) {
            checks |= CHECK_BATCH;
        } else if (!fail) {
            fail = 3;
        }
    }

    /* 4: memory-model gate - source AND destination above 0x10000000 */
    if (checks & CHECK_BATCH) {
        struct smoke_vtx *hv = (struct smoke_vtx *) (uintptr_t) HIGH_RAM_VTX;
        struct smoke_lv *ho = (struct smoke_lv *) (uintptr_t) HIGH_RAM_OUT;
        for (i = 0; i < 2; i++) {
            hv[i] = fix_vtx[i];
            ho[i].u = 12345.0f; /* poison */
        }
        fill_params(&p, hv, ho, 2);
        if (ie_coproc_tnl_batch(&p) && lv_matches_ref(&ho[0], &fix_vtx[0], &dbg) &&
            lv_matches_ref(&ho[1], &fix_vtx[1], &dbg)) {
            checks |= CHECK_HIGH_RAM;
        } else if (!fail) {
            fail = 4;
        }
    }

    /* 5: lit path (nl == 2, texgen off). Normal 127,0,0 with coeff 1,0,0
     * gives intensity exactly 1.0 -> col = amb + dir, clamped at 255. */
    if (checks & CHECK_BATCH) {
        static const float lc[3] = { 1.0f, 0.0f, 0.0f };
        struct smoke_vtx lit = { { 10, 20, 30 }, 0, { 0, 0 }, { 127, 0, 0, 99 } };
        fill_params(&p, &lit, lv_out, 1);
        p.geomode = TNL_G_LIGHTING;
        p.num_lights = 2;
        p.amb_col[0] = 20; p.amb_col[1] = 30; p.amb_col[2] = 40;
        p.dir_col[0] = 100; p.dir_col[1] = 240; p.dir_col[2] = 60;
        p.light_coef = lc;
        if (ie_coproc_tnl_batch(&p) &&
            lv_out[0].r >= 119 && lv_out[0].r <= 121 &&   /* 20 + 100 */
            lv_out[0].g == 255 &&                          /* 30 + 240 clamped */
            lv_out[0].b >= 99 && lv_out[0].b <= 101 &&     /* 40 + 60  */
            lv_out[0].a == 99) {
            checks |= CHECK_LIT;
        } else if (!fail) {
            fail = 5;
            dbg = ((uint32_t) lv_out[0].r << 16) | ((uint32_t) lv_out[0].g << 8) |
                  lv_out[0].b;
        }
    }

    /* 6: async enqueue + drain matches the synchronous result */
    if (checks & CHECK_BATCH) {
        lv_out[0].u = 777.0f; /* poison */
        fill_params(&p, fix_vtx, lv_out, 2);
        if (ie_coproc_tnl_batch_async(&p)) {
            ie_coproc_drain();
            if (lv_matches_ref(&lv_out[0], &fix_vtx[0], &dbg) &&
                lv_matches_ref(&lv_out[1], &fix_vtx[1], &dbg)) {
                checks |= CHECK_ASYNC;
            } else if (!fail) {
                fail = 6;
            }
        } else if (!fail) {
            fail = 6;
        }
    }

    status[4] = checks;
    status[5] = fail;
    status[6] = dbg;
    status[1] = 0;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
    }
}
