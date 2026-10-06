# Method 7: keep loads in runs. See experiment.c.
TARGET = 07-load-runs
EXP_SOURCES = experiment.c tile6_compiler.c tile6_pinned.c tile6_pinned_nosched2.c
include ../common/experiment.mk

# Turn off the post-register-allocation scheduler for this file only.
$(BUILD_DIR)/tile6_pinned_nosched2.o: CFLAGS += -fno-schedule-insns2
