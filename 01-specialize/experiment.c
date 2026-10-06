/*
 * Method 1: specialize the code to the model.
 *
 * Both variants use the same loop order (frame by frame) and the same masked
 * delay lines. They differ only in what is known when the code is compiled:
 *
 *   generic      shapes are run-time values, as in a library. Each frame and
 *                tap calls a matrix-vector routine whose loops run over
 *                run-time sizes and accumulate into memory.
 *   specialized  3 channels and the kernel sizes are constants: 9
 *                straight-line multiply-adds per tap, sums kept in registers.
 */
#include "bench.h"
#include "placement.h"
#include "rings.h"

#define FM_NAME layer_specialized
#include "frame_major.h"

/* Read at run time so the compiler can't specialize the generic kernel. */
static volatile int g_channels = A2_C;

/* y[r] += sum_c W[c][r] * x[c]: a typical library matrix-vector product. */
ITCM_CODE static void gemv_acc(float* y, const float* W, const float* x, int rows, int cols)
{
    for (int c = 0; c < cols; c++)
        for (int r = 0; r < rows; r++)
            y[r] += W[c * rows + r] * x[c];
}

ITCM_CODE static void layer_generic(const a2_layer_t* L, int K, int D, const mring_t* in, mring_t* next, float* out,
                                    int nf)
{
    const int C = g_channels;
    float z[A2_C], a[A2_C], y[A2_C];
    for (int f = 0; f < nf; f++) {
        for (int o = 0; o < C; o++)
            z[o] = L->b[o];
        for (int k = 0; k < K; k++)
            gemv_acc(z, &L->w[k][0][0], mring_col(in, in->wp + f - (K - 1 - k) * D), C, C);
        const float* x = mring_col(in, in->wp + f);
        for (int o = 0; o < C; o++) {
            a[o] = a2_leaky(z[o]);
            y[o] = x[o] + L->b1[o];
        }
        gemv_acc(y, &L->w1[0][0], a, C, C);
        mstack_emit(next, out, f, y);
    }
}

static mstack_t g_stack;

static void reset(void) { mstack_reset(&g_stack); }
static void run_generic(const float* in, float* out, int n) { mstack_process(&g_stack, layer_generic, in, out, n); }
static void run_specialized(const float* in, float* out, int n)
{
    mstack_process(&g_stack, layer_specialized, in, out, n);
}

void experiment_run(void)
{
    bench_title("01 Specialize the code to the model",
                "Frame-major loops, masked delay lines. Run-time shapes and a library GEMV vs compile-time shapes.");
    const variant_t v[] = {
        {"generic GEMV, run-time shapes", reset, run_generic, 0, 0},
        {"specialized, compile-time shapes", reset, run_specialized, 0, 0},
    };
    bench_run(v, 2);
}
