#pragma once

#include <os_types.h>

bool VID_InitializeTextDraw();
void VID_FinishTextDraw();
void VID_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
void VID_DrawCharacterSolidPatched(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint16_t cover, uint8_t ink, uint8_t paper);
void VID_DrawCharacterTransparentPatched(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint8_t ink);
