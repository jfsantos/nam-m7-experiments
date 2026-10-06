/*
 * Tap-major layer kernel: taps outside, frames inside.
 *
 * For each tap, its 9 weights are loaded once into registers and the whole
 * block is swept. The running sums can't stay in registers across taps (a
 * block has 48 frames x 3 sums), so they go through a small scratch buffer
 * in DTCM. The last tap is fused with the epilogue (activation, 1x1,
 * residual), so there is no extra pass over the block.
 *
 * A template over the delay-line layout. Define before including:
 *   TM_NAME        function name
 *   TM_SIG         its parameter list; must provide L (layer), K, nf
 *   TM_X(k, f)     pointer to the 3 input channels tap k reads for frame f
 *   TM_EMIT(f, y)  store frame f's output y[3] (next ring or output)
 */
#include "placement.h"
#include "rings.h"

#ifndef TM_SCRATCH
#define TM_SCRATCH
DTCM_DATA static float tm_sums[A2_BLOCK_MAX * A2_C];
#endif

ITCM_CODE void TM_NAME(TM_SIG)
{
    float* s = tm_sums;
    {   /* first tap: start from the bias */
        const float* w = &L->w[0][0][0];
        const float w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3], w4 = w[4], w5 = w[5], w6 = w[6], w7 = w[7], w8 = w[8];
        const float b0 = L->b[0], b1 = L->b[1], b2 = L->b[2];
        for (int f = 0; f < nf; f++) {
            const float* x = TM_X(0, f);
            s[3 * f + 0] = fmaf(w6, x[2], fmaf(w3, x[1], fmaf(w0, x[0], b0)));
            s[3 * f + 1] = fmaf(w7, x[2], fmaf(w4, x[1], fmaf(w1, x[0], b1)));
            s[3 * f + 2] = fmaf(w8, x[2], fmaf(w5, x[1], fmaf(w2, x[0], b2)));
        }
    }
    for (int k = 1; k < K - 1; k++) {
        const float* w = &L->w[k][0][0];
        const float w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3], w4 = w[4], w5 = w[5], w6 = w[6], w7 = w[7], w8 = w[8];
        for (int f = 0; f < nf; f++) {
            const float* x = TM_X(k, f);
            s[3 * f + 0] = fmaf(w6, x[2], fmaf(w3, x[1], fmaf(w0, x[0], s[3 * f + 0])));
            s[3 * f + 1] = fmaf(w7, x[2], fmaf(w4, x[1], fmaf(w1, x[0], s[3 * f + 1])));
            s[3 * f + 2] = fmaf(w8, x[2], fmaf(w5, x[1], fmaf(w2, x[0], s[3 * f + 2])));
        }
    }
    {   /* last tap (the current frame), fused with the epilogue */
        const float* w = &L->w[K - 1][0][0];
        const float w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3], w4 = w[4], w5 = w[5], w6 = w[6], w7 = w[7], w8 = w[8];
        for (int f = 0; f < nf; f++) {
            const float* x = TM_X(K - 1, f);
            const float z0 = fmaf(w6, x[2], fmaf(w3, x[1], fmaf(w0, x[0], s[3 * f + 0])));
            const float z1 = fmaf(w7, x[2], fmaf(w4, x[1], fmaf(w1, x[0], s[3 * f + 1])));
            const float z2 = fmaf(w8, x[2], fmaf(w5, x[1], fmaf(w2, x[0], s[3 * f + 2])));
            float y[3];
            a2_epilogue(L, x, z0, z1, z2, y);
            TM_EMIT(f, y);
        }
    }
}

#undef TM_NAME
#undef TM_SIG
#undef TM_X
#undef TM_EMIT
