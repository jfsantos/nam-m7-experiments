#include "rings.h"

#include <string.h>

/* Large enough for the biggest layout (power-of-two rings, ~142 KB) and for
 * experiment 4's rewind buffers. Lives in AXI SRAM, behind the D-cache. */
#ifndef ARENA_FLOATS
#define ARENA_FLOATS 40960
#endif
static float g_arena[ARENA_FLOATS] __attribute__((aligned(32)));

static float* g_base = g_arena;
static long g_size = ARENA_FLOATS;
static long g_used = 0;

void arena_use(float* base, long nfloats)
{
    g_base = base;
    g_size = nfloats;
    g_used = 0;
}

void arena_use_default(void) { arena_use(g_arena, ARENA_FLOATS); }

float* arena_alloc(long nfloats)
{
    nfloats = (nfloats + 7) & ~7L; /* keep every block 32-byte aligned */
    if (g_used + nfloats > g_size)
        return 0; /* callers check: an experiment that overflows is a bug */
    float* p = g_base + g_used;
    g_used += nfloats;
    memset(p, 0, (size_t)nfloats * sizeof(float));
    return p;
}

/* ---- Masked power-of-two rings ------------------------------------------ */

void mstack_reset(mstack_t* s)
{
    for (int l = 0; l < A2_LAYERS; l++) {
        int cols = 1;
        while (cols < a2_lookback(l) + A2_BLOCK_MAX)
            cols <<= 1;
        s->ring[l].buf = arena_alloc(3L * cols);
        s->ring[l].mask = cols - 1;
        s->ring[l].wp = 0;
    }
}

void mstack_process(mstack_t* s, mlayer_fn fn, const float* in, float* out, int nf)
{
    const float* iw = a2_w->in_w;
    for (int f = 0; f < nf; f++) {
        const float y[3] = {iw[0] * in[f], iw[1] * in[f], iw[2] * in[f]};
        mstack_emit(&s->ring[0], 0, f, y);
    }
    for (int l = 0; l < A2_LAYERS; l++)
        fn(&a2_w->layer[l], a2_kernel[l], a2_dilation[l], &s->ring[l], l + 1 < A2_LAYERS ? &s->ring[l + 1] : 0, out,
           nf);
    for (int l = 0; l < A2_LAYERS; l++)
        s->ring[l].wp = (s->ring[l].wp + nf) & s->ring[l].mask;
}

/* ---- Mirrored exact-size rings ------------------------------------------ */

void xstack_reset(xstack_t* s)
{
    for (int l = 0; l < A2_LAYERS; l++) {
        const int R = a2_lookback(l) + A2_BLOCK_MAX;
        s->ring[l].buf = arena_alloc(3L * (R + A2_BLOCK_MAX));
        s->ring[l].R = R;
        s->ring[l].wp = 0;
    }
}

void xstack_process(xstack_t* s, xlayer_fn fn, const float* in, float* out, int nf)
{
    const float* iw = a2_w->in_w;
    for (int f = 0; f < nf; f++) {
        const float y[3] = {iw[0] * in[f], iw[1] * in[f], iw[2] * in[f]};
        xstack_emit(&s->ring[0], 0, f, y);
    }
    const float* tap[A2_KMAX];
    for (int l = 0; l < A2_LAYERS; l++) {
        const xring_t* r = &s->ring[l];
        const int K = a2_kernel[l], D = a2_dilation[l];
        for (int k = 0; k < K; k++) {
            int col = r->wp - (K - 1 - k) * D;
            if (col < 0)
                col += r->R;
            tap[k] = r->buf + 3 * col;
        }
        fn(&a2_w->layer[l], K, tap, l + 1 < A2_LAYERS ? &s->ring[l + 1] : 0, out, nf);
    }
    for (int l = 0; l < A2_LAYERS; l++) {
        int wp = s->ring[l].wp + nf;
        s->ring[l].wp = wp >= s->ring[l].R ? wp - s->ring[l].R : wp;
    }
}
