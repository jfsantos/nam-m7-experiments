/* 6x3 register tile, loads pinned, and built with -fno-schedule-insns2 (see
 * the Makefile) so the scheduler that runs after register allocation can't
 * move loads back between the FMAs either. */
#define TILE_NAME layer_tile6_pinned_nosched2
#define TILE_T 6
#define TILE_PIN 1
#include "../06-register-tiles/tile.h"
