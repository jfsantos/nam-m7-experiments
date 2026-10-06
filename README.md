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

## What the cycle model predicts

The ARM builds have also been run in an emulator, with each variant's timed
blocks fed to a Cortex-M7 timing model. The model is built from the same FPU
measurements that experiment 5 makes. It treats every memory access as
zero-wait, so it can't see the effects experiment 9 measures, or cache
misses on the delay lines. **These are predictions, not board measurements:**
run the experiments.

| Experiment | Variant | Model cycles/sample |
|---|---|---:|
| 1 | generic GEMV, run-time shapes | 29,900 |
| | specialized, compile-time shapes | 6,137 |
| 2 | frame-major, `a += w * x` | 5,484 |
| | frame-major, `fmaf` | 6,137 |
| | 6×3 tile, `a += w * x` | 6,899 |
| | 6×3 tile, `fmaf` | 4,681 |
| 3 | frame-major | 6,137 |
| | tap-major | 7,830 |
| 4 | rewind buffer | (spiky; see max/block) |
| | masked ring | 7,830 |
| | mirrored ring | 7,614 |
| 6 | tap-major | 7,614 |
| | tile 1×3 / 3×3 / 6×3 / 8×3 | 6,442 / 5,355 / 5,318 / 5,489 |
| 7 | compiler schedule | 5,318 |
| | pinned load runs | 4,681 |
| | pinned + `-fno-schedule-insns2` | 4,777 |
| 8 | C tile, pinned | 4,681 |
| | assembly tile | 3,950 |
| 10 | block 48: 6-frame tiles | 4,681 |
| | block 16: 6-frame tiles + singles / 4-frame / 6+6+4 | 5,265 / 5,334 / 4,963 |

Two predictions go against the usual advice:

- **Experiment 2.** In the load-bound frame-major kernel, `a += w * x` is
  predicted *faster* than `fmaf`. The split form's VMULs can issue alongside
  loads, while VFMAs block them (experiment 5). In the FPU-bound tile, `fmaf`
  wins clearly.
- **Experiment 3.** Tap-major is predicted *slower* than a frame-major
  baseline that already keeps its sums in registers, because every partial
  sum is stored right after an FMA. Its value is as the step towards
  register tiles (experiment 6).
