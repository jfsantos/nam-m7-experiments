/*
 * Specialized frame-major layer kernel (masked rings).
 *
 * Channel count, kernel size and loop structure are compile-time constants:
 * one frame at a time, the 3 running sums stay in registers across all taps,
 * and each tap is 9 straight-line multiply-adds. The weights don't fit in
 * registers, so every tap reloads its 9 weights for every frame
 * (experiment 3 fixes that).
 *
 * Include with FM_NAME set to the function name. FM_MAC(acc, w, x) is the
 * multiply-add; it defaults to fmaf (experiment 2 compares the two forms).
 */
#include "placement.h"
#include "rings.h"

#ifndef FM_MAC
#define FM_MAC(acc, w, x) ((acc) = fmaf((w), (x), (acc)))
#endif
#define FM_CAT_(a, b) a##b
#define FM_CAT(a, b) FM_CAT_(a, b)

static inline __attribute__((always_inline)) void FM_CAT(FM_NAME, _k)(const a2_layer_t* L, const int K, const int D,
                                                                      const mring_t* in, mring_t* next, float* out,
                                                                      int nf)
{
    for (int f = 0; f < nf; f++) {
        float z0 = L->b[0], z1 = L->b[1], z2 = L->b[2];
        for (int k = 0; k < K; k++) {
            const float* x = mring_col(in, in->wp + f - (K - 1 - k) * D);
            const float* w = &L->w[k][0][0];
            FM_MAC(z0, w[0], x[0]);
            FM_MAC(z1, w[1], x[0]);
            FM_MAC(z2, w[2], x[0]);
            FM_MAC(z0, w[3], x[1]);
            FM_MAC(z1, w[4], x[1]);
            FM_MAC(z2, w[5], x[1]);
            FM_MAC(z0, w[6], x[2]);
            FM_MAC(z1, w[7], x[2]);
            FM_MAC(z2, w[8], x[2]);
        }
        float y[3];
        a2_epilogue(L, mring_col(in, in->wp + f), z0, z1, z2, y);
        mstack_emit(next, out, f, y);
    }
}

/* One instance per kernel size, so K is a constant in each loop. */
ITCM_CODE void FM_NAME(const a2_layer_t* L, int K, int D, const mring_t* in, mring_t* next, float* out, int nf)
{
    if (K == 6)
        FM_CAT(FM_NAME, _k)(L, 6, D, in, next, out, nf);
    else
        FM_CAT(FM_NAME, _k)(L, 15, D, in, next, out, nf);
}

#undef FM_MAC
