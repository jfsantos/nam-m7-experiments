/*
 * Method 10: tune for the block size.
 *
 * The tile covers T frames; a block of nf frames runs nf / T tiles and then
 * nf % T frames one at a time, at 0.75 FMAs per load instead of 2. So the
 * best T depends on the block size the product calls the kernel with:
 *
 *   6-frame tile:  block 48 = 8 tiles;         block 16 = 2 tiles + 4 single frames
 *   4-frame tile:  block 48 = 12 tiles;        block 16 = 4 tiles
 *
 * The 4-frame tile does less work per load (1.7 vs 2.0 FMAs per load), but
 * leaves no frames over at 16. A plan tuned for 16 can also mix them: two
 * 6-frame tiles and one 4-frame tile. All use pinned load runs (experiment 7).
 */
#include "bench.h"
#include "placement.h"
#include "rings.h"

#define TILE_NAME layer_tile6
#define TILE_T 6
#define TILE_PIN 1
#include "../06-register-tiles/tile.h"
#define TILE_NAME layer_tile4
#define TILE_T 4
#define TILE_PIN 1
#include "../06-register-tiles/tile.h"

/* Plan for 16-frame blocks: 6-frame tiles, then a 4-frame tile, then singles. */
ITCM_CODE static void layer_mixed(const a2_layer_t* L, int K, const float* const* tap, xring_t* next, float* out, int nf)
{
    int f = 0;
    for (; f + 6 <= nf; f += 6)
        layer_tile6_tile(L, K, tap, next, out, f);
    for (; f + 4 <= nf; f += 4)
        layer_tile4_tile(L, K, tap, next, out, f);
    for (; f < nf; f++)
        layer_tile4_one(L, K, tap, next, out, f);
}

static xstack_t g_stack;

static void reset(void) { xstack_reset(&g_stack); }
static void run_tile6(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile6, in, out, n); }
static void run_tile4(const float* in, float* out, int n) { xstack_process(&g_stack, layer_tile4, in, out, n); }
static void run_mixed(const float* in, float* out, int n) { xstack_process(&g_stack, layer_mixed, in, out, n); }

void experiment_run(void)
{
    bench_title("10 Tune for the block size",
                "Pinned C tiles, mirrored delay lines. 6-frame vs 4-frame tile, called with 48- and 16-frame blocks.");
    const variant_t v[] = {
        {"block 48: 6-frame tiles", reset, run_tile6, 48, 0},
        {"block 48: 4-frame tiles", reset, run_tile4, 48, 0},
        {"block 16: 6-frame tiles + 4 singles", reset, run_tile6, 16, 0},
        {"block 16: 4-frame tiles", reset, run_tile4, 16, 0},
        {"block 16: 6 + 6 + 4-frame tiles", reset, run_mixed, 16, 0},
    };
    bench_run(v, 5);
}
