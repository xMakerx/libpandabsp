#pragma once

#include "config_bsp.h"
#include <bitMask.h>

extern EXPCL_PANDABSP const BitMask32 wall_mask;
extern EXPCL_PANDABSP const BitMask32 floor_mask;
extern EXPCL_PANDABSP const BitMask32 event_mask;
extern EXPCL_PANDABSP const BitMask32 useable_mask;
extern EXPCL_PANDABSP const BitMask32 camera_mask;
extern EXPCL_PANDABSP const BitMask32 character_mask;

#define WORLD_MASK wall_mask|floor_mask