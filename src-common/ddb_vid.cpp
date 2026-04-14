#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_file.h>
#include <os_lib.h>
#include <dmg.h>

#ifdef _AMIGA
extern void VID_PresentDefaultScreen();
extern void VID_SetPaletteRangeFast(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool waitForVBlank);
#endif

bool		waitingForKey = false;
bool 		buffering = false;
uint8_t 	lineHeight;
uint8_t 	columnWidth;
uint16_t	screenWidth;
uint16_t	screenHeight;
uint8_t 	charWidth[256];

void VID_ResetDisplay()
{
#ifdef _AMIGA
	VID_PresentDefaultScreen();
#else
	VID_Clear(0, 0, screenWidth, screenHeight, 0, Clear_All);
	VID_ClearBuffer(true);
	VID_ClearBuffer(false);
	VID_SetDefaultPalette();
	VID_ActivatePalette();
#endif
}

#ifndef _AMIGA
void VID_SetPaletteEntries(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool waitForVBlank)
{
	uint16_t paletteSize = VID_GetPaletteSize();
	if (firstColor > paletteSize)
		return;
	if (count > paletteSize - firstColor)
		count = paletteSize - firstColor;

	if (clearOutside)
	{
		for (uint16_t i = 0; i < firstColor; i++)
			VID_SetPaletteColor(i, 0, 0, 0);
		for (uint16_t i = firstColor + count; i < paletteSize; i++)
			VID_SetPaletteColor(i, 0, 0, 0);
	}

	for (uint16_t i = 0; i < count; i++)
	{
		uint32_t color = palette[i];
		VID_SetPaletteColor(firstColor + i,
			(color >> 16) & 0xFF,
			(color >> 8) & 0xFF,
			color & 0xFF);
	}

	if (waitForVBlank)
		VID_VSync();
	VID_ActivatePalette();
}
#endif

void VID_SetPaletteRange(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool waitForVBlank)
{
#ifdef _AMIGA
	VID_SetPaletteRangeFast(palette, count, firstColor, clearOutside, waitForVBlank);
#else
	VID_SetPaletteEntries(palette, count, firstColor, clearOutside, waitForVBlank);
#endif
}

#if HAS_PCX
static char externalPictureBase[FILE_MAX_PATH];

static bool FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static const char* GetLastPathSeparator(const char* fileName)
{
	const char* slash = StrRChr(fileName, '/');
	const char* backslash = StrRChr(fileName, '\\');
	if (slash == 0)
		return backslash;
	if (backslash == 0)
		return slash;
	return slash > backslash ? slash : backslash;
}

static void BuildExternalPictureFileName(const char* fileName, uint8_t picno, const char* extension, char* output, size_t outputSize)
{
	if (output == 0 || outputSize == 0)
		return;

	output[0] = 0;
	if (fileName == 0 || fileName[0] == 0)
		return;

	const char* separator = GetLastPathSeparator(fileName);
	size_t prefixLength = separator == 0 ? 0 : (size_t)(separator - fileName + 1);
	if (prefixLength >= outputSize)
		prefixLength = outputSize - 1;

	if (prefixLength > 0)
	{
		MemCopy(output, fileName, prefixLength);
		output[prefixLength] = 0;
	}

	if (prefixLength + 4 >= outputSize)
		return;

	output[prefixLength + 0] = '0' + (picno / 100);
	output[prefixLength + 1] = '0' + ((picno / 10) % 10);
	output[prefixLength + 2] = '0' + (picno % 10);
	output[prefixLength + 3] = 0;
	StrCat(output, outputSize, extension);
}

void VID_SetExternalPictureBase(const char* fileName)
{
	if (fileName == 0)
		externalPictureBase[0] = 0;
	else
		StrCopy(externalPictureBase, sizeof(externalPictureBase), fileName);
}

bool VID_GetExternalPictureFileName(uint8_t picno, char* fileName, size_t fileNameSize)
{
	if (externalPictureBase[0] == 0 || fileName == 0 || fileNameSize == 0)
		return false;

	#if HAS_SPECTRUM
	if (screenMachine == DDB_MACHINE_SPECTRUM)
	{
		BuildExternalPictureFileName(externalPictureBase, picno, ".ZXS", fileName, fileNameSize);
		if (FileExists(fileName))
			return true;

		BuildExternalPictureFileName(externalPictureBase, picno, ".zxs", fileName, fileNameSize);
		if (FileExists(fileName))
			return true;
	}
	#endif

	BuildExternalPictureFileName(externalPictureBase, picno, ".VGA", fileName, fileNameSize);
	if (FileExists(fileName))
		return true;

	BuildExternalPictureFileName(externalPictureBase, picno, ".vga", fileName, fileNameSize);
	if (FileExists(fileName))
		return true;

	fileName[0] = 0;
	return false;
}

bool VID_HasExternalPictures()
{
	return externalPictureBase[0] != 0;
}
#endif

bool VID_PictureExists (uint8_t picno)
{
	#if HAS_PCX
	if (dmg == 0)
	{
		char fileName[FILE_MAX_PATH];
		return VID_GetExternalPictureFileName(picno, fileName, sizeof(fileName));
	}
	#endif
	
	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	return (entry != 0 && entry->type == DMGEntry_Image);
}

bool VID_SampleExists (uint8_t picno)
{
	if (dmg == 0) return false;

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	return (entry != 0 && entry->type == DMGEntry_Audio);
}

void VID_SetDefaultPalette()
{
	for (int n = 0; n < 16; n++)
	{
		uint32_t color = DefaultPalette[n];
		VID_SetPaletteColor(n, (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
	}
}

void VID_ShowError(const char* msg)
{
	int bg = 0, cr = 0;
	for (int n = 0; n < 16; n++)
	{
		uint8_t r, g, b;
		VID_GetPaletteColor(n, &r, &g, &b);
		if (r > cr)
		{
			bg = n;
			cr = r;
		}
	}

	VID_Clear(0, 0, 320, 10, bg);
	
	uint16_t x = 160 - StrLen(msg)*3;
	for (int n = 0; msg[n]; n++, x+= 6)
		VID_DrawCharacter(x, 1, msg[n], 0, 255);

	VID_WaitForKey();
}

static bool progressBarVisible = false;

void VID_ShowProgressBar(uint16_t amount)
{
	uint16_t x = screenWidth/2 - 32;
	uint16_t y = screenHeight - 32;
	
	if (!progressBarVisible)
	{
		VID_Clear(x, y, 64, 1, 15);
		VID_Clear(x, y, 1, 12, 15);
		VID_Clear(x+63, y, 1, 12, 15);
		VID_Clear(x, y+11, 64, 1, 15);
		VID_Clear(x+1, y+1, 62, 10, 0);
		progressBarVisible = true;
	}
	VID_Clear(x+2, y+2, amount*60/255, 8, 15);
}

#if !defined(HAS_DRAWTEXT) && !defined(_AMIGA)

void VID_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	for (uint16_t n = 0; n < length && x < screenWidth; n++)
	{
		uint8_t ch = text[n];
		VID_DrawCharacter(x, y, ch, ink, paper);
		x += charWidth[ch];
	}
}

void VID_DrawText(int x, int y, const char* text, uint8_t ink, uint8_t paper)
{
	VID_DrawTextSpan(x, y, (const uint8_t*)text, (uint16_t)StrLen(text), ink, paper);
}

#endif
