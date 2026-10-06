/*
 * Method 7: keep loads in runs.
 *
 * On the M7, a load or store right after an FMA waits ~3 cycles for it
 * (experiment 5). GCC's scheduler doesn't know that: it moves each load next
 * to its first use, which puts it right behind an FMA.
 *
 * The fix is an empty asm statement after each run of loads that takes every
 * loaded value as a "+w" (in/out FP register) operand. The loads must finish
 * before it and the FMAs must come after it, so each run of 9 loads issues
 * back to back and then the 18 FMAs do. The second scheduling pass (after
 * register allocation) can still interleave them, so the last variant also
 * turns it off for that file.
 *
 * Same 6x3 tile, same instructions; only their order changes. Compare:
 *   arm-none-eabi-objdump -d build/07-load-runs.elf | less
 */
#include "bench.h"
#include "rings.h"

void layer_tile6_compiler(const a2_layer_t*, int, const float* const*, xring_t*, float*, int);
void layer_tile6_pinned(const a2_layer_t*, int, const float* const*, xring_t*, float*, int);
void layer_tile6_pinned_nosched2(const a2_layer_t*, int, const float* const*, xring_t*, float*, int);

static xstack_t g_stack;

static void reset(void) { xstack_reset(&g_stack); }
static void run_compiler(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6_compiler, in, out, n); }
static void run_pinned(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6_pinned, in, out, n); }
static void run_pinned_nosched2(const float* in, float* out, int n)
{
    xstack_process(&g_stack, layer_tile6_pinned_nosched2, in, out, n);
}

void experiment_run(void)
{
    bench_title("07 Keep loads in runs",
                "6x3 register tile, mirrored delay lines. Compiler's load schedule vs load runs pinned before the FMAs.");
    const variant_t v[] = {
        {"compiler schedule", reset, run_compiler, 0, 0},
        {"pinned load runs", reset, run_pinned, 0, 0},
        {"pinned + -fno-schedule-insns2", reset, run_pinned_nosched2, 0, 0},
    };
    bench_run(v, 3);
}
