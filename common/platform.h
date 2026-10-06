/* Hooks each platform (Daisy Seed, host) provides to the benchmark runner. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t plat_cycles(void);     /* free-running cycle counter (DWT->CYCCNT) */
uint32_t plat_clock_hz(void);   /* core clock; 0 if plat_cycles() is not in cycles */
void plat_irq_off(void);        /* wrapped around each timed call */
void plat_irq_on(void);
void plat_cache_flush(void);    /* clean+invalidate D-cache, invalidate I-cache */
void plat_print(const char* fmt, ...) __attribute__((format(printf, 1, 2))); /* one line */

/* Called around each variant's timed blocks. No-ops on the board; a
 * simulator can use them to trace only the timed code. */
void plat_region_begin(void);
void plat_region_end(void);

/* experiment_id is the folder name (set by common/experiment.mk);
 * experiment_run() is defined by each experiment. */
extern const char experiment_id[];
void experiment_run(void);

#ifdef __cplusplus
}
#endif

#endif
