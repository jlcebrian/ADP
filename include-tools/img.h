#ifndef __DMG_IMG_H__
#define __DMG_IMG_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

extern bool LoadPNGIndexed16 (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette);
extern bool LoadPNGIndexed16 (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, uint8_t* paletteAlpha);
extern bool LoadPNGIndexed   (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize, uint8_t* paletteAlpha, int maxColors = 256, bool* reduced = 0, int* sourceColorCount = 0);
extern bool LoadPI1Indexed   (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize);
extern bool LoadIFFIndexed   (const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize);
extern bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette);
extern bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize, const uint8_t* paletteAlpha);
extern bool SavePNGIndexed   (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize, const uint8_t* paletteAlpha);
extern bool SavePI1Indexed   (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize);
extern bool SaveIFFIndexed   (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize);
extern bool SavePNGRGB32     (const char* filename, uint32_t* pixels, uint16_t width, uint16_t height);
extern bool SaveCOLPalette16 (const char* filename, uint32_t* palette);
extern bool SaveCOLPalette   (const char* filename, uint32_t* palette, int paletteSize);
extern bool CompressImage    (uint8_t* pixels, int count, uint8_t* buffer, size_t bufferSize, bool* compressed, uint16_t* size, bool debug);

#endif // __DMG_IMG_H__
