/*
 * Method 5: measure the FMA pipeline.
 *
 * Microbenchmarks of instruction pairings on the Cortex-M7 FPU. Each probe
 * repeats a short instruction group in a loop running from ITCM, on data in
 * DTCM, with interrupts off, and reports cycles per unit (one instruction,
 * one pair, or one float loaded) after subtracting the loop overhead.
 *
 * The rows to compare:
 *   - independent VFMAs issue one per cycle; a dependent chain waits 3
 *   - a VLDR right after a VFMA waits for it: an FMA+load pair costs ~4
 *     cycles, while VMUL+load and FMA+integer-load pairs dual-issue
 *   - stores behave like loads; core->FP moves use the same path
 *   - a run of loads costs ~1 cycle each, less with 64-bit VLDR
 *
 * These rules drive experiments 6-8. Cortex-M7 only: the host build skips it.
 */
#include <stdint.h>

#include "bench.h"
#include "placement.h"

#if defined(__ARM_ARCH_7EM__) && defined(__ARM_FP)

#define ITERS 1000
#define REPS 8 /* groups per loop iteration */

DTCM_DATA static float g_data[16] __attribute__((aligned(8)));

#define CLOBBERS                                                                                                       \
    "r2", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15", "memory", \
        "cc"

/* Times ITERS iterations of REPS copies of BODY. */
#define PROBE(fn, body)                                                                                                \
    ITCM_CODE static uint32_t fn(float* p)                                                                             \
    {                                                                                                                  \
        int n = ITERS;                                                                                                 \
        plat_irq_off();                                                                                                \
        const uint32_t t0 = plat_cycles();                                                                             \
        __asm__ volatile("mov r2, #0\n\t"                                                                              \
                         "1:\n\t"                                                                                      \
                         ".rept " "8" "\n\t" body ".endr\n\t"                                                          \
                         "subs %[n], %[n], #1\n\t"                                                                     \
                         "bne 1b\n\t"                                                                                  \
                         : [n] "+r"(n)                                                                                 \
                         : [p] "r"(p)                                                                                  \
                         : CLOBBERS);                                                                                  \
        const uint32_t t1 = plat_cycles();                                                                             \
        plat_irq_on();                                                                                                 \
        return t1 - t0;                                                                                                \
    }

PROBE(probe_empty, "")
PROBE(probe_fma_indep, "vfma.f32 s0, s8, s9\n\t vfma.f32 s1, s8, s9\n\t vfma.f32 s2, s8, s9\n\t"
                       "vfma.f32 s3, s8, s9\n\t vfma.f32 s4, s8, s9\n\t vfma.f32 s5, s8, s9\n\t")
PROBE(probe_fma_chain, "vfma.f32 s0, s8, s9\n\t vfma.f32 s0, s8, s9\n\t vfma.f32 s0, s8, s9\n\t")
PROBE(probe_fma_ldr, "vfma.f32 s0, s8, s9\n\t vldr s10, [%[p]]\n\t vfma.f32 s1, s8, s9\n\t vldr s11, [%[p], #4]\n\t"
                     "vfma.f32 s2, s8, s9\n\t vldr s12, [%[p], #8]\n\t")
PROBE(probe_mul_ldr, "vmul.f32 s0, s8, s9\n\t vldr s10, [%[p]]\n\t vmul.f32 s1, s8, s9\n\t vldr s11, [%[p], #4]\n\t"
                     "vmul.f32 s2, s8, s9\n\t vldr s12, [%[p], #8]\n\t")
PROBE(probe_fma_intldr, "vfma.f32 s0, s8, s9\n\t ldr r2, [%[p]]\n\t vfma.f32 s1, s8, s9\n\t ldr r2, [%[p], #4]\n\t"
                        "vfma.f32 s2, s8, s9\n\t ldr r2, [%[p], #8]\n\t")
PROBE(probe_fma_str, "vfma.f32 s0, s8, s9\n\t vstr s10, [%[p]]\n\t vfma.f32 s1, s8, s9\n\t vstr s11, [%[p], #4]\n\t"
                     "vfma.f32 s2, s8, s9\n\t vstr s12, [%[p], #8]\n\t")
PROBE(probe_max_str, "vmaxnm.f32 s0, s8, s9\n\t vstr s10, [%[p]]\n\t vmaxnm.f32 s1, s8, s9\n\t vstr s11, [%[p], #4]\n\t"
                     "vmaxnm.f32 s2, s8, s9\n\t vstr s12, [%[p], #8]\n\t")
PROBE(probe_fma_vmov, "vfma.f32 s0, s8, s9\n\t vmov s10, r2\n\t vfma.f32 s1, s8, s9\n\t vmov s11, r2\n\t"
                      "vfma.f32 s2, s8, s9\n\t vmov s12, r2\n\t")
PROBE(probe_ldr_run, "vldr s0, [%[p]]\n\t vldr s1, [%[p], #4]\n\t vldr s2, [%[p], #8]\n\t vldr s3, [%[p], #12]\n\t"
                     "vldr s4, [%[p], #16]\n\t vldr s5, [%[p], #20]\n\t vldr s6, [%[p], #24]\n\t vldr s7, [%[p], #28]\n\t")
PROBE(probe_ldr64_run, "vldr d0, [%[p]]\n\t vldr d1, [%[p], #8]\n\t vldr d2, [%[p], #16]\n\t vldr d3, [%[p], #24]\n\t")
PROBE(probe_vldm, "vldmia %[p], {s0-s7}\n\t")

typedef struct {
    const char* name;
    uint32_t (*fn)(float*);
    int units; /* per group */
    const char* unit;
    const char* expect;
} probe_t;

static const probe_t probes[] = {
    {"VFMA, 6 independent accumulators", probe_fma_indep, 6, "per VFMA", "~1"},
    {"VFMA, dependent chain", probe_fma_chain, 3, "per VFMA", "~3 (latency)"},
    {"VFMA + VLDR, interleaved", probe_fma_ldr, 3, "per pair", "~4: load waits for the FMA"},
    {"VMUL + VLDR, interleaved", probe_mul_ldr, 3, "per pair", "~1.1: dual-issue"},
    {"VFMA + integer LDR, interleaved", probe_fma_intldr, 3, "per pair", "~1.1: dual-issue"},
    {"VFMA + VSTR, interleaved", probe_fma_str, 3, "per pair", "~4: store waits too"},
    {"VMAXNM + VSTR, interleaved", probe_max_str, 3, "per pair", "~1.1"},
    {"VFMA + VMOV core->FP, interleaved", probe_fma_vmov, 3, "per pair", "~3.9: same path"},
    {"run of VLDR", probe_ldr_run, 8, "per float", "0.8-1.0"},
    {"run of VLDR.64", probe_ldr64_run, 8, "per float", "~0.7"},
    {"VLDM of 8 registers", probe_vldm, 8, "per float", "~2.1"},
};

void experiment_run(void)
{
    bench_title("05 Measure the FMA pipeline",
                "Cortex-M7 FPU pairings: code in ITCM, data in DTCM, interrupts off, loop overhead subtracted.");
    float* p = g_data;
    probe_empty(p); /* warm up */
    uint32_t base = probe_empty(p);
    for (int i = 0; i < 3; i++) { /* take the fastest of a few runs */
        uint32_t t = probe_empty(p);
        if (t < base)
            base = t;
    }
    plat_print("   %-36s %10s  %-10s %s", "pattern", "cycles", "", "measured before on an STM32H750");
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        const probe_t* pr = &probes[i];
        pr->fn(p);
        uint32_t best = pr->fn(p);
        for (int r = 0; r < 3; r++) {
            uint32_t t = pr->fn(p);
            if (t < best)
                best = t;
        }
        const uint32_t net = best > base ? best - base : 0;
        const uint32_t units = (uint32_t)ITERS * REPS * (uint32_t)pr->units;
        const uint32_t c100 = (net * 100u + units / 2) / units;
        plat_print("   %-36s %7lu.%02lu  %-10s %s", pr->name, (unsigned long)(c100 / 100), (unsigned long)(c100 % 100),
                   pr->unit, pr->expect);
    }
    plat_print("   (%d iterations x %d groups per probe, best of 4 runs)", ITERS, REPS);
}

#else

void experiment_run(void)
{
    bench_title("05 Measure the FMA pipeline", "Cortex-M7 assembly probes: nothing to run on this host.");
}

#endif
