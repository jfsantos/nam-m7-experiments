/*
 * Method 4: delay lines without wrap logic.
 *
 * The tap-major kernel from experiment 3 on three delay-line layouts:
 *
 *   rewind     linear buffer; when it fills up, memmove the history back
 *              to the start. Cheap reads, but periodic spikes (see max/block).
 *   masked     power-of-two ring; every access is (wp + f - lag) & mask.
 *   mirrored   exact-size ring with an N-column mirrored tail: every read
 *              window is contiguous, reads are plain pointer increments, and
 *              there is never a rewind.
 */
#include <string.h>

#include "bench.h"
#include "placement.h"
#include "rings.h"

/* ---- rewind buffers ------------------------------------------------------ */
#define REWIND_BLOCKS 3 /* free space after the history, in blocks */

typedef struct {
    float* buf;
    int cap; /* columns */
    int wp;  /* column of the current block's first frame; >= lookback */
    int lookback;
} rring_t;

static rring_t g_rew[A2_LAYERS];

static void rewind_reset(void)
{
    for (int l = 0; l < A2_LAYERS; l++) {
        g_rew[l].lookback = a2_lookback(l);
        g_rew[l].cap = a2_lookback(l) + REWIND_BLOCKS * A2_BLOCK_MAX;
        g_rew[l].buf = arena_alloc(3L * g_rew[l].cap);
        g_rew[l].wp = g_rew[l].lookback;
    }
}

#define TM_NAME layer_rewind
#define TM_SIG const a2_layer_t* L, int K, int D, const rring_t* in, rring_t* next, float* out, int nf
#define TM_X(k, f) (in->buf + 3 * (in->wp + (f) - (K - 1 - (k)) * D))
#define TM_EMIT(f, y) rewind_emit(next, out, (f), (y))
static inline void rewind_emit(rring_t* next, float* out, int f, const float* y)
{
    if (next) {
        float* p = next->buf + 3 * (next->wp + f);
        p[0] = y[0];
        p[1] = y[1];
        p[2] = y[2];
    } else {
        out[f] = y[0];
    }
}
#include "../03-loop-order/tap_major.h"

static void run_rewind(const float* in, float* out, int nf)
{
    const float* iw = a2_w->in_w;
    for (int f = 0; f < nf; f++) {
        const float y[3] = {iw[0] * in[f], iw[1] * in[f], iw[2] * in[f]};
        rewind_emit(&g_rew[0], 0, f, y);
    }
    for (int l = 0; l < A2_LAYERS; l++)
        layer_rewind(&a2_w->layer[l], a2_kernel[l], a2_dilation[l], &g_rew[l], l + 1 < A2_LAYERS ? &g_rew[l + 1] : 0,
                     out, nf);
    for (int l = 0; l < A2_LAYERS; l++) {
        rring_t* r = &g_rew[l];
        r->wp += nf;
        if (r->wp + A2_BLOCK_MAX > r->cap) { /* full: move the history back */
            memmove(r->buf, r->buf + 3 * (r->wp - r->lookback), sizeof(float) * 3 * r->lookback);
            r->wp = r->lookback;
        }
    }
}

/* ---- masked and mirrored rings (common/rings.c) -------------------------- */
#define TM_NAME layer_masked
#define TM_SIG const a2_layer_t* L, int K, int D, const mring_t* in, mring_t* next, float* out, int nf
#define TM_X(k, f) mring_col(in, in->wp + (f) - (K - 1 - (k)) * D)
#define TM_EMIT(f, y) mstack_emit(next, out, (f), (y))
#include "../03-loop-order/tap_major.h"

#define TM_NAME layer_mirrored
#define TM_SIG const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out, int nf
#define TM_X(k, f) (tap[k] + 3 * (f))
#define TM_EMIT(f, y) xstack_emit(next, out, (f), (y))
#include "../03-loop-order/tap_major.h"

static mstack_t g_mstack;
static xstack_t g_xstack;

static void masked_reset(void) { mstack_reset(&g_mstack); }
static void mirrored_reset(void) { xstack_reset(&g_xstack); }
static void run_masked(const float* in, float* out, int n) { mstack_process(&g_mstack, layer_masked, in, out, n); }
static void run_mirrored(const float* in, float* out, int n) { xstack_process(&g_xstack, layer_mirrored, in, out, n); }

void experiment_run(void)
{
    bench_title("04 Delay lines without wrap logic",
                "Tap-major kernel. Rewind (memmove when full) vs masked power-of-two ring vs mirrored exact ring.");
    const variant_t v[] = {
        {"rewind buffer (memmove when full)", rewind_reset, run_rewind, 0, 0},
        {"masked power-of-two ring", masked_reset, run_masked, 0, 0},
        {"mirrored exact-size ring", mirrored_reset, run_mirrored, 0, 0},
    };
    bench_run(v, 3);
}
