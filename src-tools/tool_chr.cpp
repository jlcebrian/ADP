#include <img.h>
#include <dmg_font.h>
#include <ddb.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_file.h>

#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

static uint8_t indexed[256 * 384];
static uint32_t palette[16] = { 0x00000000, 0xFFFFFFFF, 0xFF000000 };
static uint8_t paletteAlpha[256];

static const uint8_t Pixel_Transparent = 0;
static const uint8_t Pixel_Background = 1;
static const uint8_t Pixel_Foreground = 2;
static bool forceV4PNG = false;

enum InputFormat
{
	InputFormat_Unknown,
	InputFormat_PNG,
	InputFormat_CHR,
	InputFormat_FNT,
};

struct ToolFontState
{
	DMG_Font font;
	bool hasFont16;
	bool hasFont8;
	bool hasLegacyFont16;
	uint8_t legacyWidth16[256];
	uint8_t legacyBitmap16[256 * 16];
	uint8_t sourceSintacVersion;
};

void TracePrintf(const char* format, ...)
{
}

static bool ReadExact(File* file, void* buffer, uint64_t size)
{
	return File_Read(file, buffer, size) == size;
}

static void ShowHelp()
{
	printf("CHR Character Set utility for DAAD " VERSION_STR "\n\n");
	printf("Converts between DAAD character sets, SINTAC fonts and PNG files.\n\n");
	printf("Usage: chr [--v4-png] <input> [<output>]\n\n");
	printf("Supported inputs: .png, .chr, .fnt\n");
	printf("Supported outputs: .png, .chr, .fnt\n\n");
	printf("Options:\n");
	printf("  --v4-png: Export FNT3 inputs as v4 PNG layouts (16x16 + optional 8x8)\n\n");
	printf("PNG layouts:\n");
	printf("  128x128: v3/v4 8x8 font sheet\n");
	printf("  128x256: legacy v3 8x16 font sheet\n");
	printf("  128x384: legacy v3 8x16 sheet on top, 8x8 sheet below\n");
	printf("  256x256: v4 16x16 font sheet\n");
	printf("  256x320: v4 16x16 sheet on top, 8x8 sheet below in 32 columns\n");
}

static void InitializePNGPalette()
{
	for (int i = 0; i < 256; i++)
		paletteAlpha[i] = 255;
	paletteAlpha[Pixel_Transparent] = 0;
}

static void InitializeFont(DMG_Font* font)
{
	MemClear(font, sizeof(*font));
	for (int i = 0; i < 256; i++)
	{
		font->width16[i] = 8;
		font->width8[i] = 8;
	}
}

static void InitializeState(ToolFontState* state)
{
	InitializePNGPalette();
	InitializeFont(&state->font);
	state->hasFont16 = false;
	state->hasFont8 = false;
	state->hasLegacyFont16 = false;
	state->sourceSintacVersion = 0;
	MemClear(state->legacyWidth16, sizeof(state->legacyWidth16));
	MemClear(state->legacyBitmap16, sizeof(state->legacyBitmap16));
}

static bool HasExtension(const char* fileName, const char* extension)
{
	const char* dot = StrRChr(fileName, '.');
	return dot != 0 && StrIComp(dot, extension) == 0;
}

static InputFormat DetectInputFormat(const uint8_t* header, uint64_t size)
{
	if (size >= 8 &&
		header[0] == 0x89 &&
		header[1] == 0x50 &&
		header[2] == 0x4E &&
		header[3] == 0x47 &&
		header[4] == 0x0D &&
		header[5] == 0x0A &&
		header[6] == 0x1A &&
		header[7] == 0x0A)
	{
		return InputFormat_PNG;
	}

	if (size >= 16 &&
		(MemComp((void*)header, (void*)"JSJ SINTAC FNT3\0", 16) == 0 ||
		 MemComp((void*)header, (void*)"JSJ SINTAC FNT4\0", 16) == 0))
		return InputFormat_FNT;

	if (size == 2176)
		return InputFormat_CHR;

	return InputFormat_Unknown;
}

static const char* GetDefaultOutputExtension(InputFormat inputFormat, uint16_t pngHeight)
{
	if (inputFormat == InputFormat_PNG)
	{
		if (pngHeight == 256 || pngHeight == 320 || pngHeight == 384)
			return ".fnt";
		return ".chr";
	}
	return ".png";
}

static uint16_t Pack8To16Bits(uint8_t bits)
{
	return (uint16_t)bits << 8;
}

static uint16_t Expand8To16Bits(uint8_t bits)
{
	uint16_t expanded = 0;
	for (int x = 0; x < 8; x++)
	{
		if ((bits & (0x80 >> x)) == 0)
			continue;
		expanded |= (uint16_t)(0xC000u >> (x * 2));
	}
	return expanded;
}


static void PromoteLegacy16ToV4(DMG_Font* font, const uint8_t* width8x16, const uint8_t* bitmap8x16)
{
	for (int glyph = 0; glyph < 256; glyph++)
	{
		uint8_t width = width8x16[glyph];
		if (width > 8)
			width = 8;
		font->width16[glyph] = width;

		const uint8_t* src = bitmap8x16 + glyph * 16;
		uint8_t* dst = font->bitmap16 + glyph * 32;
		for (int row = 0; row < 16; row++)
		{
			uint16_t packed = Pack8To16Bits(src[row]);
			dst[row * 2 + 0] = (uint8_t)(packed >> 8);
			dst[row * 2 + 1] = (uint8_t)(packed & 0xFF);
		}
	}
}

static void Scale8To16(DMG_Font* font)
{
	for (int glyph = 0; glyph < 256; glyph++)
	{
		uint8_t width = font->width8[glyph];
		if (width > 8)
			width = 8;
		font->width16[glyph] = (uint8_t)(width * 2);

		const uint8_t* src = font->bitmap8 + glyph * 8;
		uint8_t* dst = font->bitmap16 + glyph * 32;
		for (int row = 0; row < 8; row++)
		{
			uint16_t expanded = Expand8To16Bits(src[row]);
			int dstRow = row * 2;
			dst[dstRow * 2 + 0] = (uint8_t)(expanded >> 8);
			dst[dstRow * 2 + 1] = (uint8_t)(expanded & 0xFF);
			dst[(dstRow + 1) * 2 + 0] = (uint8_t)(expanded >> 8);
			dst[(dstRow + 1) * 2 + 1] = (uint8_t)(expanded & 0xFF);
		}
	}
}

static uint8_t MeasureGlyphWidth8(const uint8_t* glyph, int height)
{
	int maxWidth = 0;
	for (int y = 0; y < height; y++)
	{
		uint8_t bits = glyph[y];
		for (int x = 0; x < 8; x++)
		{
			if ((bits & (0x80 >> x)) != 0 && x + 1 > maxWidth)
				maxWidth = x + 1;
		}
	}
	if (maxWidth < 1)
		maxWidth = 1;
	return (uint8_t)maxWidth;
}

static uint8_t MeasureGlyphWidth16(const uint8_t* glyph)
{
	int maxWidth = 0;
	for (int y = 0; y < 16; y++)
	{
		uint16_t bits = (uint16_t)(glyph[y * 2] << 8) | glyph[y * 2 + 1];
		for (int x = 0; x < 16; x++)
		{
			if ((bits & (0x8000 >> x)) != 0 && x + 1 > maxWidth)
				maxWidth = x + 1;
		}
	}
	if (maxWidth < 1)
		maxWidth = 1;
	return (uint8_t)maxWidth;
}

static void UpdateWidths8(uint8_t* widths, const uint8_t* bitmap, int glyphHeight)
{
	int maxW = 0;
	for (int i = 0; i < 256; i++)
	{
		int w = MeasureGlyphWidth8(bitmap + i * glyphHeight, glyphHeight);
		if (w > maxW)
			maxW = w;
	}
	if (maxW < 6)
		maxW = 6;
	for (int i = 0; i < 256; i++)
		widths[i] = (uint8_t)maxW;
}

static void UpdateWidths16(uint8_t* widths, const uint8_t* bitmap)
{
	int maxW = 0;
	for (int i = 0; i < 256; i++)
	{
		int w = MeasureGlyphWidth16(bitmap + i * 32);
		if (w > maxW)
			maxW = w;
	}
	if (maxW < 6)
		maxW = 6;
	for (int i = 0; i < 256; i++)
		widths[i] = (uint8_t)maxW;
}

static bool HasTransparentPaletteEntries()
{
	for (int i = 0; i < 256; i++)
	{
		if (paletteAlpha[i] == 0)
			return true;
	}
	return false;
}

static void NormalizeIndexedToToolPalette(uint16_t width, uint16_t height)
{
	uint32_t pixelCount = (uint32_t)width * height;
	for (uint32_t i = 0; i < pixelCount; i++)
	{
		uint8_t value = indexed[i];
		if (paletteAlpha[value] == 0)
		{
			indexed[i] = Pixel_Transparent;
			continue;
		}

		uint32_t color = palette[value];
		uint8_t r = (uint8_t)((color >> 16) & 0xFF);
		uint8_t g = (uint8_t)((color >> 8) & 0xFF);
		uint8_t b = (uint8_t)(color & 0xFF);
		int luminance = r * 299 + g * 587 + b * 114;
		indexed[i] = luminance >= 128000 ? Pixel_Background : Pixel_Foreground;
	}

	for (int i = 0; i < 256; i++)
		paletteAlpha[i] = 255;
	paletteAlpha[Pixel_Transparent] = 0;
	palette[Pixel_Transparent] = 0x00000000;
	palette[Pixel_Background] = 0xFFFFFFFF;
	palette[Pixel_Foreground] = 0xFF000000;
}

static uint8_t GetWidthFromIndexedGlyph(const uint8_t* pixels, int imageWidth, int glyphWidth, int glyphHeight)
{
	int maxWidth = 0;
	for (int y = 0; y < glyphHeight; y++)
	{
		for (int x = 0; x < glyphWidth; x++)
		{
			uint8_t value = pixels[y * imageWidth + x];
			if (paletteAlpha[value] != 0)
				maxWidth = x + 1;
		}
	}
	if (maxWidth == 0)
		return 1;
	return (uint8_t)maxWidth;
}

static void ConvertCharset8ToIndexed(const uint8_t* charset, const uint8_t* widths, int glyphHeight, uint8_t* pixels, int imageWidth, int imageHeight, int columns, int offsetY)
{
	for (int i = 0; i < 256; i++)
	{
		int destx = (i % columns) * 8;
		int desty = offsetY + (i / columns) * glyphHeight;
		int glyphWidth = widths ? widths[i] : 8;
		if (glyphWidth < 1)
			glyphWidth = 1;
		if (glyphWidth > 8)
			glyphWidth = 8;

		const uint8_t* src = charset + i * glyphHeight;
		uint8_t* dst = pixels + destx + desty * imageWidth;
		for (int y = 0; y < glyphHeight && desty + y < imageHeight; y++)
		{
			uint8_t bits = *src++;
			for (int x = 0; x < 8; x++)
			{
				if (x >= glyphWidth)
					dst[x] = Pixel_Transparent;
				else if ((bits & (0x80 >> x)) != 0)
					dst[x] = Pixel_Foreground;
				else
					dst[x] = Pixel_Background;
			}
			dst += imageWidth;
		}
	}
}

static void ConvertCharset16ToIndexed(const uint8_t* charset, const uint8_t* widths, uint8_t* pixels, int imageWidth, int imageHeight, int offsetY)
{
	for (int i = 0; i < 256; i++)
	{
		int destx = (i & 0x0F) * 16;
		int desty = offsetY + (i >> 4) * 16;
		int glyphWidth = widths ? widths[i] : 16;
		if (glyphWidth < 1)
			glyphWidth = 1;
		if (glyphWidth > 16)
			glyphWidth = 16;

		const uint8_t* src = charset + i * 32;
		uint8_t* dst = pixels + destx + desty * imageWidth;
		for (int y = 0; y < 16 && desty + y < imageHeight; y++)
		{
			uint16_t bits = (uint16_t)(src[0] << 8) | src[1];
			src += 2;
			for (int x = 0; x < 16; x++)
			{
				if (x >= glyphWidth)
					dst[x] = Pixel_Transparent;
				else if ((bits & (0x8000 >> x)) != 0)
					dst[x] = Pixel_Foreground;
				else
					dst[x] = Pixel_Background;
			}
			dst += imageWidth;
		}
	}
}

static void ConvertLegacy16ToIndexed(const uint8_t* charset, const uint8_t* widths, uint8_t* pixels, int imageWidth, int imageHeight, int offsetY)
{
	for (int i = 0; i < 256; i++)
	{
		int destx = (i & 0x0F) * 16;
		int desty = offsetY + (i >> 4) * 16;
		int glyphWidth = widths ? widths[i] : 8;
		if (glyphWidth < 1)
			glyphWidth = 1;
		if (glyphWidth > 8)
			glyphWidth = 8;

		const uint8_t* src = charset + i * 16;
		uint8_t* dst = pixels + destx + desty * imageWidth;
		for (int y = 0; y < 16 && desty + y < imageHeight; y++)
		{
			uint8_t bits = *src++;
			for (int x = 0; x < 16; x++)
			{
				if (x >= glyphWidth)
					dst[x] = Pixel_Transparent;
				else if ((bits & (0x80 >> x)) != 0)
					dst[x] = Pixel_Foreground;
				else
					dst[x] = Pixel_Background;
			}
			dst += imageWidth;
		}
	}
}

static void ConvertIndexedToCharset8(const uint8_t* pixels, int imageWidth, int glyphHeight, int columns, int offsetY, uint8_t* charset, uint8_t* widths, bool explicitWidths)
{
	for (int i = 0; i < 256; i++)
	{
		int srcx = (i % columns) * 8;
		int srcy = offsetY + (i / columns) * glyphHeight;

		const uint8_t* src = pixels + srcx + srcy * imageWidth;
		uint8_t* dst = charset + i * glyphHeight;
		if (widths && explicitWidths)
			widths[i] = GetWidthFromIndexedGlyph(src, imageWidth, 8, glyphHeight);

		for (int y = 0; y < glyphHeight; y++)
		{
			uint8_t bits = 0;
			for (int x = 0; x < 8; x++)
			{
				uint8_t value = *src++;
				bits <<= 1;
				if (explicitWidths)
				{
					if (paletteAlpha[value] != 0 && value != Pixel_Background)
						bits |= 1;
				}
				else if (value != 0)
				{
					bits |= 1;
				}
			}
			*dst++ = bits;
			src += imageWidth - 8;
		}
	}

	if (widths)
	{
		if (!explicitWidths)
			UpdateWidths8(widths, charset, glyphHeight);
	}
}

static void ConvertIndexedToCharset16(const uint8_t* pixels, int imageWidth, int offsetY, uint8_t* charset, uint8_t* widths, bool explicitWidths)
{
	for (int i = 0; i < 256; i++)
	{
		int srcx = (i & 0x0F) * 16;
		int srcy = offsetY + (i >> 4) * 16;

		const uint8_t* src = pixels + srcx + srcy * imageWidth;
		uint8_t* dst = charset + i * 32;
		if (widths && explicitWidths)
			widths[i] = GetWidthFromIndexedGlyph(src, imageWidth, 16, 16);

		for (int y = 0; y < 16; y++)
		{
			uint16_t bits = 0;
			for (int x = 0; x < 16; x++)
			{
				uint8_t value = *src++;
				bits <<= 1;
				if (explicitWidths)
				{
					if (paletteAlpha[value] != 0 && value != Pixel_Background)
						bits |= 1;
				}
				else if (value != 0)
				{
					bits |= 1;
				}
			}
			dst[0] = (uint8_t)(bits >> 8);
			dst[1] = (uint8_t)(bits & 0xFF);
			dst += 2;
			src += imageWidth - 16;
		}
	}

	if (widths)
	{
		if (!explicitWidths)
			UpdateWidths16(widths, charset);
	}
}

static bool LoadCHR(const char* fileName, ToolFontState* state)
{
	File* file = File_Open(fileName);
	if (file == 0)
		return false;

	bool ok =
		File_Seek(file, 128) &&
		File_Read(file, state->font.bitmap8, sizeof(state->font.bitmap8)) == sizeof(state->font.bitmap8);
	File_Close(file);

	if (!ok)
		return false;

	state->hasFont8 = true;
	UpdateWidths8(state->font.width8, state->font.bitmap8, 8);
	return true;
}

static bool LoadFNT(const char* fileName, ToolFontState* state)
{
	File* file = File_Open(fileName);
	if (file == 0)
		return false;

	uint64_t fileSize = File_GetSize(file);
	uint8_t header[16];
	bool ok =
		ReadExact(file, header, sizeof(header));
	if (!ok)
	{
		File_Close(file);
		return false;
	}

	if (fileSize == 6672 && MemComp((void*)header, (void*)"JSJ SINTAC FNT3\0", 16) == 0)
	{
		state->sourceSintacVersion = 3;
		ok =
			ReadExact(file, state->legacyWidth16, sizeof(state->legacyWidth16)) &&
			ReadExact(file, state->legacyBitmap16, sizeof(state->legacyBitmap16)) &&
			ReadExact(file, state->font.width8, sizeof(state->font.width8)) &&
			ReadExact(file, state->font.bitmap8, sizeof(state->font.bitmap8));
		File_Close(file);
		if (!ok)
			return false;

		PromoteLegacy16ToV4(&state->font, state->legacyWidth16, state->legacyBitmap16);
		state->hasLegacyFont16 = true;
		state->hasFont16 = true;
		state->hasFont8 = true;
		return true;
	}

	File_Close(file);
	if (!DMG_ReadSINTACFont(fileName, &state->font))
		return false;
	state->sourceSintacVersion = 4;
	state->hasFont16 = true;
	state->hasFont8 = true;
	return true;
}

static bool SaveCHR(const char* fileName, const ToolFontState* state)
{
	if (!state->hasFont8)
	{
		printf("Error: no 8-pixel font available for CHR output: %s\n", fileName);
		return false;
	}

	File* file = File_Create(fileName);
	if (file == 0)
		return false;

	char header[128];
	MemClear(header, sizeof(header));

	strncpy(header + 1, fileName, 8);
	for (char* ptr = header + 1; ptr < header + 9; ptr++)
	{
		*ptr = toupper(*ptr);
		if (*ptr == '.' || *ptr == 0)
		{
			*ptr = ' ';
			while (ptr < header + 9)
				*++ptr = ' ';
		}
	}
	strncpy(header + 9, "CHR", 3);
	header[0x12] = 2;
	header[0x41] = 8;
	header[0x43] = 0x24;
	header[0x44] = 0x02;

	bool ok =
		File_Write(file, header, sizeof(header)) == sizeof(header) &&
		File_Write(file, state->font.bitmap8, sizeof(state->font.bitmap8)) == sizeof(state->font.bitmap8);
	File_Close(file);
	return ok;
}

static bool LoadPNG(const char* fileName, ToolFontState* state, uint16_t* width, uint16_t* height)
{
	InitializePNGPalette();
	bool loadedIndexed = LoadPNGIndexed16(fileName, indexed, sizeof(indexed), width, height, palette, paletteAlpha);
	if (!loadedIndexed)
	{
		FILE* file = fopen(fileName, "rb");
		if (file == 0)
			return false;

		png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (png == 0)
		{
			fclose(file);
			return false;
		}
		png_infop info = png_create_info_struct(png);
		if (info == 0)
		{
			png_destroy_read_struct(&png, NULL, NULL);
			fclose(file);
			return false;
		}
		if (setjmp(png_jmpbuf(png)))
		{
			png_destroy_read_struct(&png, &info, NULL);
			fclose(file);
			return false;
		}

		png_init_io(png, file);
		png_read_info(png, info);

		uint32_t pngWidth = png_get_image_width(png, info);
		uint32_t pngHeight = png_get_image_height(png, info);
		int colorType = png_get_color_type(png, info);
		int bitDepth = png_get_bit_depth(png, info);

		if (bitDepth == 16)
			png_set_strip_16(png);
		if (colorType == PNG_COLOR_TYPE_PALETTE)
			png_set_palette_to_rgb(png);
		if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
			png_set_expand_gray_1_2_4_to_8(png);
		if (png_get_valid(png, info, PNG_INFO_tRNS))
			png_set_tRNS_to_alpha(png);
		if ((colorType & PNG_COLOR_MASK_ALPHA) == 0)
			png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
		if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(png);

		png_read_update_info(png, info);
		if (pngWidth * pngHeight > sizeof(indexed))
		{
			png_destroy_read_struct(&png, &info, NULL);
			fclose(file);
			return false;
		}

		png_bytep* rowPointers = Allocate<png_bytep>("PNG rows", pngHeight);
		png_byte* image = Allocate<png_byte>("PNG image", pngWidth * pngHeight * 4);
		if (rowPointers == 0 || image == 0)
		{
			if (rowPointers) Free(rowPointers);
			if (image) Free(image);
			png_destroy_read_struct(&png, &info, NULL);
			fclose(file);
			return false;
		}

		for (uint32_t y = 0; y < pngHeight; y++)
			rowPointers[y] = image + y * pngWidth * 4;
		png_read_image(png, rowPointers);
		png_destroy_read_struct(&png, &info, NULL);
		fclose(file);
		*width = (uint16_t)pngWidth;
		*height = (uint16_t)pngHeight;

		for (uint32_t y = 0; y < pngHeight; y++)
		{
			png_byte* src = image + y * pngWidth * 4;
			uint8_t* dst = indexed + y * pngWidth;
			for (uint32_t x = 0; x < pngWidth; x++, src += 4, dst++)
			{
				if (src[3] == 0)
				{
					*dst = Pixel_Transparent;
				}
				else
				{
					int luminance = src[0] * 299 + src[1] * 587 + src[2] * 114;
					*dst = luminance >= 128000 ? Pixel_Background : Pixel_Foreground;
				}
			}
		}

		Free(rowPointers);
		Free(image);
	}

	NormalizeIndexedToToolPalette(*width, *height);

	if (!((*width == 128 && (*height == 128 || *height == 256 || *height == 384)) ||
		(*width == 256 && (*height == 256 || *height == 320))))
		return false;

	bool explicitWidths = HasTransparentPaletteEntries();

	if (*width == 128 && (*height == 128 || *height == 384))
	{
		ConvertIndexedToCharset8(indexed, 128, 8, 16, *height == 384 ? 256 : 0, state->font.bitmap8, state->font.width8, explicitWidths);
		state->hasFont8 = true;
	}

	if (*width == 128 && (*height == 256 || *height == 384))
	{
		uint8_t legacyWidth16[256];
		uint8_t legacyBitmap16[256 * 16];
		ConvertIndexedToCharset8(indexed, 128, 16, 16, 0, legacyBitmap16, legacyWidth16, explicitWidths);
		PromoteLegacy16ToV4(&state->font, legacyWidth16, legacyBitmap16);
		state->hasFont16 = true;
	}

	if (*width == 256)
	{
		ConvertIndexedToCharset16(indexed, 256, 0, state->font.bitmap16, state->font.width16, explicitWidths);
		state->hasFont16 = true;
		if (*height == 320)
		{
			ConvertIndexedToCharset8(indexed, 256, 8, 32, 256, state->font.bitmap8, state->font.width8, explicitWidths);
			state->hasFont8 = true;
		}
	}

	return true;
}

static bool SavePNG(const char* fileName, const ToolFontState* state)
{
	if (forceV4PNG && state->sourceSintacVersion == 3 && state->hasLegacyFont16)
	{
		int width = 256;
		int height = state->hasFont8 ? 320 : 256;
		MemClear(indexed, sizeof(indexed));
		InitializePNGPalette();

		ConvertLegacy16ToIndexed(state->legacyBitmap16, state->legacyWidth16, indexed, width, height, 0);
		if (state->hasFont8)
			ConvertCharset8ToIndexed(state->font.bitmap8, state->font.width8, 8, indexed, width, height, 32, 256);

		return SavePNGIndexed16(fileName, indexed, width, height, palette, 3, paletteAlpha);
	}

	if (!forceV4PNG && state->sourceSintacVersion == 3 && state->hasLegacyFont16)
	{
		int width = 128;
		int height = state->hasFont8 ? 384 : 256;
		MemClear(indexed, sizeof(indexed));
		InitializePNGPalette();

		ConvertCharset8ToIndexed(state->legacyBitmap16, state->legacyWidth16, 16, indexed, width, height, 16, 0);
		if (state->hasFont8)
			ConvertCharset8ToIndexed(state->font.bitmap8, state->font.width8, 8, indexed, width, height, 16, 256);

		return SavePNGIndexed16(fileName, indexed, width, height, palette, 3, paletteAlpha);
	}

	int width = state->hasFont16 ? 256 : 128;
	int height = state->hasFont16 ? (state->hasFont8 ? 320 : 256) : 128;
	MemClear(indexed, sizeof(indexed));
	InitializePNGPalette();

	if (state->hasFont16)
		ConvertCharset16ToIndexed(state->font.bitmap16, state->font.width16, indexed, width, height, 0);
	if (state->hasFont8)
		ConvertCharset8ToIndexed(state->font.bitmap8, state->font.width8, 8, indexed, width, height, state->hasFont16 ? 32 : 16, state->hasFont16 ? 256 : 0);

	return SavePNGIndexed16(fileName, indexed, width, height, palette, 3, paletteAlpha);
}

static bool LoadInput(const char* fileName, ToolFontState* state, InputFormat* inputFormat, uint16_t* pngHeight)
{
	File* file = File_Open(fileName);
	if (file == 0)
		return false;

	uint8_t header[16];
	uint64_t size = File_GetSize(file);
	MemClear(header, sizeof(header));
	if (size > 0)
		File_Read(file, header, size < sizeof(header) ? size : sizeof(header));
	File_Close(file);

	*inputFormat = DetectInputFormat(header, size);
	if (*inputFormat == InputFormat_Unknown)
		return false;

	switch (*inputFormat)
	{
		case InputFormat_PNG:
		{
			uint16_t pngWidth = 0;
			return LoadPNG(fileName, state, &pngWidth, pngHeight);
		}
		case InputFormat_CHR:
			return LoadCHR(fileName, state);
		case InputFormat_FNT:
			return LoadFNT(fileName, state);
		default:
			return false;
	}
}

static bool SaveOutput(const char* fileName, const ToolFontState* state)
{
	if (HasExtension(fileName, ".png"))
		return SavePNG(fileName, state);
	if (HasExtension(fileName, ".chr"))
		return SaveCHR(fileName, state);
	if (HasExtension(fileName, ".fnt"))
	{
		DMG_Font font = state->font;
		if (!state->hasFont16 && state->hasFont8)
			Scale8To16(&font);
		return DMG_WriteSINTACFont(fileName, &font);
	}
	return false;
}

int main(int argc, char* argv[])
{
	const char* inputFileName = 0;
	const char* outputFileName = 0;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--v4-png") == 0)
		{
			forceV4PNG = true;
			continue;
		}

		if (argv[i][0] == '-')
		{
			printf("Error: unknown option: %s\n", argv[i]);
			return 1;
		}

		if (inputFileName == 0)
		{
			inputFileName = argv[i];
		}
		else if (outputFileName == 0)
		{
			outputFileName = argv[i];
		}
		else
		{
			printf("Error: too many positional arguments\n");
			return 1;
		}
	}

	if (inputFileName == 0)
	{
		ShowHelp();
		return 0;
	}

	ToolFontState state;
	InitializeState(&state);

	InputFormat inputFormat;
	uint16_t pngHeight = 0;
	if (!LoadInput(inputFileName, &state, &inputFormat, &pngHeight))
	{
		printf("Error: unable to read input file: %s\n", inputFileName);
		return 1;
	}

	if (outputFileName == 0)
		outputFileName = ChangeExtension(inputFileName, GetDefaultOutputExtension(inputFormat, pngHeight));
	if (!SaveOutput(outputFileName, &state))
	{
		printf("Error: unable to write to file: %s\n", outputFileName);
		return 1;
	}

	printf("%s written.\n", outputFileName);
	return 0;
}
