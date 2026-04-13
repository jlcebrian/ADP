#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_mem.h>

#ifdef _AMIGA

#include "video.h"

static const int fadeSteps = 8;
static const uint16_t fadeScale[fadeSteps] = { 256, 219, 183, 146, 110, 73, 37, 0 };

static void FadeInPalette(const uint32_t* targetPalette, uint16_t count)
{
	uint32_t palette[256];
	for (int frame = 1; frame < fadeSteps; frame++)
	{
		uint16_t scale = fadeScale[fadeSteps - 1 - frame];
		for (uint16_t n = 0; n < count; n++)
		{
			uint32_t v0 = ((targetPalette[n] & 0xFF00FFUL) * scale) >> 8;
			uint32_t v1 = ((targetPalette[n] & 0x00FF00UL) * scale) >> 8;
			palette[n] = (v0 & 0xFF00FFUL) | (v1 & 0x00FF00UL);
		}
		VID_SetPaletteEntries(palette, count, 0, false, true);
	}
	VID_SetPaletteEntries(targetPalette, count, 0, false, true);
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	// Special case: handle Amiga directly
	if (target == DDB_MACHINE_AMIGA)
	{
		uint16_t palette[16];
		uint32_t targetPalette[16];
		uint32_t blackPalette[16] = { 0 };

		File* file = File_Open(fileName, ReadOnly);
		if (!file) return false;
		uint32_t fileSize = File_GetSize(file);
		if (fadeIn)
			VID_SetPaletteEntries(blackPalette, 16, 0, true, true);
		File_Seek(file, 2);
		File_Read(file, palette, 32);
		File_Read(file, plane[0], 32000);
		File_Close(file);
		for (int n = 0; n < 16; n++)
		{
			uint16_t c = palette[n];
			targetPalette[n] =
				((uint32_t)((c >> 8) & 0x0F) * 0x11 << 16) |
				((uint32_t)((c >> 4) & 0x0F) * 0x11 << 8) |
				(uint32_t)(c & 0x0F) * 0x11;
		}
		if (fadeIn)
			FadeInPalette(targetPalette, 16);
		else
			VID_SetPaletteEntries(targetPalette, 16, 0, false, true);
		return true;
	}

	uint32_t palette[16];
	uint32_t blackPalette[16] = { 0 };
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", 320*200);
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	size_t bufferSize = 32768;

	if (SCR_GetScreen(fileName, target, buffer, bufferSize, 
	                  output, 320, 200, palette))
	{
		uint8_t *in = output;

		if (fadeIn)
			VID_SetPaletteRange(blackPalette, 16, 0, true, true);

		for (unsigned offset = 0; offset < 200*SCR_STRIDEB; offset += SCR_STRIDEB)
		{
			uint16_t *out0 = (uint16_t*)(plane[0] + offset);
			uint16_t *out1 = (uint16_t*)(plane[1] + offset);
			uint16_t *out2 = (uint16_t*)(plane[2] + offset);
			uint16_t *out3 = (uint16_t*)(plane[3] + offset);
			for (int x = 0; x < 320; x += 16)
			{
				uint32_t p0 = 0;
				uint32_t p1 = 0;
				uint32_t mask0 = 0x00008000;
				uint32_t mask1 = 0x80000000;

				do
				{
					const uint8_t  c = *in++;
					if (c & 0x01) p0 |= mask1;
					if (c & 0x02) p0 |= mask0;
					if (c & 0x04) p1 |= mask1;
					if (c & 0x08) p1 |= mask0;
					mask0 >>= 1;
					mask1 >>= 1;
				}
				while (mask0);

				*out0++ = p0 >> 16;
				*out1++ = p0;
				*out2++ = p1 >> 16;
				*out3++ = p1;
			}
		}
		
		if (fadeIn)
			FadeInPalette(palette, 16);
		else
			VID_SetPaletteRange(palette, 16, 0, false, true);
	}

	Free(buffer);
	Free(output);
	return true;
}

#endif