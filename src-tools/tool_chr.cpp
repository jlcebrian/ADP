#include <img.h>
#include <dmg_font.h>
#include <ddb.h>
#include <os_lib.h>
#include <os_file.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

static uint8_t indexed[128 * 384];
static uint32_t palette[16] = { 0x00000000, 0xFFFFFFFF, 0xFF000000 };
static uint8_t paletteAlpha[256];

static const uint8_t Pixel_Transparent = 0;
static const uint8_t Pixel_Background = 1;
static const uint8_t Pixel_Foreground = 2;

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
};

void TracePrintf(const char* format, ...)
{
}

static void ShowHelp()
{
	printf("CHR Character Set utility for DAAD " VERSION_STR "\n\n");
	printf("Converts between DAAD character sets, SINTAC fonts and PNG files.\n\n");
	printf("Usage: chr <input> [<output>]\n\n");
	printf("Supported inputs: .png, .chr, .fnt\n");
	printf("Supported outputs: .png, .chr, .fnt\n\n");
	printf("PNG layouts:\n");
	printf("  128x128: 8-pixel font sheet\n");
	printf("  128x256: 16-pixel font sheet\n");
	printf("  128x384: 16-pixel font sheet on top, 8-pixel sheet below\n");
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

	if (size == 6672 && size >= 16 && MemComp((void*)header, (void*)"JSJ SINTAC FNT3\0", 16) == 0)
		return InputFormat_FNT;

	if (size == 2176)
		return InputFormat_CHR;

	return InputFormat_Unknown;
}

static const char* GetDefaultOutputExtension(InputFormat inputFormat, uint16_t pngHeight)
{
	if (inputFormat == InputFormat_PNG)
	{
		if (pngHeight == 256 || pngHeight == 384)
			return ".fnt";
		return ".chr";
	}
	return ".png";
}

static uint8_t MeasureGlyphWidth(const uint8_t* glyph, int height)
{
	int maxWidth = 0;
	for (int y = 0; y < height; y++)
	{
		uint8_t bits = glyph[y];
		for (int x = 0; x < 8; x++)
		{
			if ((bits & (0x80 >> x)) != 0)
				maxWidth = x + 1;
		}
	}

	int width = maxWidth + 1;
	if (width > 8)
		width = 8;
	if (width < 1)
		width = 1;
	return (uint8_t)width;
}

static void UpdateWidths(uint8_t* widths, const uint8_t* bitmap, int glyphHeight)
{
	for (int i = 0; i < 256; i++)
		widths[i] = MeasureGlyphWidth(bitmap + i * glyphHeight, glyphHeight);
	widths[32] = 8;
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

static uint8_t GetWidthFromIndexedGlyph(const uint8_t* pixels, int glyphHeight)
{
	int maxWidth = 0;
	for (int y = 0; y < glyphHeight; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			uint8_t value = pixels[y * 128 + x];
			if (paletteAlpha[value] != 0)
				maxWidth = x + 1;
		}
	}
	if (maxWidth == 0)
		return 1;
	return (uint8_t)maxWidth;
}

static void ConvertCharsetToIndexed(const uint8_t* charset, const uint8_t* widths, int glyphHeight, uint8_t* pixels, int imageHeight, int offsetY)
{
	for (int i = 0; i < 256; i++)
	{
		int destx = (i & 0x0F) * 8;
		int desty = offsetY + (i >> 4) * glyphHeight;
		int glyphWidth = widths ? widths[i] : 8;
		if (glyphWidth < 1)
			glyphWidth = 1;
		if (glyphWidth > 8)
			glyphWidth = 8;

		const uint8_t* src = charset + i * glyphHeight;
		uint8_t* dst = pixels + destx + desty * 128;
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
			dst += 128;
		}
	}
}

static void ConvertIndexedToCharset(const uint8_t* pixels, int glyphHeight, int offsetY, uint8_t* charset, uint8_t* widths, bool explicitWidths)
{
	for (int i = 0; i < 256; i++)
	{
		int srcx = (i & 0x0F) * 8;
		int srcy = offsetY + (i >> 4) * glyphHeight;

		const uint8_t* src = pixels + srcx + srcy * 128;
		uint8_t* dst = charset + i * glyphHeight;
		if (widths && explicitWidths)
			widths[i] = GetWidthFromIndexedGlyph(src, glyphHeight);

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
			src += 128 - 8;
		}
	}

	if (widths)
	{
		if (explicitWidths)
			widths[32] = 8;
		else
			UpdateWidths(widths, charset, glyphHeight);
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
	UpdateWidths(state->font.width8, state->font.bitmap8, 8);
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
	if (!LoadPNGIndexed16(fileName, indexed, sizeof(indexed), width, height, palette, paletteAlpha))
		return false;

	if (*width != 128 || (*height != 128 && *height != 256 && *height != 384))
		return false;

	bool explicitWidths = HasTransparentPaletteEntries();

	if (*height == 128 || *height == 384)
	{
		ConvertIndexedToCharset(indexed, 8, *height == 384 ? 256 : 0, state->font.bitmap8, state->font.width8, explicitWidths);
		state->hasFont8 = true;
	}

	if (*height == 256 || *height == 384)
	{
		ConvertIndexedToCharset(indexed, 16, 0, state->font.bitmap16, state->font.width16, explicitWidths);
		state->hasFont16 = true;
	}

	return true;
}

static bool SavePNG(const char* fileName, const ToolFontState* state)
{
	int height = state->hasFont16 ? (state->hasFont8 ? 384 : 256) : 128;
	MemClear(indexed, sizeof(indexed));
	InitializePNGPalette();

	if (state->hasFont16)
		ConvertCharsetToIndexed(state->font.bitmap16, state->font.width16, 16, indexed, height, 0);
	if (state->hasFont8)
		ConvertCharsetToIndexed(state->font.bitmap8, state->font.width8, 8, indexed, height, state->hasFont16 ? 256 : 0);

	return SavePNGIndexed16(fileName, indexed, 128, height, palette, 3, paletteAlpha);
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
			if (!DMG_ReadSINTACFont(fileName, &state->font))
				return false;
			state->hasFont16 = true;
			state->hasFont8 = true;
			return true;
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
		return DMG_WriteSINTACFont(fileName, &state->font);
	return false;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	ToolFontState state;
	InitializePNGPalette();
	InitializeFont(&state.font);
	state.hasFont16 = false;
	state.hasFont8 = false;

	InputFormat inputFormat;
	uint16_t pngHeight = 0;
	if (!LoadInput(argv[1], &state, &inputFormat, &pngHeight))
	{
		printf("Error: unable to read input file: %s\n", argv[1]);
		return 1;
	}

	const char* outputFileName = argc > 2 ? argv[2] : ChangeExtension(argv[1], GetDefaultOutputExtension(inputFormat, pngHeight));
	if (!SaveOutput(outputFileName, &state))
	{
		printf("Error: unable to write to file: %s\n", outputFileName);
		return 1;
	}

	printf("%s written.\n", outputFileName);
	return 0;
}
