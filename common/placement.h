/*
 * Memory placement on the STM32H750 (Daisy Seed). See experiment 9.
 *
 *   ITCM_CODE   function runs from zero-wait ITCM (copied there at boot)
 *   DTCM_DATA   zero-initialized data in zero-wait DTCM
 *   (default)   code executes in place from QSPI flash behind the I-cache;
 *               .bss lives in AXI SRAM behind the D-cache
 *
 * Experiments 1-8 and 10 put their kernels in ITCM and the weights in DTCM,
 * and leave the delay lines in SRAM, so that only the method under test
 * changes. On other targets (host builds) the macros are empty.
 */
#ifndef PLACEMENT_H
#define PLACEMENT_H

#if defined(__arm__) && !defined(PLACEMENT_NONE)
#define ITCM_CODE __attribute__((section(".itcm_text"), noinline))
#define DTCM_DATA __attribute__((section(".dtcmram_bss")))
#else
#define ITCM_CODE __attribute__((noinline))
#define DTCM_DATA
#endif

/* Kernels that must stay in QSPI flash (experiment 9). */
#define FLASH_CODE __attribute__((noinline))

#endif
