/*
 * Method 9: place data in tightly coupled memory.
 *
 * STM32H750 memories (Daisy Seed):
 *   ITCM 64 KB     zero-wait instruction memory
 *   DTCM 128 KB    zero-wait data memory (also holds the stack)
 *   AXI SRAM       512 KB behind the 16 KB D-cache
 *   QSPI flash     8 MB, code executes in place behind the 16 KB I-cache
 *
 * The same kernel (the assembly tile from experiment 8) with its code,
 * weights and delay lines in different places, each run with warm caches
 * and with both caches flushed before every block ("cold": what an audio
 * callback sees after other work evicted everything).
 *
 *   code QSPI, data SRAM            nothing placed: the default
 *   code ITCM, weights DTCM         the setup of experiments 1-8 and 10
 *   code ITCM, everything in DTCM   delay lines too (103 KB of the 128 KB)
 */
#include <string.h>

#include "bench.h"
#include "placement.h"
#include "rings.h"

#if defined(__ARM_ARCH_7EM__) && defined(__ARM_FP)
#define ASM_TILE_NAME layer_itcm
#define ASM_TILE_ATTR ITCM_CODE
#include "../08-asm-tile/asm_tile.h"
#define ASM_TILE_NAME layer_flash
#define ASM_TILE_ATTR FLASH_CODE
#include "../08-asm-tile/asm_tile.h"
#else /* host: the C tile stands in */
#define TILE_NAME layer_itcm
#define TILE_T 6
#include "../06-register-tiles/tile.h"
#define TILE_NAME layer_flash
#define TILE_T 6
#define TILE_ATTR FLASH_CODE
#include "../06-register-tiles/tile.h"
#endif

/* Delay lines in DTCM: mirrored rings need 25,659 floats (+ alignment). */
#define DTCM_ARENA_FLOATS 25824
DTCM_DATA static float g_dtcm_arena[DTCM_ARENA_FLOATS] __attribute__((aligned(32)));

/* A copy of the weights in AXI SRAM. */
static a2_weights_t g_weights_sram;

static xstack_t g_stack;

static void reset_sram(void)
{
    g_weights_sram = a2_weights_dtcm;
    a2_w = &g_weights_sram;
    xstack_reset(&g_stack);
}
static void reset_weights_dtcm(void)
{
    a2_w = &a2_weights_dtcm;
    xstack_reset(&g_stack);
}
static void reset_all_dtcm(void)
{
    a2_w = &a2_weights_dtcm;
    arena_use(g_dtcm_arena, DTCM_ARENA_FLOATS);
    xstack_reset(&g_stack);
}

static void run_flash(const float* in, float* out, int n) { xstack_process(&g_stack, layer_flash, in, out, n); }
static void run_itcm(const float* in, float* out, int n) { xstack_process(&g_stack, layer_itcm, in, out, n); }

void experiment_run(void)
{
    bench_title("09 Place data in tightly coupled memory",
                "Assembly 6x3 tile. Code, weights and delay lines in flash/SRAM vs ITCM/DTCM; warm and cold caches.");
    const variant_t v[] = {
        {"QSPI code, SRAM data", reset_sram, run_flash, 0, 0},
        {"QSPI code, SRAM data, cold", reset_sram, run_flash, 0, 1},
        {"ITCM code, DTCM weights", reset_weights_dtcm, run_itcm, 0, 0},
        {"ITCM code, DTCM weights, cold", reset_weights_dtcm, run_itcm, 0, 1},
        {"ITCM code, all data DTCM", reset_all_dtcm, run_itcm, 0, 0},
        {"ITCM code, all data DTCM, cold", reset_all_dtcm, run_itcm, 0, 1},
    };
    bench_run(v, 6);
    a2_w = &a2_weights_dtcm;
}
