/*
 * Benchmark runner shared by the experiments.
 *
 * Each variant processes the same input: BENCH_PREWARM samples untimed (to
 * fill the delay lines), then BENCH_SAMPLES samples in blocks, each block
 * timed with interrupts off. The first variant is the reference: every other
 * variant's output is compared with it sample by sample.
 *
 * Report columns:
 *   cyc/sample  mean cycles per sample over the timed blocks
 *   load        share of the 48 kHz real-time budget at the current clock
 *   speedup     cyc/sample of the first variant / this variant
 *   p50, max    cycles per block (max shows spikes: rewinds, cache misses)
 *   check       max |output - reference|, OK if <= 1e-4 of the output peak
 */
#ifndef BENCH_H
#define BENCH_H

#include "platform.h"

#ifndef BENCH_BLOCK
#define BENCH_BLOCK 48
#endif
#ifndef BENCH_SAMPLES
#define BENCH_SAMPLES 4800 /* 0.1 s of audio; a multiple of 48 and 16 */
#endif
#ifndef BENCH_PREWARM
#define BENCH_PREWARM 6400 /* > the stack's receptive field (6,346) */
#endif

typedef struct {
    const char* name;
    void (*reset)(void);                                 /* allocate + clear state */
    void (*process)(const float* in, float* out, int n); /* n <= A2_BLOCK_MAX */
    int block;                                           /* frames per call; 0 = BENCH_BLOCK */
    int cold;                                            /* flush caches before every block */
} variant_t;

/* Prints the experiment's title and a one-line description. */
void bench_title(const char* title, const char* what);
void bench_run(const variant_t* v, int nv);
#define bench_note plat_print

#endif
