#pragma once

#include <os_types.h>

bool VID_InitializeTextDraw();
void VID_FinishTextDraw();
bool VID_DrawCharacterSolidPatched(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint16_t cover, uint8_t ink, uint8_t paper);
bool VID_DrawCharacterTransparentPatched(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint8_t ink);
