#pragma once

#include <os_types.h>

extern uint32_t Pal2RGB (uint16_t paletteEntry, bool amigaHack);
extern uint16_t RGB2Pal (uint32_t color);

extern uint32_t ZXSpectrumPalette[16];
extern uint32_t CPCPalette[4];
extern uint32_t EGAPalette[16];
extern uint32_t CGAPaletteRed[16];
extern uint32_t CGAPaletteCyan[16];
extern uint32_t DefaultPalette[16];
