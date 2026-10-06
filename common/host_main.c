/* Host build: runs an experiment's portable variants to check that they
 * agree. Timings are wall-clock nanoseconds and say nothing about the M7. */
#define _POSIX_C_SOURCE 199309L
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "a2.h"
#include "platform.h"

uint32_t plat_cycles(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}
uint32_t plat_clock_hz(void) { return 0; }
void plat_irq_off(void) {}
void plat_irq_on(void) {}
void plat_cache_flush(void) {}
void plat_region_begin(void) {}
void plat_region_end(void) {}

void plat_print(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    putchar('\n');
}

int main(void)
{
    a2_make_weights(&a2_weights_dtcm);
    experiment_run();
    return 0;
}
