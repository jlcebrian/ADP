#pragma once

#include <os_types.h>

bool DMG_PackChunkyPixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output);
bool DMG_PackPlanar8Pixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output);
bool DMG_PackBitplaneBytes(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output);
bool DMG_PackBitplaneWords(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output);