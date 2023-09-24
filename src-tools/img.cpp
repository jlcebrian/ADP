
#include <dmg.h>
#include <img.h>
#include <os_file.h>
#include <os_bito.h>
#include <os_mem.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <png.h>

bool LoadPNGIndexed16(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette) 
{
	png_uint_32 y;
	png_structp png;
	png_infop info;
	png_uint_32 pngWidth;
	png_uint_32 pngHeight;
	int colorType, bitDepth;
	png_colorp pngPalette;
    int numPaletteEntries;
	png_bytep* rowPointers;

	FILE* file = fopen(filename, "rb");
    if (!file) {
        return false; // Failed to open the file
    }
    
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        return false; // Failed to create png_struct
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false; // Error during PNG read
    }
    
    png_init_io(png, file);
    png_read_info(png, info);
    
    colorType = png_get_color_type(png, info);
    if (colorType != PNG_COLOR_TYPE_PALETTE) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false;
    }
    
    pngWidth = png_get_image_width(png, info);
    pngHeight = png_get_image_height(png, info);
    bitDepth = png_get_bit_depth(png, info);
    
    if (bitDepth > 8) {
        png_set_strip_16(png);
    }

    *width = (uint16_t)pngWidth;
    *height = (uint16_t)pngHeight;
    
    png_get_PLTE(png, info, &pngPalette, &numPaletteEntries);
    
    if (numPaletteEntries > 256 || numPaletteEntries <= 0) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false; // Invalid palette size
    }
	
	#ifdef _DMG
	if (numPaletteEntries > 16) {
		bool colorFound = false;
    	for (int i = 16; i < numPaletteEntries; i++) {
			if (pngPalette[i].red != pngPalette[i].green || pngPalette[i].red != pngPalette[i].blue) {
				colorFound = true;
				break;
			}
		}
		if (colorFound) {
			DMG_Warning("Image has palette size %d, larger than 16", numPaletteEntries);
		}
	}
	#endif
    
    for (int i = 0; i < numPaletteEntries && i < 16; i++) {
        palette[i] = 0xFF000000 | (pngPalette[i].red << 16) | (pngPalette[i].green << 8) | pngPalette[i].blue;
    }
    rowPointers = Allocate<png_bytep>("PNG buffer", pngHeight * sizeof(png_bytep));
    for (y = 0; y < pngHeight; y++) {
        rowPointers[y] = (png_bytep)(buffer + y * pngWidth);
    }
    
    png_read_image(png, rowPointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(file);
    Free(rowPointers);
    
    return true; // Successfully loaded the image
}

bool SaveCOLPalette16 (const char* filename, uint32_t* palette)
{
	int n;
	uint8_t data[56];

	File* file = File_Create(filename);
	if (!file)
		return false; // Failed to open the file

	write32(data, sizeof(data), true);
	write16(data + 4, 0xB123, true);
	write16(data + 6, 0x0000, true);
	for (n = 0; n < 16; n++)
	{
		uint8_t r = (uint8_t)(palette[n] >> 16);
		uint8_t g = (uint8_t)(palette[n] >> 8);
		uint8_t b = (uint8_t)palette[n];
		data[8 + n*3] = r;
		data[8 + n*3 + 1] = g;
		data[8 + n*3 + 2] = b;
	}

	bool success = File_Write(file, data, sizeof(data)) == sizeof(data);
	File_Close(file);
	return success;
}

bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette)
{
	FILE* file = fopen(filename, "wb");
	if (!file)
		return false; // Failed to open the file

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info || !png) {
		png_destroy_write_struct(&png, NULL);
		fclose(file);
		return false; // Failed to create png_info
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return false; // Error during PNG write
	}

	png_init_io(png, file);
	png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_colorp pngPalette = (png_colorp)png_malloc(png, 16 * sizeof(png_color));
	for (int i = 0; i < 16; i++) {
		pngPalette[i].red = (png_byte)(palette[i] >> 16);
		pngPalette[i].green = (png_byte)(palette[i] >> 8);
		pngPalette[i].blue = (png_byte)palette[i];
	}

	png_set_PLTE(png, info, pngPalette, 16);

	png_bytep* rowPointers = (png_bytep*)png_malloc(png, height * sizeof(png_bytep));
	for (int y = 0; y < height; y++) {
		rowPointers[y] = (png_bytep)(pixels + y * width);
	}

	png_set_rows(png, info, rowPointers);
	png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
	png_free(png, pngPalette);
	png_free(png, rowPointers);
	png_destroy_write_struct(&png, &info);
	fclose(file);
	return true; // Successfully saved the image
}

bool SavePNGRGB32 (const char* filename, uint32_t* pixels, uint16_t width, uint16_t height)
{
	int n;

	FILE* file = fopen(filename, "wb");
	if (!file)
	{
		return false; // Failed to open the file
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info || !png) {
		png_destroy_write_struct(&png, NULL);
		fclose(file);
		return false; // Failed to create png_info
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return false; // Error during PNG write
	}

	png_init_io(png, file);
	png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_bytep* rowPointers = (png_bytep*)png_malloc(png, height * sizeof(png_bytep));
	for (n = 0; n < height; n++) {
		rowPointers[n] = (png_bytep)(pixels + n * width);
	}

	png_set_rows(png, info, rowPointers);
	png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
	png_free(png, rowPointers);
	png_destroy_write_struct(&png, &info);
	fclose(file);

	return true; // Successfully saved the image
}