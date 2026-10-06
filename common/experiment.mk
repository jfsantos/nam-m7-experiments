# Shared build for one experiment. Each experiment's Makefile sets
#
#   TARGET       firmware name (the folder name)
#   EXP_SOURCES  the experiment's C files
#
# and includes this file. Targets:
#
#   make            build the Daisy Seed firmware (build/$(TARGET).bin)
#   make run        build, flash over USB DFU, run, print the report
#   make monitor    run the firmware already on the board, print the report
#   make host       build and run the portable variants on this computer
#   make clean

REPO_DIR     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
COMMON_DIR   := $(REPO_DIR)/common
LIBDAISY_DIR ?= $(REPO_DIR)/DaisyExamples/libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# An arm-none-eabi toolchain with newlib. DaisyToolchain is used when
# installed (Homebrew's arm-none-eabi-gcc has no newlib); otherwise PATH.
GCC_PATH ?= $(patsubst %/arm-none-eabi-gcc,%,$(firstword $(wildcard /Library/DaisyToolchain/*/arm/bin/arm-none-eabi-gcc)))
ifeq ($(GCC_PATH),)
undefine GCC_PATH
endif

PYTHON ?= python3

# Flags for every C file: the ones the pedal firmware's DSP code uses.
KERNEL_CFLAGS ?= -ffast-math -fno-unroll-loops

C_SOURCES   = $(EXP_SOURCES) $(COMMON_DIR)/bench.c $(COMMON_DIR)/a2.c $(COMMON_DIR)/rings.c
CPP_SOURCES = $(COMMON_DIR)/daisy_main.cpp
C_INCLUDES  = -I. -I$(COMMON_DIR)

# The Daisy bootloader stays in internal flash and the app goes to QSPI, so
# reflashing never needs the BOOT/RESET buttons once an experiment is running.
APP_TYPE = BOOT_QSPI
CPP_STANDARD = -std=gnu++17
OPT = -O2

# libDaisy's QSPI linker script with the ITCM section inserted before .text.
LDSCRIPT = $(BUILD_DIR)/experiment.lds

include $(SYSTEM_FILES_DIR)/Makefile

CFLAGS += $(KERNEL_CFLAGS) $(EXP_CFLAGS) '-DEXPERIMENT_ID="$(TARGET)"'

$(BUILD_DIR)/experiment.lds: $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds $(COMMON_DIR)/itcm.lds | $(BUILD_DIR)
	awk 'FNR == NR { blk = blk $$0 "\n"; next } \
	     !done && /^[ \t]*\.text[ \t]*:/ { printf "%s", blk; done = 1 } \
	     { print } \
	     END { if (!done) { print "no .text section found" > "/dev/stderr"; exit 1 } }' \
	    $(COMMON_DIR)/itcm.lds $< > $@

$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)

$(LIBDAISY_DIR)/build/libdaisy.a:
	@echo "libDaisy is not built: run 'make libdaisy' in $(REPO_DIR)" && exit 1
$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_DIR)/build/libdaisy.a

run: all
	$(PYTHON) $(REPO_DIR)/tools/daisy.py flash-run $(BUILD_DIR)/$(TARGET).bin --id $(TARGET)

monitor:
	$(PYTHON) $(REPO_DIR)/tools/daisy.py run --id $(TARGET)

# Host build: portable variants only (inline-asm variants are compiled out).
HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -ffast-math -Wall
host: $(BUILD_DIR)/host/$(TARGET)
	$(BUILD_DIR)/host/$(TARGET)

$(BUILD_DIR)/host/$(TARGET): $(EXP_SOURCES) $(COMMON_DIR)/*.c $(COMMON_DIR)/*.h $(wildcard *.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $(EXP_HOST_CFLAGS) -I. -I$(COMMON_DIR) \
	    '-DEXPERIMENT_ID="$(TARGET)"' $(EXP_SOURCES) \
	    $(COMMON_DIR)/bench.c $(COMMON_DIR)/a2.c $(COMMON_DIR)/rings.c $(COMMON_DIR)/host_main.c -lm -o $@

.PHONY: run monitor host
