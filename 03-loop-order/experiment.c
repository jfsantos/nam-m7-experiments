/*
 * Method 3: invert the loop order.
 *
 *   frame-major  for each frame: for each tap. The 3 sums stay in registers,
 *                but a layer has K x 9 weights (135 for K = 15) and only 32
 *                FP registers, so every tap reloads its weights every frame.
 *   tap-major    for each tap: for each frame. One tap's 9 weights stay in
 *                registers for the whole block; the sums go through memory.
 *
 * Both use the masked delay lines and fmaf.
 */
#include "bench.h"
#include "placement.h"
#include "rings.h"

#define FM_NAME layer_frame_major
#include "../01-specialize/frame_major.h"

#define TM_NAME layer_tap_major
#define TM_SIG const a2_layer_t* L, int K, int D, const mring_t* in, mring_t* next, float* out, int nf
#define TM_X(k, f) mring_col(in, in->wp + (f) - (K - 1 - (k)) * D)
#define TM_EMIT(f, y) mstack_emit(next, out, (f), (y))
#include "tap_major.h"

static mstack_t g_stack;

static void reset(void) { mstack_reset(&g_stack); }
static void run_frame_major(const float* in, float* out, int n)
{
    mstack_process(&g_stack, layer_frame_major, in, out, n);
}
static void run_tap_major(const float* in, float* out, int n) { mstack_process(&g_stack, layer_tap_major, in, out, n); }

void experiment_run(void)
{
    bench_title("03 Invert the loop order",
                "Masked delay lines, fmaf. Frames outside taps (weights reloaded) vs taps outside frames.");
    const variant_t v[] = {
        {"frame-major (taps inside)", reset, run_frame_major, 0, 0},
        {"tap-major (taps outside)", reset, run_tap_major, 0, 0},
    };
    bench_run(v, 2);
}
