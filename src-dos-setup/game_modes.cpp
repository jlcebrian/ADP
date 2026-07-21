#ifdef _DOS

#include "game_modes.h"

#include <ddb.h>
#include <dmg.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static uint16_t Read16BE(const uint8_t* data)
{
	return ((uint16_t)data[0] << 8) | data[1];
}

static bool IsClassicSignature(uint16_t signature)
{
	return signature == 0x0004 || signature == 0x0300 || signature == 0xFFFF;
}

static uint32_t ModeFlag(DDB_ScreenMode mode)
{
	switch (mode)
	{
		case ScreenMode_CGA: return DDB_DataFileMode_CGA;
		case ScreenMode_EGA: return DDB_DataFileMode_EGA;
		case ScreenMode_VGA16: return DDB_DataFileMode_VGA16;
		case ScreenMode_VGA: return DDB_DataFileMode_VGA;
		case ScreenMode_HiRes: return DDB_DataFileMode_HiRes;
		case ScreenMode_SHiRes: return DDB_DataFileMode_SHiRes;
		default: return 0;
	}
}

static uint32_t ProbeDataFile(const char* fileName, DDB_ScreenMode extensionHint)
{
	FILE* file = fopen(fileName, "rb");
	if (file == 0)
		return 0;

	uint8_t header[16];
	bool validHeader = fread(header, 1, sizeof(header), file) == sizeof(header);
	fclose(file);
	if (!validHeader)
		return 0;

	if (header[0] == 'D' && header[1] == 'A' && header[2] == 'T' &&
		header[3] == 0 && header[4] == 0 && header[5] == 5)
	{
		uint16_t width = Read16BE(header + 6);
		uint16_t height = Read16BE(header + 8);
		uint8_t colorMode = header[14];
		if (!DMG_DAT5ModeIsDOSSupported(colorMode))
			return 0;
		if (width == 320 && height == 200)
		{
			if (colorMode == DMG_DAT5_COLORMODE_CGA)
				return DDB_DataFileMode_CGA;
			if (colorMode == DMG_DAT5_COLORMODE_EGA)
				return DDB_DataFileMode_EGA;
			return DMG_DAT5ModePlaneCount(colorMode) >= 8 ?
				DDB_DataFileMode_VGA : DDB_DataFileMode_VGA16;
		}
		if (width == 640 && height == 400)
			return DDB_DataFileMode_SHiRes;
		return 0;
	}

	const char* extension = strrchr(fileName, '.');
	if (extension != 0 && stricmp(extension, ".CGA") == 0)
		return DDB_DataFileMode_CGA;
	if (extension != 0 && stricmp(extension, ".EGA") == 0)
		return DDB_DataFileMode_EGA;

	if (!IsClassicSignature(Read16BE(header)))
		return 0;

	uint16_t storedMode = Read16BE(header + 2);
	if (storedMode == 0)
		return DDB_DataFileMode_CGA | DDB_DataFileMode_EGA | DDB_DataFileMode_VGA16;
	if (storedMode == ScreenMode_CGA || storedMode == ScreenMode_EGA ||
		storedMode == ScreenMode_HiRes || storedMode == ScreenMode_SHiRes)
		return ModeFlag((DDB_ScreenMode)storedMode);
	return ModeFlag(extensionHint == ScreenMode_Default ?
		ScreenMode_VGA16 : extensionHint);
}

static uint32_t ProbeGame(const char* ddbFileName)
{
	char baseName[128];
	char dataFileName[128];
	strncpy(baseName, ddbFileName, sizeof(baseName) - 1);
	baseName[sizeof(baseName) - 1] = 0;
	char* extension = strrchr(baseName, '.');
	if (extension != 0)
		*extension = 0;

	static const char* extensions[] = { ".DAT", ".EGA", ".CGA", ".VGA", ".SGA" };
	static const DDB_ScreenMode hints[] = {
		ScreenMode_Default, ScreenMode_EGA, ScreenMode_CGA,
		ScreenMode_VGA16, ScreenMode_SHiRes
	};

	uint32_t modes = 0;
	for (int index = 0; index < 5; index++)
	{
		strcpy(dataFileName, baseName);
		strcat(dataFileName, extensions[index]);
		modes |= ProbeDataFile(dataFileName, hints[index]);
	}
	return modes;
}

uint32_t Setup_DetectGameVideoModes()
{
	uint32_t modes = 0;
	bool foundGame = false;
	DIR* directory = opendir(".");
	if (directory == 0)
		return 0;

	struct dirent* entry;
	while ((entry = readdir(directory)) != 0)
	{
		const char* extension = strrchr(entry->d_name, '.');
		if (extension == 0 || stricmp(extension, ".DDB") != 0)
			continue;

		uint32_t partModes = ProbeGame(entry->d_name);
		if (!foundGame)
			modes = partModes;
		else
			modes &= partModes;
		foundGame = true;
	}
	closedir(directory);
	return modes;
}

#endif
