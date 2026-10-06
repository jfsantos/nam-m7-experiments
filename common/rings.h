/*
 * Delay lines (ring buffers) for the layer stack, and the stack drivers.
 *
 * Every layer keeps the history of its input stream: frame t of layer l is
 * read again by layer l's taps up to (K-1)*D frames later. Frames are stored
 * channel-interleaved: 3 floats per column.
 *
 * Two layouts are shared by several experiments (experiment 4 compares them):
 *
 *   mring_t  power-of-two ring, every access masked: col = (wp + f - lag) & mask
 *   xring_t  exact-size ring of R = lookback + N columns, plus an N-column
 *            mirrored tail. A read window of up to N frames starting at any
 *            column is contiguous, so kernels use plain pointers.
 *
 * Both stacks write layer l's output straight into layer l+1's ring. The
 * last layer writes channel 0 to the output buffer.
 */
#ifndef RINGS_H
#define RINGS_H

#include "a2.h"

/* ---- Arena: variants run one after the other and share the same memory. */
void arena_use(float* base, long nfloats); /* default: a static SRAM arena */
void arena_use_default(void);
float* arena_alloc(long nfloats);          /* zeroed, 32-byte aligned */

/* ---- Masked power-of-two rings ------------------------------------------ */
typedef struct {
    float* buf;
    int mask; /* columns - 1 */
    int wp;   /* column of the current block's first frame */
} mring_t;

typedef struct {
    mring_t ring[A2_LAYERS];
} mstack_t;

/* Layer kernel: reads `in`, writes next layer's ring (or out[] if next is NULL). */
typedef void (*mlayer_fn)(const a2_layer_t* L, int K, int D, const mring_t* in, mring_t* next, float* out,
                          int nf);

void mstack_reset(mstack_t* s);
void mstack_process(mstack_t* s, mlayer_fn fn, const float* in, float* out, int nf);

static inline float* mring_col(const mring_t* r, int col) { return r->buf + 3 * (col & r->mask); }

static inline void mstack_emit(mring_t* next, float* out, int f, const float* y)
{
    if (next) {
        float* p = mring_col(next, next->wp + f);
        p[0] = y[0];
        p[1] = y[1];
        p[2] = y[2];
    } else {
        out[f] = y[0];
    }
}

/* ---- Mirrored exact-size rings ------------------------------------------ */
typedef struct {
    float* buf;
    int R;  /* lookback + A2_BLOCK_MAX columns; storage has R + A2_BLOCK_MAX */
    int wp; /* in [0, R) */
} xring_t;

typedef struct {
    xring_t ring[A2_LAYERS];
} xstack_t;

/* Layer kernel. tap[k] points at the column tap k reads for frame 0 of this
 * block; frame f of tap k is tap[k] + 3*f. tap[K-1] is the current frame. */
typedef void (*xlayer_fn)(const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out, int nf);

void xstack_reset(xstack_t* s);
void xstack_process(xstack_t* s, xlayer_fn fn, const float* in, float* out, int nf);

/* Store one frame at column col in [0, R + N), keeping the mirror in sync. */
static inline void xring_put(xring_t* r, int col, const float* y)
{
    float* p = r->buf + 3 * col;
    p[0] = y[0];
    p[1] = y[1];
    p[2] = y[2];
    int m = col < A2_BLOCK_MAX ? col + r->R : (col >= r->R ? col - r->R : -1);
    if (m >= 0) {
        float* q = r->buf + 3 * m;
        q[0] = y[0];
        q[1] = y[1];
        q[2] = y[2];
    }
}

static inline void xstack_emit(xring_t* next, float* out, int f, const float* y)
{
    if (next)
        xring_put(next, next->wp + f, y);
    else
        out[f] = y[0];
}

#endif
