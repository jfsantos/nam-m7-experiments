# Top level: build libDaisy once, then work in each experiment folder.
#
#   make libdaisy        build DaisyExamples/libDaisy (needed once)
#   make                 build every experiment's firmware
#   make host            run every experiment's portable variants on this computer
#   make run-all         flash and run every experiment in turn (board on USB)
#   make clean

EXPERIMENTS := $(sort $(dir $(wildcard [0-9][0-9]-*/Makefile)))
GCC_PATH ?= $(patsubst %/arm-none-eabi-gcc,%,$(firstword $(wildcard /Library/DaisyToolchain/*/arm/bin/arm-none-eabi-gcc)))

all:
	@set -e; for e in $(EXPERIMENTS); do $(MAKE) -C $$e; done

libdaisy:
	$(MAKE) -C DaisyExamples/libDaisy $(if $(GCC_PATH),GCC_PATH=$(GCC_PATH))

host:
	@set -e; for e in $(EXPERIMENTS); do $(MAKE) -s -C $$e host; done

run-all:
	@set -e; for e in $(EXPERIMENTS); do $(MAKE) -C $$e run; done

clean:
	@for e in $(EXPERIMENTS); do $(MAKE) -C $$e clean; done

.PHONY: all libdaisy host run-all clean
