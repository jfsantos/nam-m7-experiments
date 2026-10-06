/*
 * Register-tiled layer kernel (mirrored rings).
 *
 * A tile is TILE_T consecutive frames x 3 outputs. Its TILE_T * 3 running
 * sums stay in registers across all K taps; for each tap and input channel
 * the tile loads TILE_T inputs and 3 weights, then does TILE_T * 3 FMAs:
 *
 *   registers:  T*3 sums + T inputs + 3 weights   (6-frame tile: 27 of 32)
 *   FMAs/load:  3T / (T + 3)                      (6-frame tile: 2.0)
 *
 * Frames left over at the end of a block (nf % TILE_T) use a 1-frame tile.
 *
 * Define before including:
 *   TILE_NAME   function name
 *   TILE_T      frames per tile (1..8)
 *   TILE_PIN    1: force each run of loads to finish before its FMAs
 *               (experiment 7); 0 or undefined: leave it to the compiler
 *   TILE_ATTR   placement attribute (default ITCM_CODE)
 *   TILE_MAC    multiply-add, default fmaf (experiment 2 tries a += w * x)
 */
#include "placement.h"
#include "rings.h"

#ifndef TILE_ATTR
#define TILE_ATTR ITCM_CODE
#endif
#ifndef TILE_PIN
#define TILE_PIN 0
#endif
#ifndef TILE_MAC
#define TILE_MAC(acc, w, x) ((acc) = fmaf((w), (x), (acc)))
#endif

#define TILE_CAT_(a, b) a##b
#define TILE_CAT(a, b) TILE_CAT_(a, b)

/* An empty asm that takes every value of the load run as an in/out FP
 * register operand: the loads must complete before it, and the FMAs that
 * use the values must come after it. GCC can no longer sink each load next
 * to its first use, between FMAs. */
#if TILE_PIN && defined(__arm__)
#if TILE_T == 1
#define TILE_PIN_RUN() __asm__ volatile("" : "+w"(x[0]), "+w"(w0), "+w"(w1), "+w"(w2))
#elif TILE_T == 2
#define TILE_PIN_RUN() __asm__ volatile("" : "+w"(x[0]), "+w"(x[1]), "+w"(w0), "+w"(w1), "+w"(w2))
#elif TILE_T == 3
#define TILE_PIN_RUN() __asm__ volatile("" : "+w"(x[0]), "+w"(x[1]), "+w"(x[2]), "+w"(w0), "+w"(w1), "+w"(w2))
#elif TILE_T == 4
#define TILE_PIN_RUN()                                                                                                 \
    __asm__ volatile("" : "+w"(x[0]), "+w"(x[1]), "+w"(x[2]), "+w"(x[3]), "+w"(w0), "+w"(w1), "+w"(w2))
#elif TILE_T == 6
#define TILE_PIN_RUN()                                                                                                 \
    __asm__ volatile("" : "+w"(x[0]), "+w"(x[1]), "+w"(x[2]), "+w"(x[3]), "+w"(x[4]), "+w"(x[5]), "+w"(w0), "+w"(w1), \
                     "+w"(w2))
#else
#error "TILE_PIN supports TILE_T 1, 2, 3, 4 and 6"
#endif
#else
#define TILE_PIN_RUN() ((void)0)
#endif

/* The pin for the 1-frame tail. */
#if TILE_PIN && defined(__arm__)
#define TILE_PIN_ONE() __asm__ volatile("" : "+w"(x0), "+w"(x1), "+w"(x2))
#else
#define TILE_PIN_ONE() ((void)0)
#endif

/* One frame: 3 sums in registers, 9 FMAs per tap. */
static inline __attribute__((always_inline)) void TILE_CAT(TILE_NAME, _one)(const a2_layer_t* L, int K,
                                                                            const float* const* tap, xring_t* next,
                                                                            float* out, int f)
{
    float z0 = L->b[0], z1 = L->b[1], z2 = L->b[2];
    for (int k = 0; k < K; k++) {
        const float* xp = tap[k] + 3 * f;
        const float* w = &L->w[k][0][0];
        float x0 = xp[0], x1 = xp[1], x2 = xp[2];
        TILE_PIN_ONE();
        TILE_MAC(z0, w[0], x0);
        TILE_MAC(z1, w[1], x0);
        TILE_MAC(z2, w[2], x0);
        TILE_MAC(z0, w[3], x1);
        TILE_MAC(z1, w[4], x1);
        TILE_MAC(z2, w[5], x1);
        TILE_MAC(z0, w[6], x2);
        TILE_MAC(z1, w[7], x2);
        TILE_MAC(z2, w[8], x2);
    }
    float y[3];
    a2_epilogue(L, tap[K - 1] + 3 * f, z0, z1, z2, y);
    xstack_emit(next, out, f, y);
}

/* TILE_T frames starting at f. */
static inline __attribute__((always_inline)) void TILE_CAT(TILE_NAME, _tile)(const a2_layer_t* L, int K,
                                                                             const float* const* tap, xring_t* next,
                                                                             float* out, int f)
{
    float acc[TILE_T][3];
#pragma GCC unroll 8
    for (int t = 0; t < TILE_T; t++) {
        acc[t][0] = L->b[0];
        acc[t][1] = L->b[1];
        acc[t][2] = L->b[2];
    }
    for (int k = 0; k < K; k++) {
        const float* xk = tap[k] + 3 * f;
        const float* wk = &L->w[k][0][0];
#pragma GCC unroll 3
        for (int c = 0; c < 3; c++) {
            float x[TILE_T];
#pragma GCC unroll 8
            for (int t = 0; t < TILE_T; t++)
                x[t] = xk[3 * t + c];
            float w0 = wk[3 * c + 0], w1 = wk[3 * c + 1], w2 = wk[3 * c + 2];
            TILE_PIN_RUN();
#pragma GCC unroll 8
            for (int t = 0; t < TILE_T; t++)
                TILE_MAC(acc[t][0], w0, x[t]);
#pragma GCC unroll 8
            for (int t = 0; t < TILE_T; t++)
                TILE_MAC(acc[t][1], w1, x[t]);
#pragma GCC unroll 8
            for (int t = 0; t < TILE_T; t++)
                TILE_MAC(acc[t][2], w2, x[t]);
        }
    }
#pragma GCC unroll 8
    for (int t = 0; t < TILE_T; t++) {
        float y[3];
        a2_epilogue(L, tap[K - 1] + 3 * (f + t), acc[t][0], acc[t][1], acc[t][2], y);
        xstack_emit(next, out, f + t, y);
    }
}

TILE_ATTR void TILE_NAME(const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out, int nf)
{
    int f = 0;
    for (; f + TILE_T <= nf; f += TILE_T)
        TILE_CAT(TILE_NAME, _tile)(L, K, tap, next, out, f);
    for (; f < nf; f++)
        TILE_CAT(TILE_NAME, _one)(L, K, tap, next, out, f);
}

#undef TILE_NAME
#undef TILE_T
#undef TILE_PIN
#undef TILE_PIN_RUN
#undef TILE_PIN_ONE
#undef TILE_ATTR
#undef TILE_MAC
