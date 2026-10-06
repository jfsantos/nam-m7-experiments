/*
 * Method 2: get the FMA you asked for.
 *
 * The specialized frame-major kernel from experiment 1, compiled twice with
 * the same flags (-O2 -ffast-math). Only the multiply-add is written
 * differently:
 *
 *   a += w * x       -ffast-math lets GCC reassociate the sums, so some
 *                    multiply-adds become a VMUL plus a VADD
 *   fmaf(w, x, a)    pins a as the accumulator: one VFMA per multiply-add
 *
 * Two kernels show the two sides of it:
 *
 *   frame-major (experiment 1)   limited by loads: 12 loads per 9 MACs. A
 *       VMUL can issue alongside a load but a VFMA can't (experiment 5), so
 *       the reassociated form may hide some load cost.
 *   6x3 register tile, pinned loads (experiments 6-7)   limited by the FPU:
 *       every extra VMUL/VADD costs a cycle, so fmaf should win.
 *
 * Count the instructions with:
 *   arm-none-eabi-objdump -d build/02-fma.elf | grep -c vmul
 */
#include "bench.h"
#include "placement.h"
#include "rings.h"

#define FM_NAME layer_mul_add
#define FM_MAC(acc, w, x) ((acc) += (w) * (x))
#include "../01-specialize/frame_major.h"
#undef FM_NAME

#define FM_NAME layer_fmaf
#define FM_MAC(acc, w, x) ((acc) = fmaf((w), (x), (acc)))
#include "../01-specialize/frame_major.h"

#define TILE_NAME tile_mul_add
#define TILE_T 6
#define TILE_PIN 1
#define TILE_MAC(acc, w, x) ((acc) += (w) * (x))
#include "../06-register-tiles/tile.h"

#define TILE_NAME tile_fmaf
#define TILE_T 6
#define TILE_PIN 1
#include "../06-register-tiles/tile.h"

static mstack_t g_stack;
static xstack_t g_xstack;

static void reset(void) { mstack_reset(&g_stack); }
static void run_mul_add(const float* in, float* out, int n) { mstack_process(&g_stack, layer_mul_add, in, out, n); }
static void run_fmaf(const float* in, float* out, int n) { mstack_process(&g_stack, layer_fmaf, in, out, n); }
static void xreset(void) { xstack_reset(&g_xstack); }
static void run_tile_mul_add(const float* in, float* out, int n) { xstack_process(&g_xstack, tile_mul_add, in, out, n); }
static void run_tile_fmaf(const float* in, float* out, int n) { xstack_process(&g_xstack, tile_fmaf, in, out, n); }

void experiment_run(void)
{
    bench_title("02 Get the FMA you asked for",
                "-O2 -ffast-math. a += w * x vs fmaf(w, x, a), in a load-bound and in an FPU-bound kernel.");
    const variant_t v[] = {
        {"frame-major, a += w * x", reset, run_mul_add, 0, 0},
        {"frame-major, fmaf(w, x, a)", reset, run_fmaf, 0, 0},
        {"6x3 tile, a += w * x", xreset, run_tile_mul_add, 0, 0},
        {"6x3 tile, fmaf(w, x, a)", xreset, run_tile_fmaf, 0, 0},
    };
    bench_run(v, 4);
}
