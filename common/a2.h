/*
 * The workload shared by every experiment: an A2-Lite-shaped WaveNet stack.
 *
 *   23 layers, 3 channels, kernel size 6 (21 layers) or 15 (2 layers),
 *   dilations 1 to 239, as in NAM's A2-Lite architecture.
 *
 * Per layer and frame t, with x the layer's input stream:
 *
 *   z[o] = b[o] + sum_k sum_i w[k][i][o] * x[t - (K-1-k)*D][i]   dilated conv
 *   a[o] = max(z[o], 0.01 * z[o])                                 LeakyReLU
 *   y[o] = x[t][o] + b1[o] + sum_i w1[i][o] * a[i]                1x1 + residual
 *
 * y is the next layer's input. The stack's input is the mono signal times
 * a 1->3 projection; its output is channel 0 of the last layer.
 *
 * About 1,614 multiply-adds per sample. Weights are synthetic and
 * deterministic: speed does not depend on their values.
 */
#ifndef A2_H
#define A2_H

#include <math.h>
#include <stdint.h>

#define A2_C 3
#define A2_LAYERS 23
#define A2_KMAX 15

/* Largest block any experiment processes. Ring buffers are sized for it. */
#define A2_BLOCK_MAX 48

typedef struct {
    float w[A2_KMAX][A2_C][A2_C]; /* [tap][in][out]; tap K-1 is the current frame */
    float b[A2_C];
    float w1[A2_C][A2_C];         /* 1x1 [in][out] */
    float b1[A2_C];
} a2_layer_t;

typedef struct {
    a2_layer_t layer[A2_LAYERS];
    float in_w[A2_C]; /* 1 -> 3 input projection */
} a2_weights_t;

extern const int a2_kernel[A2_LAYERS];
extern const int a2_dilation[A2_LAYERS];

/* The weights every kernel reads. Points at a DTCM copy by default
 * (a2_weights_dtcm); experiment 9 points it elsewhere. */
extern const a2_weights_t* a2_w;
extern a2_weights_t a2_weights_dtcm;

void a2_make_weights(a2_weights_t* w);
static inline int a2_lookback(int l) { return (a2_kernel[l] - 1) * a2_dilation[l]; }
int a2_total_lookback(void); /* sum over layers */

/* Deterministic test signal: a few decaying plucks. */
float a2_input(long i);

/* Multiply-adds per sample, for reports. */
int a2_macs_per_sample(void);

/* fmaxf compiles to a single VMAXNM on Cortex-M7 (FPv5). */
static inline float a2_leaky(float z) { return fmaxf(z, 0.01f * z); }

/* 1x1 + residual for one frame: y = x + b1 + w1^T leaky(z). */
static inline void a2_epilogue(const a2_layer_t* L, const float* x, float z0, float z1, float z2, float* y)
{
    const float a0 = a2_leaky(z0), a1 = a2_leaky(z1), a2v = a2_leaky(z2);
    y[0] = x[0] + L->b1[0] + L->w1[0][0] * a0 + L->w1[1][0] * a1 + L->w1[2][0] * a2v;
    y[1] = x[1] + L->b1[1] + L->w1[0][1] * a0 + L->w1[1][1] * a1 + L->w1[2][1] * a2v;
    y[2] = x[2] + L->b1[2] + L->w1[0][2] * a0 + L->w1[1][2] * a1 + L->w1[2][2] * a2v;
}

#endif
