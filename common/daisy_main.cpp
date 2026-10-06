// Daisy Seed firmware shell shared by every experiment.
//
// Needs only the Daisy and its USB cable. The firmware waits for single-byte
// commands on the USB serial port (tools/daisy.py sends them):
//
//   R  run the experiment and print its report, ending with "EXP_DONE"
//   I  print the experiment id and clock, ending with "EXP_READY"
//   D  reboot into the Daisy bootloader, ready for the next DFU flash
//
// The LED blinks slowly while idle and stays on during a run.

#include "daisy_seed.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C"
{
#include "a2.h"
#include "platform.h"
}

using namespace daisy;

static DaisySeed hw;
static volatile char g_cmd = 0;

static void OnUsbReceive(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
    {
        const char c = (char)buf[i];
        if(c == 'R' || c == 'I' || c == 'D')
            g_cmd = c;
    }
}

extern "C" uint32_t plat_cycles(void)
{
    return DWT->CYCCNT;
}
extern "C" uint32_t plat_clock_hz(void)
{
    return System::GetSysClkFreq();
}
extern "C" void plat_irq_off(void)
{
    __disable_irq();
}
extern "C" void plat_irq_on(void)
{
    __enable_irq();
}
extern "C" void plat_cache_flush(void)
{
    SCB_CleanInvalidateDCache();
    SCB_InvalidateICache();
}
extern "C" void plat_region_begin(void) {}
extern "C" void plat_region_end(void) {}

// libDaisy's logger buffer holds 128 bytes; report lines are longer, so
// format here and send in pieces.
extern "C" void plat_print(const char* fmt, ...)
{
    char    line[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(line, sizeof(line), fmt, va);
    va_end(va);
    const size_t n = strlen(line);
    for(size_t i = 0; i < n; i += 96)
    {
        char chunk[97];
        size_t k = n - i < 96 ? n - i : 96;
        memcpy(chunk, line + i, k);
        chunk[k] = 0;
        hw.Print("%s", chunk);
    }
    hw.PrintLine("");
}

// Kernels marked ITCM_CODE (common/placement.h) are linked to run from ITCM
// and stored in QSPI flash; copy them before anything calls them. The
// symbols come from common/itcm.lds.
extern "C" uint32_t _sitcm_text, _itcm_text_start, _itcm_text_end;

static void CopyCodeToItcm()
{
    const volatile uint32_t* src = &_sitcm_text;
    volatile uint32_t*       dst = &_itcm_text_start;
    while(dst < &_itcm_text_end)
        *dst++ = *src++;
    __DSB();
    __ISB();
}

int main(void)
{
    CopyCodeToItcm();
    hw.Init(true); // 480 MHz

    // Flush-to-zero and default NaN: denormals are slow on the M7 FPU and
    // appear in decaying signals. FPDSCR is the default for new FP contexts.
    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1U << 24) | (1U << 25);
    __set_FPSCR(fpscr);
    *reinterpret_cast<volatile uint32_t*>(0xE000EF3C) |= (1U << 24) | (1U << 25);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    a2_make_weights(&a2_weights_dtcm);

    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(OnUsbReceive, UsbHandle::FS_INTERNAL);

    uint32_t last_blink = System::GetNow();
    bool     led        = false;
    for(;;)
    {
        const char c = g_cmd;
        if(c)
        {
            g_cmd = 0;
            hw.SetLed(true);
            switch(c)
            {
                case 'I':
                    plat_print("EXP_INFO id=%s hz=%lu",
                               experiment_id,
                               (unsigned long)System::GetSysClkFreq());
                    plat_print("EXP_READY");
                    break;
                case 'R':
                    experiment_run();
                    plat_print("EXP_DONE");
                    break;
                case 'D':
                    plat_print("EXP_DFU");
                    System::Delay(100);
                    System::ResetToBootloader(
                        System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
                    break;
            }
        }
        if(System::GetNow() - last_blink > 500)
        {
            last_blink = System::GetNow();
            led        = !led;
            hw.SetLed(led);
        }
    }
}
