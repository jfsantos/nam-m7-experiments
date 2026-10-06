#include "bench.h"

#include <stdlib.h>
#include <string.h>

#include "a2.h"
#include "rings.h"

#ifndef EXPERIMENT_ID
#define EXPERIMENT_ID "experiment"
#endif
const char experiment_id[] = EXPERIMENT_ID;

static float g_ref[BENCH_SAMPLES];
static float g_out[BENCH_SAMPLES];
static uint32_t g_cyc[BENCH_SAMPLES]; /* per block; at most BENCH_SAMPLES blocks */

/* Integer-only formatting: newlib-nano's printf has no float support. */
static const char* group(char* buf, unsigned long v)
{
    char tmp[24];
    int n = 0, d = 0;
    do {
        if (d && d % 3 == 0)
            tmp[n++] = ',';
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
        d++;
    } while (v);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

/* |x| as "m.me-NN". */
static const char* sci(char* buf, float x)
{
    if (x < 0.0f)
        x = -x;
    if (x == 0.0f) {
        strcpy(buf, "0");
        return buf;
    }
    if (!(x < 1e30f)) {
        strcpy(buf, "inf/nan");
        return buf;
    }
    int e = 0;
    while (x >= 10.0f) {
        x *= 0.1f;
        e++;
    }
    while (x < 1.0f) {
        x *= 10.0f;
        e--;
    }
    int m = (int)(x * 10.0f + 0.5f);
    if (m >= 100) {
        m = 10;
        e++;
    }
    char* p = buf;
    *p++ = (char)('0' + m / 10);
    *p++ = '.';
    *p++ = (char)('0' + m % 10);
    *p++ = 'e';
    *p++ = e < 0 ? '-' : '+';
    if (e < 0)
        e = -e;
    *p++ = (char)('0' + e / 10);
    *p++ = (char)('0' + e % 10);
    *p = 0;
    return buf;
}

static int cmp_u32(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

void bench_title(const char* title, const char* what)
{
    const uint32_t hz = plat_clock_hz();
    plat_print("%s", "");
    plat_print("== %s", title);
    plat_print("   %s", what);
    if (hz)
        plat_print("   A2-Lite-shaped stack: %d layers, 3 channels, %d MACs/sample. Clock %lu MHz: the 48 kHz budget "
                   "is %lu cycles/sample.",
                   A2_LAYERS, a2_macs_per_sample(), (unsigned long)(hz / 1000000u), (unsigned long)(hz / 48000u));
    else
        plat_print("   A2-Lite-shaped stack: %d layers, 3 channels, %d MACs/sample. Host build: times are ns, "
                   "not cycles.",
                   A2_LAYERS, a2_macs_per_sample());
}

static void run_one(const variant_t* v, float* out, unsigned long* mean, uint32_t* p50, uint32_t* pmax)
{
    const int B = v->block ? v->block : BENCH_BLOCK;
    const int nblocks = BENCH_SAMPLES / B;
    float in[A2_BLOCK_MAX], scratch[A2_BLOCK_MAX];

    arena_use_default();
    v->reset();

    for (long s = 0; s < BENCH_PREWARM; s += B) { /* exactly BENCH_PREWARM samples, for every block size */
        const int n = BENCH_PREWARM - s < B ? (int)(BENCH_PREWARM - s) : B;
        for (int i = 0; i < n; i++)
            in[i] = a2_input(s + i);
        v->process(in, scratch, n);
    }

    uint64_t total = 0;
    plat_region_begin();
    for (int b = 0; b < nblocks; b++) {
        const long s0 = BENCH_PREWARM + (long)b * B;
        for (int i = 0; i < B; i++)
            in[i] = a2_input(s0 + i);
        if (v->cold)
            plat_cache_flush();
        plat_irq_off();
        const uint32_t t0 = plat_cycles();
        v->process(in, out + (long)b * B, B);
        const uint32_t t1 = plat_cycles();
        plat_irq_on();
        g_cyc[b] = t1 - t0;
        total += g_cyc[b];
    }
    plat_region_end();

    *mean = (unsigned long)(total / (uint64_t)(nblocks * B));
    qsort(g_cyc, (size_t)nblocks, sizeof(g_cyc[0]), cmp_u32);
    *p50 = g_cyc[nblocks / 2];
    *pmax = g_cyc[nblocks - 1];
}

void bench_run(const variant_t* v, int nv)
{
    const uint32_t hz = plat_clock_hz();
    const unsigned long budget = hz ? hz / 48000u : 0;
    unsigned long base = 0;
    float peak = 0.0f;
    char a[24], b[24], c[24], d[24];

    plat_print("   %-36s %10s %7s %8s %6s %10s %10s  %s", "variant", hz ? "cyc/sample" : "ns/sample", "load",
               "speedup", "block", "p50/block", "max/block", "check vs first");
    for (int i = 0; i < nv; i++) {
        unsigned long mean;
        uint32_t p50, pmax;
        float* out = i == 0 ? g_ref : g_out;
        run_one(&v[i], out, &mean, &p50, &pmax);

        if (i == 0) {
            base = mean;
            for (int s = 0; s < BENCH_SAMPLES; s++)
                if (g_ref[s] > peak || -g_ref[s] > peak)
                    peak = g_ref[s] < 0.0f ? -g_ref[s] : g_ref[s];
        }
        char check[48];
        int ok = 1;
        if (i == 0) {
            strcpy(check, "reference");
            for (int s = 0; s < BENCH_SAMPLES; s++)
                if (!(g_ref[s] < 1e30f && g_ref[s] > -1e30f))
                    ok = 0;
            if (!ok)
                strcpy(check, "FAIL: reference not finite");
        } else {
            float err = 0.0f;
            for (int s = 0; s < BENCH_SAMPLES; s++) {
                float e = g_out[s] - g_ref[s];
                e = e < 0.0f ? -e : e;
                if (!(e <= err)) /* also catches NaN */
                    err = e;
            }
            ok = err <= 1e-4f * peak;
            sci(c, err);
            strcpy(check, ok ? "OK, max err " : "FAIL, max err ");
            strcat(check, c);
        }

        char load[16] = "-";
        if (budget) {
            const unsigned long pm = mean * 1000u / budget; /* per mille */
            int n = 0;
            char t[16];
            group(t, pm / 10);
            for (char* q = t; *q; q++)
                load[n++] = *q;
            load[n++] = '.';
            load[n++] = (char)('0' + pm % 10);
            load[n++] = '%';
            load[n] = 0;
        }
        const unsigned long sp = mean ? (base * 100u + mean / 2) / mean : 0;
        const int B = v[i].block ? v[i].block : BENCH_BLOCK;
        plat_print("   %-36s %10s %7s %5lu.%02lux %6d %10s %10s  %s", v[i].name, group(a, mean), load, sp / 100,
                   sp % 100, B, group(b, p50), group(d, pmax), check);
    }
    plat_print("   (%d timed samples per variant after %d untimed; reference output peak %s)", BENCH_SAMPLES,
               BENCH_PREWARM, sci(c, peak));
}
