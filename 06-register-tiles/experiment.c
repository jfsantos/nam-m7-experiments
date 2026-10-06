/*
 * Method 6: keep the running sums in registers.
 *
 * Tap-major (experiment 3) keeps the weights in registers but sends every
 * running sum through memory on every tap. A register tile keeps T frames x
 * 3 outputs of sums in registers across all taps instead; the budget is
 *
 *   T*3 sums + T inputs + 3 weights <= 32 FP registers
 *
 *   T = 1   7 registers, 0.75 FMAs per load
 *   T = 3  15 registers, 1.5
 *   T = 6  27 registers, 2.0   (the largest that fits with room to spare)
 *   T = 8  35 registers: does not fit, the compiler spills to the stack
 *
 * All variants use mirrored delay lines (experiment 4) and fmaf. The loads
 * are left to the compiler's schedule here; experiment 7 fixes that.
 */
#include "bench.h"
#include "placement.h"
#include "rings.h"

#define TM_NAME layer_tap_major
#define TM_SIG const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out, int nf
#define TM_X(k, f) (tap[k] + 3 * (f))
#define TM_EMIT(f, y) xstack_emit(next, out, (f), (y))
#include "../03-loop-order/tap_major.h"

#define TILE_NAME layer_tile1
#define TILE_T 1
#include "tile.h"
#define TILE_NAME layer_tile3
#define TILE_T 3
#include "tile.h"
#define TILE_NAME layer_tile6
#define TILE_T 6
#include "tile.h"
#define TILE_NAME layer_tile8
#define TILE_T 8
#include "tile.h"

static xstack_t g_stack;

static void reset(void) { xstack_reset(&g_stack); }
static void run_tap_major(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tap_major, in, out, n); }
static void run_tile1(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile1, in, out, n); }
static void run_tile3(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile3, in, out, n); }
static void run_tile6(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6, in, out, n); }
static void run_tile8(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile8, in, out, n); }

void experiment_run(void)
{
    bench_title("06 Keep the running sums in registers",
                "Mirrored delay lines, fmaf. Tap-major (sums in memory) vs register tiles of T frames x 3 outputs.");
    const variant_t v[] = {
        {"tap-major, sums in memory", reset, run_tap_major, 0, 0},
        {"tile 1x3  (7 registers)", reset, run_tile1, 0, 0},
        {"tile 3x3  (15 registers)", reset, run_tile3, 0, 0},
        {"tile 6x3  (27 registers)", reset, run_tile6, 0, 0},
        {"tile 8x3  (35: spills)", reset, run_tile8, 0, 0},
    };
    bench_run(v, 5);
}
