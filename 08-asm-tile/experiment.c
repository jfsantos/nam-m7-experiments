/*
 * Method 8: write the inner tile in assembly.
 *
 * Even with pinned load runs (experiment 7), GCC allocates registers its own
 * way and never merges two adjacent single-precision loads into one 64-bit
 * VLDR. The assembly tile (asm_tile.h) fixes the register map, loads two
 * weights per instruction, issues each run of loads before its FMAs and
 * prefetches the next tile. Same tile, same summation order: the output
 * matches the C tile to float rounding.
 *
 * Cortex-M7 only; the host build runs the C tile alone.
 */
#include "bench.h"
#include "rings.h"

#define TILE_NAME layer_tile6_c
#define TILE_T 6
#define TILE_PIN 1
#include "../06-register-tiles/tile.h"

#if defined(__ARM_ARCH_7EM__) && defined(__ARM_FP)
#define HAVE_ASM_TILE 1
#define ASM_TILE_NAME layer_tile6_asm
#include "asm_tile.h"
#endif

static xstack_t g_stack;

static void reset(void) { xstack_reset(&g_stack); }
static void run_c(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6_c, in, out, n); }
#ifdef HAVE_ASM_TILE
static void run_asm(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6_asm, in, out, n); }
#endif

void experiment_run(void)
{
    bench_title("08 Write the inner tile in assembly",
                "6x3 register tile, mirrored delay lines. C with pinned load runs vs hand-allocated assembly.");
    const variant_t v[] = {
        {"C tile, pinned load runs", reset, run_c, 0, 0},
#ifdef HAVE_ASM_TILE
        {"assembly tile", reset, run_asm, 0, 0},
#endif
    };
    bench_run(v, (int)(sizeof(v) / sizeof(v[0])));
}
