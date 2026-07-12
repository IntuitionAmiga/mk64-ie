/*
 * ie/coproc/ie_coproc.c - M68K caller for the x86 T&L coprocessor (see .h).
 */
#include "ie_coproc.h"
#include "tnl_proto.h"

#include "../ie_mmio.h"

#define IE_MMIO_Read32  ie_mmio_read32
#define IE_MMIO_Write32 ie_mmio_write32

/* COPROC MMIO control block ($F2340). */
#define COPROC_BASE          0x000F2340u
#define COPROC_CMD           (COPROC_BASE + 0x00u)
#define COPROC_CPU_TYPE      (COPROC_BASE + 0x04u)
#define COPROC_CMD_STATUS    (COPROC_BASE + 0x08u)
#define COPROC_TICKET        (COPROC_BASE + 0x10u)
#define COPROC_TICKET_STATUS (COPROC_BASE + 0x14u)
#define COPROC_OP            (COPROC_BASE + 0x18u)
#define COPROC_REQ_PTR       (COPROC_BASE + 0x1Cu)
#define COPROC_REQ_LEN       (COPROC_BASE + 0x20u)
#define COPROC_RESP_PTR      (COPROC_BASE + 0x24u)
#define COPROC_RESP_CAP      (COPROC_BASE + 0x28u)
#define COPROC_TIMEOUT       (COPROC_BASE + 0x2Cu)
#define COPROC_NAME_PTR      (COPROC_BASE + 0x30u)
#define COPROC_WORKER_STATE  (COPROC_BASE + 0x34u)

#define CMD_START    1u
#define CMD_ENQUEUE  3u
#define CMD_WAIT     5u
#define COPROC_TYPE  2u            /* EXEC_TYPE_IE64 */
/* op codes come from tnl_proto.h (OP_TNL_XFORM, OP_TNL_BATCH) */
#define TICKET_OK    2u

/* IE64 mailbox ring 10 (0x790000 + 10*0x400 = 0x792800) in shared RAM.
 * Per-batch dispatch bypasses the COPROC MMIO block entirely: the M68K writes
 * the request descriptor and advances head here with NATIVE RAM stores (no JIT
 * bail), the worker polls and processes in parallel, and we poll tail for
 * completion. Only COSTART uses MMIO (once). The ROM load corrupts this region,
 * so it is reset at init. Keep in sync with coproc/coproc_layout.h. */
#define RING_HDR     0x00792800u
#define RING_HEAD    0x00792800u  /* u8, producer (us)   */
#define RING_TAIL    0x00792801u  /* u8, consumer (IE64) */
#define RING_ENTRIES 0x00792808u  /* 32-byte descriptors */
#define RING_SLOTS   16u
#define ENTRY_OP      8u
#define ENTRY_REQPTR  16u
#define ENTRY_RESPPTR 24u
/* ENTRY_TICKET is offset 0 */

static inline uint8_t rrd8(uint32_t a) { return *(volatile uint8_t *) (uintptr_t) a; }
static inline void    rwr8(uint32_t a, uint8_t v) { *(volatile uint8_t *) (uintptr_t) a = v; }
/* Descriptor fields are read little-endian by the worker, so write them LE. */
static inline void rwr32le(uint32_t a, uint32_t v)
{
    volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) a;
    p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24);
}
static uint32_t g_ticket;

static const char svc_name[] = "build/ie/tnl.ie64";

/* Self-contained pack boot: the service binary ships inside
 * mariokart64.ie68 (TOC name "svc/tnl.ie64") and starts DIRECTLY from
 * its in-RAM blob via CMD_START_MEM - no host filesystem involved.
 * Returns 1 when a pack blob was found and the start was issued. */
#include "../ie_pack.h"

#define CMD_START_MEM 6u

static int svc_start_from_pack(void)
{
    const ie_pack_header *hdr = (const ie_pack_header *) (uintptr_t) IE_PACK_HDR_ADDR;
    const ie_pack_entry *e;
    uint32_t i;

    if (hdr->magic0 != IE_PACK_MAGIC0 || hdr->magic1 != IE_PACK_MAGIC1)
    {
        return 0;
    }
    e = (const ie_pack_entry *) (const void *) (hdr + 1);
    for (i = 0; i < hdr->toc_count; i++, e++)
    {
        const char *a = e->name;
        const char *b = "svc/tnl.ie64";
        while (*a != '\0' && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            IE_MMIO_Write32(COPROC_CPU_TYPE, COPROC_TYPE);
            IE_MMIO_Write32(COPROC_REQ_PTR, IE_LOAD_BASE + e->offset);
            IE_MMIO_Write32(COPROC_REQ_LEN, e->size);
            IE_MMIO_Write32(COPROC_CMD, CMD_START_MEM);
            return 1;
        }
    }
    return 0;
}

/* Request/response live in guest RAM; the worker reads them on the shared
 * bus. matrix(16) + vertex(3) = 76 bytes in; 4 floats out. */
static float ie_req[19];
static float ie_resp[4];

static int g_coproc_ok;
static int g_coproc_tried;

int ie_coproc_init(void)
{
    if (g_coproc_tried)
    {
        return g_coproc_ok; /* idempotent: COSTART once (a 2nd start errors) */
    }
    g_coproc_tried = 1;
    /* Reinit the ring header the ROM load clobbered (head=0, tail=0,
     * cap=16) BEFORE starting the worker: the JIT-compiled service polls
     * immediately, and stale head/tail bytes would make it consume
     * garbage descriptors - and write through their garbage pointers -
     * in the window before a post-start reset lands. */
    *(volatile uint8_t *) (uintptr_t) (RING_HDR + 0u) = 0u;
    *(volatile uint8_t *) (uintptr_t) (RING_HDR + 1u) = 0u;
    *(volatile uint8_t *) (uintptr_t) (RING_HDR + 2u) = 16u;

    /* Pack boot starts the embedded service straight from RAM; file
     * boots COSTART from the service file. */
    if (!svc_start_from_pack())
    {
        IE_MMIO_Write32(COPROC_NAME_PTR, (uint32_t) (uintptr_t) svc_name);
        IE_MMIO_Write32(COPROC_CPU_TYPE, COPROC_TYPE);
        IE_MMIO_Write32(COPROC_CMD, CMD_START);
    }
    if (IE_MMIO_Read32(COPROC_CMD_STATUS) != 0u)
    {
        g_coproc_ok = 0;
        return 0;
    }

    g_coproc_ok = (IE_MMIO_Read32(COPROC_WORKER_STATE) & (1u << COPROC_TYPE)) != 0u;
    return g_coproc_ok;
}

#ifdef IE_GFX_SERVICE
/* Gfx-service compile pass: the main CPU already COSTARTed the IE64 worker
 * and reset ring 10, so this image must neither re-init the ring nor start a
 * second worker - just arm the dispatch flag if the worker is live. */
int ie_coproc_attach(void)
{
    g_coproc_tried = 1;
    g_coproc_ok = (IE_MMIO_Read32(COPROC_WORKER_STATE) & (1u << COPROC_TYPE)) != 0u;
    return g_coproc_ok;
}
#endif

int ie_coproc_xform(const float m[16], float x, float y, float z, float out[4])
{
    int i;
    uint32_t ticket;

    if (!g_coproc_ok)
    {
        return 0;
    }
    for (i = 0; i < 16; ++i)
    {
        ie_req[i] = m[i];
    }
    ie_req[16] = x;
    ie_req[17] = y;
    ie_req[18] = z;

    IE_MMIO_Write32(COPROC_CPU_TYPE, COPROC_TYPE);
    IE_MMIO_Write32(COPROC_OP, OP_TNL_XFORM);
    IE_MMIO_Write32(COPROC_REQ_PTR, (uint32_t) (uintptr_t) ie_req);
    IE_MMIO_Write32(COPROC_REQ_LEN, 76u);
    IE_MMIO_Write32(COPROC_RESP_PTR, (uint32_t) (uintptr_t) ie_resp);
    IE_MMIO_Write32(COPROC_RESP_CAP, 16u);
    IE_MMIO_Write32(COPROC_CMD, CMD_ENQUEUE);

    ticket = IE_MMIO_Read32(COPROC_TICKET);
    if (ticket == 0u)
    {
        return 0;
    }
    IE_MMIO_Write32(COPROC_TICKET, ticket);
    IE_MMIO_Write32(COPROC_TIMEOUT, 1000u);
    IE_MMIO_Write32(COPROC_CMD, CMD_WAIT);
    if (IE_MMIO_Read32(COPROC_TICKET_STATUS) != TICKET_OK)
    {
        return 0;
    }
    out[0] = ie_resp[0];
    out[1] = ie_resp[1];
    out[2] = ie_resp[2];
    out[3] = ie_resp[3];
    return 1;
}

/* Batch request header (guest RAM; the worker reads it directly, byte-swapping).
 * The guest writes its native big-endian floats/u32 - no per-value work here. */
static unsigned char ie_batch_req[TNL_REQ_SIZE] __attribute__((aligned(4)));

/* Store a u32 into the (big-endian) request header at byte offset off. */
static void req_u32(unsigned off, uint32_t v)
{
    *(uint32_t *) (void *) (ie_batch_req + off) = v;
}
static void req_f32(unsigned off, float v)
{
    *(float *) (void *) (ie_batch_req + off) = v;
}

static int g_pending;            /* a batch is enqueued but not yet drained */
static uint8_t g_pending_head;   /* ring head we wrote; tail reaching it = done */

/* Diagnostics (LE u32 triplet at a fixed RAM address): drains completed,
 * spin-cap timeouts, worst spin count. Native stores; plain data. */
#define DRAIN_STAT_BASE 0x0009EF00u

/* Block until the in-flight batch (if any) has been consumed by the worker. */
void ie_coproc_drain(void)
{
    uint32_t spins = 0;
    static uint32_t drains, timeouts, worst;

    if (!g_pending)
    {
        return;
    }
    while (rrd8(RING_TAIL) != g_pending_head)
    {
        if (++spins > 200000000u)
        {
            timeouts++;
            break; /* worker stalled */
        }
    }
    drains++;
    if (spins > worst)
    {
        worst = spins;
    }
    *(volatile uint32_t *) (DRAIN_STAT_BASE + 0) = drains;
    *(volatile uint32_t *) (DRAIN_STAT_BASE + 4) = timeouts;
    *(volatile uint32_t *) (DRAIN_STAT_BASE + 8) = worst;
    g_pending = 0;
}

/* Enqueue a batch and return WITHOUT waiting, so the M68K keeps walking the
 * display list while the worker transforms in parallel. Any previously pending
 * batch is drained first (single request buffer + output-slot reuse). The caller
 * MUST ie_coproc_drain() before reading the output verts. Returns 1, or 0 to fall
 * back to the local path. */
int ie_coproc_tnl_batch_async(const ie_tnl_params *p)
{
    int i;

    if (!g_coproc_ok)
    {
        return 0;
    }
    ie_coproc_drain(); /* finish the previous batch before reusing ie_batch_req */
    for (i = 0; i < 16; ++i)
    {
        req_f32(TNL_REQ_MATRIX + (unsigned) i * 4u, p->m[i]);
    }
    req_u32(TNL_REQ_TEXSCALE_S, p->ss);
    req_u32(TNL_REQ_TEXSCALE_T, p->st);
    req_u32(TNL_REQ_NVERTS, p->n);
    req_u32(TNL_REQ_VERTS_PTR, (uint32_t) (uintptr_t) p->verts);
    req_u32(TNL_REQ_OUT_PTR, (uint32_t) (uintptr_t) p->out);
    req_u32(TNL_REQ_GEOMODE, p->geomode);
    req_u32(TNL_REQ_NUMLIGHTS, p->num_lights);
    for (i = 0; i < 3; ++i)
    {
        req_u32(TNL_REQ_AMB_COL + (unsigned) i * 4u, (uint32_t) p->amb_col[i]);
        req_u32(TNL_REQ_DIR_COL + (unsigned) i * 4u, (uint32_t) p->dir_col[i]);
        req_f32(TNL_REQ_LIGHTCOEF + (unsigned) i * 4u, p->light_coef ? p->light_coef[i] : 0.0f);
    }
    for (i = 0; i < 6; ++i)
    {
        req_f32(TNL_REQ_LOOKAT + (unsigned) i * 4u, p->lookat ? p->lookat[i] : 0.0f);
    }

    /* Direct shared-RAM ring enqueue - NO MMIO, so no JIT bails. Write the
     * request descriptor (LE, the worker reads it little-endian) at the head
     * slot, then advance head; the worker (already polling) consumes it in
     * parallel and advances tail. We DON'T wait here - completion is collected
     * by ie_coproc_drain() just before the verts are read. */
    {
        uint8_t slot = rrd8(RING_HEAD);
        uint32_t e = RING_ENTRIES + (uint32_t) slot * 32u;
        uint8_t newhead = (uint8_t) ((slot + 1u) & (RING_SLOTS - 1u));

        rwr32le(e + 0u, ++g_ticket);                                    /* ticket  */
        rwr32le(e + ENTRY_OP, OP_TNL_BATCH);
        rwr32le(e + ENTRY_REQPTR, (uint32_t) (uintptr_t) ie_batch_req);
        rwr32le(e + ENTRY_RESPPTR, (uint32_t) (uintptr_t) ie_batch_req); /* unused */
        rwr8(RING_HEAD, newhead);

        g_pending_head = newhead;
        g_pending = 1;
        /* diag: enqueue count */
        *(volatile uint32_t *) (DRAIN_STAT_BASE + 0xCu) =
            *(volatile uint32_t *) (DRAIN_STAT_BASE + 0xCu) + 1u;
    }
    return 1;
}

/* Synchronous batch: enqueue then wait. Used by the boot self-test. */
int ie_coproc_tnl_batch(const ie_tnl_params *p)
{
    if (!ie_coproc_tnl_batch_async(p))
    {
        return 0;
    }
    ie_coproc_drain();
    return 1;
}

/* OP_TRI_EMIT was retired with the ie68-port dc_fast_t emit path; this
 * branch's gfx_pc emits flat-float buf_vbo locally. */
