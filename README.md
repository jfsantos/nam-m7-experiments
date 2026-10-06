# Optimizing a neural amp model on Cortex-M7: one experiment per method

Ten small firmware experiments for the Daisy Seed (STM32H750, Cortex-M7 at
480 MHz). Each one isolates one optimization method and benchmarks two or
more implementations of the same workload on the board, then prints a table
over the USB serial port.

The workload is an **A2-Lite-shaped WaveNet stack**: 23 layers, 3 channels,
dilated convolutions with kernel sizes 6 and 15 and dilations up to 239,
then a LeakyReLU, a 1×1 and a residual. That's about 1,614 multiply-adds
per sample. Weights are synthetic and deterministic; speed doesn't depend on
their values. See `common/a2.h`.

| # | Folder | Method | Compares |
|---:|---|---|---|
| 1 | `01-specialize` | Specialize the code to the model | A library-style GEMV with run-time shapes vs straight-line code with compile-time shapes |
| 2 | `02-fma` | Get the FMA you asked for | `a += w * x` vs `fmaf(w, x, a)` under `-ffast-math`, in a load-bound kernel and in an FPU-bound one |
| 3 | `03-loop-order` | Invert the loop order | Frames outside taps vs taps outside frames |
| 4 | `04-delay-lines` | Delay lines without wrap logic | Rewind buffer vs masked power-of-two ring vs mirrored exact-size ring |
| 5 | `05-fma-pipeline` | Measure the FMA pipeline | Assembly probes of FPU instruction pairings (VFMA, VLDR, VSTR, VMOV …) |
| 6 | `06-register-tiles` | Keep the running sums in registers | Tap-major vs register tiles of 1, 3, 6 and 8 frames × 3 outputs |
| 7 | `07-load-runs` | Keep loads in runs | The compiler's load schedule vs load runs pinned with an empty `asm` |
| 8 | `08-asm-tile` | Write the inner tile in assembly | The pinned C tile vs a hand-allocated assembly tile |
| 9 | `09-tcm-placement` | Place data in tightly coupled memory | Code in QSPI flash vs ITCM; weights and delay lines in SRAM vs DTCM; warm vs cold caches |
| 10 | `10-block-size` | Tune for the block size | 6- vs 4-frame tiles, and a mixed plan, at 48- and 16-frame blocks |

Each experiment's `experiment.c` starts with a comment explaining what it
compares and what to look for. Kernels live in the folder of the method that
introduces them, and later experiments include them from there:
- the frame-major kernel is in `01-specialize/frame_major.h`;
- the tap-major kernel is in `03-loop-order/tap_major.h`;
- the C register tile is in `06-register-tiles/tile.h`;
- the assembly tile is in `08-asm-tile/asm_tile.h`.

## Setup

You need:

- **An arm-none-eabi toolchain that includes newlib.**
  [DaisyToolchain](https://github.com/electro-smith/DaisyToolchain) or the Arm
  GNU Toolchain both work. The Makefiles use `/Library/DaisyToolchain/*/arm/bin`
  when it exists; otherwise they use `PATH`, or `GCC_PATH=...`. Homebrew's
  `arm-none-eabi-gcc` has no newlib and won't link.
- **`dfu-util`** and **pyserial** (`pip install pyserial`).
- **The Daisy bootloader on the board.** The experiments are `BOOT_QSPI` apps.
  If the board already runs a `BOOT_QSPI` firmware, the bootloader is there.
  Otherwise, hold BOOT, press RESET, and run `make program-boot` in any
  experiment folder once.

Then:

```sh
git submodule update --init DaisyExamples
git -C DaisyExamples submodule update --init libDaisy
make libdaisy        # once
```

## Running an experiment

With the Daisy on USB:

```sh
cd 07-load-runs
make run
```

`make run` builds the firmware, flashes it over DFU, and prints the report.
To get the board into DFU mode, `tools/daisy.py` sends `D` to the running
firmware, which reboots into the bootloader. If nothing answers, it asks you
to press RESET, then BOOT while the LED pulses.

Other targets:

| Target | What it does |
|---|---|
| `make` | Build `build/<experiment>.bin` only |
| `make monitor` | Run the firmware already on the board again |
| `make host` | Build and run the portable variants on this computer, to check they agree (assembly variants are skipped; times are host nanoseconds) |
| `make run-all` (top level) | Flash and run every experiment in turn |

Sample report (the layout; numbers depend on the board):

```
== 07 Keep loads in runs
   6x3 register tile, mirrored delay lines. Compiler's load schedule vs load runs pinned before the FMAs.
   A2-Lite-shaped stack: 23 layers, 3 channels, 1614 MACs/sample. Clock 480 MHz: the 48 kHz budget is 10000 cycles/sample.
   variant                               cyc/sample    load  speedup  block  p50/block  max/block  check vs first
   compiler schedule                          ...
   pinned load runs                           ...                                                   OK, max err 0
```

- **cyc/sample:** the mean over 100 timed blocks of 48 frames, each timed
  with the DWT cycle counter and interrupts off, after 6,400 untimed samples
  that fill the delay lines.
- **load:** the share of the 48 kHz real-time budget.
- **max/block:** shows spikes, such as rewinds and cache misses.
- **check:** compares every output sample with the first variant's.

## Fixed setup

Every experiment except 9 runs the same setup, so that only the method under
test changes (`common/placement.h`):

- **Code:** kernels in ITCM.
- **Weights:** in DTCM.
- **Delay lines:** in AXI SRAM behind the D-cache.
- **Clock:** 480 MHz, with flush-to-zero on.
- **Flags:** `-O2 -ffast-math -fno-unroll-loops`.

The shared pieces are in `common/`:
- `a2.*`: the workload;
- `rings.*`: delay lines and stack drivers;
- `bench.*`: timing, the report, and output checks;
- `daisy_main.cpp`: the firmware shell, including the USB serial commands
  `R` (run), `I` (info) and `D` (reboot to bootloader);
- `experiment.mk`: the shared build.

## Results on a Daisy Seed

Measured on 2026-10-06 at 480 MHz, with `make run-all`. The full reports are
in `results/2026-10-06-daisy-seed.txt`. Every variant's output matched its
reference.

The model column comes from running the same ARM builds in an emulator and
timing them with a Cortex-M7 model. That model is built from FPU
measurements like experiment 5's, and it treats every memory access as
zero-wait, so it can't see experiment 9.

| # | Variant | Board cycles/sample | Model | vs first variant (board) |
|---:|---|---:|---:|---:|
| 1 | generic GEMV, run-time shapes | 28,236 | 29,900 | |
| | specialized, compile-time shapes | 5,649 | 6,137 | 5.00x |
| 2 | frame-major, `a += w * x` | 4,994 | 5,484 | |
| | frame-major, `fmaf` | 5,965 | 6,137 | 0.84x |
| | 6×3 tile, `a += w * x` | 6,296 | 6,899 | 0.79x |
| | 6×3 tile, `fmaf` | 4,680 | 4,681 | 1.07x |
| 3 | frame-major | 5,645 | 6,137 | |
| | tap-major | 7,259 | 7,830 | 0.78x |
| 4 | rewind buffer | 8,425 | – | |
| | masked ring | 7,263 | 7,830 | 1.16x |
| | mirrored ring | 7,081 | 7,614 | 1.19x |
| 6 | tap-major | 7,066 | 7,614 | |
| | tile 1×3 / 3×3 / 6×3 / 8×3 | 5,958 / 5,489 / 4,875 / 5,173 | 6,442 / 5,355 / 5,318 / 5,489 | 1.19 / 1.29 / 1.45 / 1.37x |
| 7 | compiler schedule | 4,914 | 5,318 | |
| | pinned load runs | 4,689 | 4,681 | 1.05x |
| | pinned + `-fno-schedule-insns2` | 5,037 | 4,777 | 0.98x |
| 8 | C tile, pinned | 4,680 | 4,681 | |
| | assembly tile | 4,045 | 3,950 | 1.16x |
| 9 | QSPI code, SRAM data (warm / cold) | 4,162 / 4,421 | – | |
| | ITCM code, DTCM weights (warm / cold) | 4,059 / 4,180 | – | 1.03x / 1.00x |
| | ITCM code, all data in DTCM (warm / cold) | 3,675 / 3,846 | – | 1.13x / 1.08x |
| 10 | block 48: 6-frame / 4-frame tiles | 4,674 / 5,586 | 4,681 / 5,199 | 1.00 / 0.84x |
| | block 16: 6-frame + singles / 4-frame / 6+6+4 | 5,280 / 5,859 / 5,219 | 5,265 / 5,334 / 4,963 | 0.89 / 0.80 / 0.90x |

Experiment 5, cycles per unit on the board:

| Pattern | Cycles |
|---|---:|
| VFMA, independent / dependent chain | 0.98 / 2.96 per VFMA |
| VFMA + VLDR, VFMA + VSTR, VFMA + VMOV core→FP | 3.87 per pair |
| VMUL + VLDR, VFMA + integer LDR, VMAXNM + VSTR | 1.00 per pair |
| VLDR / VLDR.64 / VLDM runs (from DTCM) | 0.52 / 0.50 / 0.50 per float |

What stands out:

- **Experiment 2: `fmaf` isn't always faster.** In the load-bound
  frame-major kernel, `a += w * x` is 16% faster. Its VMULs dual-issue with
  loads, while VFMAs block them (experiment 5). In the FPU-bound tile,
  `fmaf` is 26% faster.
- **Experiment 3: tap-major alone is 29% slower** than a frame-major kernel
  that keeps its sums in registers, because every partial sum is stored right
  after an FMA. It's the step towards register tiles (experiment 6), not a
  win by itself.
- **Experiment 5: runs of loads from DTCM are cheaper than the model
  assumes.** They cost 0.5 cycles per float for VLDR, VLDR.64 and VLDM alike;
  the model uses 0.7–2.1. That is probably why pinning the load runs gains
  5% here, not the 12% the model predicts.
- **Experiment 7: `-fno-schedule-insns2` makes it slower** (+7%).
- **Experiment 10: 16-frame blocks cost 13% more per sample.** Retuning the
  tile plan for them recovers only 1% in this kernel; the model predicted 6%.
- **Cumulative path on the board:**

  | Step | Experiment | Cycles/sample |
  |---|---:|---:|
  | Generic | 1 | 28,236 |
  | Specialized | 1 | 5,649 |
  | 6×3 tile | 6 | 4,875 |
  | Pinned load runs | 7 | 4,689 |
  | Assembly tile | 8 | 4,045 |
  | Delay lines in DTCM | 9 | 3,675 |

  The last step is 2.3x the floor of 1,614 multiply-adds per sample.
