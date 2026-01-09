#ifndef __DMG_IMG_H__
#define __DMG_IMG_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

extern bool LoadPNGIndexed16 (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette);
extern bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette);
extern bool SavePNGRGB32     (const char* filename, uint32_t* pixels, uint16_t width, uint16_t height);
extern bool SaveCOLPalette16 (const char* filename, uint32_t* palette);
extern bool CompressImage    (uint8_t* pixels, int count, uint8_t* buffer, size_t bufferSize, bool* compressed, uint16_t* size, bool debug);

#endif // __DMG_IMG_H__