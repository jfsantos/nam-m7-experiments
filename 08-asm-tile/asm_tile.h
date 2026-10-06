/*
 * 6x3 register tile written in Cortex-M7 assembly (mirrored rings).
 *
 * The same tile and the same summation order as the C tile in
 * 06-register-tiles/tile.h. The outputs agree to float rounding (-ffast-math
 * lets GCC order the C epilogue's additions differently in each inlined
 * copy). What the assembly does that GCC won't:
 *
 *   - fixed register map: sums in s0-s17 (frame t, output o -> s[3t+o]),
 *     inputs in s18-s23, weights in s24-s26
 *   - two weights per 64-bit VLDR (d12 = s24:s25)
 *   - every run of 8 loads issues before its 18 FMAs
 *   - a prefetch of the next tile's inputs on every tap
 *
 * The epilogue (activation, 1x1, residual) stays in C.
 *
 * Define before including: ASM_TILE_NAME, and optionally ASM_TILE_ATTR
 * (placement, default ITCM_CODE). Cortex-M7 (FPv5) only.
 */
#include "placement.h"
#include "rings.h"

#ifndef ASM_TILE_ATTR
#define ASM_TILE_ATTR ITCM_CODE
#endif
#define ASM_CAT_(a, b) a##b
#define ASM_CAT(a, b) ASM_CAT_(a, b)

/* One input channel of one tap: 6 inputs (frames 0..5, 12 bytes apart),
 * 3 weights, 18 FMAs. X0 is the channel's byte offset, W0 its weights'. */
#define ASM_CHANNEL(x0, x1, x2, x3, x4, x5, w0, w2)                                                                    \
    "vldr s18, [%[rx], #" #x0 "]\n\t"                                                                                  \
    "vldr s19, [%[rx], #" #x1 "]\n\t"                                                                                  \
    "vldr s20, [%[rx], #" #x2 "]\n\t"                                                                                  \
    "vldr s21, [%[rx], #" #x3 "]\n\t"                                                                                  \
    "vldr s22, [%[rx], #" #x4 "]\n\t"                                                                                  \
    "vldr s23, [%[rx], #" #x5 "]\n\t"                                                                                  \
    "vldr d12, [%[rw], #" #w0 "]\n\t"                                                                                  \
    "vldr s26, [%[rw], #" #w2 "]\n\t"                                                                                  \
    "vfma.f32 s0, s24, s18\n\t"                                                                                        \
    "vfma.f32 s3, s24, s19\n\t"                                                                                        \
    "vfma.f32 s6, s24, s20\n\t"                                                                                        \
    "vfma.f32 s9, s24, s21\n\t"                                                                                        \
    "vfma.f32 s12, s24, s22\n\t"                                                                                       \
    "vfma.f32 s15, s24, s23\n\t"                                                                                       \
    "vfma.f32 s1, s25, s18\n\t"                                                                                        \
    "vfma.f32 s4, s25, s19\n\t"                                                                                        \
    "vfma.f32 s7, s25, s20\n\t"                                                                                        \
    "vfma.f32 s10, s25, s21\n\t"                                                                                       \
    "vfma.f32 s13, s25, s22\n\t"                                                                                       \
    "vfma.f32 s16, s25, s23\n\t"                                                                                       \
    "vfma.f32 s2, s26, s18\n\t"                                                                                        \
    "vfma.f32 s5, s26, s19\n\t"                                                                                        \
    "vfma.f32 s8, s26, s20\n\t"                                                                                        \
    "vfma.f32 s11, s26, s21\n\t"                                                                                       \
    "vfma.f32 s14, s26, s22\n\t"                                                                                       \
    "vfma.f32 s17, s26, s23\n\t"

/* Start one frame's 3 sums from the bias. */
#define ASM_BIAS(r0, r1, r2) "vldr " r0 ", [%[b]]\n\t" "vldr " r1 ", [%[b], #4]\n\t" "vldr " r2 ", [%[b], #8]\n\t"

static inline __attribute__((always_inline)) void ASM_CAT(ASM_TILE_NAME, _tile)(const a2_layer_t* L, int K,
                                                                                const float* const* tap,
                                                                                xring_t* next, float* out, int f)
{
    /* Output-only: the asm loads the bias itself (an in/out operand counts
     * twice toward GCC's 30-operand limit). */
    register float a00 __asm__("s0"), a01 __asm__("s1"), a02 __asm__("s2");
    register float a10 __asm__("s3"), a11 __asm__("s4"), a12 __asm__("s5");
    register float a20 __asm__("s6"), a21 __asm__("s7"), a22 __asm__("s8");
    register float a30 __asm__("s9"), a31 __asm__("s10"), a32 __asm__("s11");
    register float a40 __asm__("s12"), a41 __asm__("s13"), a42 __asm__("s14");
    register float a50 __asm__("s15"), a51 __asm__("s16"), a52 __asm__("s17");
    const float* const* tp = tap;
    const float* rw = &L->w[0][0][0];
    const float* rx;
    int k = K;
    __asm__ volatile(ASM_BIAS("s0", "s1", "s2") ASM_BIAS("s3", "s4", "s5") ASM_BIAS("s6", "s7", "s8")
                     ASM_BIAS("s9", "s10", "s11") ASM_BIAS("s12", "s13", "s14") ASM_BIAS("s15", "s16", "s17")
                     "1:\n\t"
                     "ldr %[rx], [%[tp]], #4\n\t" /* tap k's column for frame 0 */
                     "add %[rx], %[rx], %[foff]\n\t"
                     "pld [%[rx], #72]\n\t" /* next tile */
                     ASM_CHANNEL(0, 12, 24, 36, 48, 60, 0, 8)
                     ASM_CHANNEL(4, 16, 28, 40, 52, 64, 12, 20)
                     ASM_CHANNEL(8, 20, 32, 44, 56, 68, 24, 32)
                     "add %[rw], %[rw], #36\n\t" /* next tap's 9 weights */
                     "subs %[k], %[k], #1\n\t"
                     "bne 1b\n\t"
                     : "=t"(a00), "=t"(a01), "=t"(a02), "=t"(a10), "=t"(a11), "=t"(a12), "=t"(a20), "=t"(a21),
                       "=t"(a22), "=t"(a30), "=t"(a31), "=t"(a32), "=t"(a40), "=t"(a41), "=t"(a42), "=t"(a50),
                       "=t"(a51), "=t"(a52), [tp] "+r"(tp), [rw] "+r"(rw), [k] "+r"(k), [rx] "=&r"(rx)
                     : [foff] "r"(12 * f), [b] "r"(L->b)
                     : "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "cc", "memory");
    const float z[6][3] = {{a00, a01, a02}, {a10, a11, a12}, {a20, a21, a22},
                           {a30, a31, a32}, {a40, a41, a42}, {a50, a51, a52}};
    for (int t = 0; t < 6; t++) {
        float y[3];
        a2_epilogue(L, tap[K - 1] + 3 * (f + t), z[t][0], z[t][1], z[t][2], y);
        xstack_emit(next, out, f + t, y);
    }
}

/* Leftover frames (nf % 6), one at a time in C. */
static inline __attribute__((always_inline)) void ASM_CAT(ASM_TILE_NAME, _one)(const a2_layer_t* L, int K,
                                                                               const float* const* tap,
                                                                               xring_t* next, float* out, int f)
{
    float z0 = L->b[0], z1 = L->b[1], z2 = L->b[2];
    for (int k = 0; k < K; k++) {
        const float* x = tap[k] + 3 * f;
        const float* w = &L->w[k][0][0];
        z0 = fmaf(w[0], x[0], z0);
        z1 = fmaf(w[1], x[0], z1);
        z2 = fmaf(w[2], x[0], z2);
        z0 = fmaf(w[3], x[1], z0);
        z1 = fmaf(w[4], x[1], z1);
        z2 = fmaf(w[5], x[1], z2);
        z0 = fmaf(w[6], x[2], z0);
        z1 = fmaf(w[7], x[2], z1);
        z2 = fmaf(w[8], x[2], z2);
    }
    float y[3];
    a2_epilogue(L, tap[K - 1] + 3 * f, z0, z1, z2, y);
    xstack_emit(next, out, f, y);
}

ASM_TILE_ATTR void ASM_TILE_NAME(const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out,
                                 int nf)
{
    int f = 0;
    for (; f + 6 <= nf; f += 6)
        ASM_CAT(ASM_TILE_NAME, _tile)(L, K, tap, next, out, f);
    for (; f < nf; f++)
        ASM_CAT(ASM_TILE_NAME, _one)(L, K, tap, next, out, f);
}

#undef ASM_TILE_NAME
#undef ASM_TILE_ATTR
