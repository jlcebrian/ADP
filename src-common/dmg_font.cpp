#include <dmg_font.h>
#include <os_file.h>
#include <os_lib.h>

static const uint8_t SintacFontHeader[16] = {
	'J', 'S', 'J', ' ', 'S', 'I', 'N', 'T', 'A', 'C', ' ', 'F', 'N', 'T', '3', 0
};

static bool ReadExact(File* file, void* buffer, uint64_t size)
{
	return File_Read(file, buffer, size) == size;
}

static bool WriteExact(File* file, const void* buffer, uint64_t size)
{
	return File_Write(file, buffer, size) == size;
}

bool DMG_ReadSINTACFont(const char* fileName, DMG_Font* font)
{
	if (font == 0)
		return false;

	File* file = File_Open(fileName);
	if (file == 0)
		return false;

	if (File_GetSize(file) != 6672)
	{
		File_Close(file);
		return false;
	}

	uint8_t header[16];
	bool ok =
		ReadExact(file, header, sizeof(header)) &&
		MemComp(header, (void*)SintacFontHeader, sizeof(header)) == 0 &&
		ReadExact(file, font->width16, sizeof(font->width16)) &&
		ReadExact(file, font->bitmap16, sizeof(font->bitmap16)) &&
		ReadExact(file, font->width8, sizeof(font->width8)) &&
		ReadExact(file, font->bitmap8, sizeof(font->bitmap8));

	File_Close(file);
	return ok;
}

bool DMG_WriteSINTACFont(const char* fileName, const DMG_Font* font)
{
	if (font == 0)
		return false;

	File* file = File_Create(fileName);
	if (file == 0)
		return false;

	bool ok =
		WriteExact(file, SintacFontHeader, sizeof(SintacFontHeader)) &&
		WriteExact(file, font->width16, sizeof(font->width16)) &&
		WriteExact(file, font->bitmap16, sizeof(font->bitmap16)) &&
		WriteExact(file, font->width8, sizeof(font->width8)) &&
		WriteExact(file, font->bitmap8, sizeof(font->bitmap8));

	File_Close(file);
	return ok;
}