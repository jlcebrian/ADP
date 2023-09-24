#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_mem.h>

#ifdef _AMIGA

#include "video.h"

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target)
{
	// Special case: handle Amiga directly
	if (target == DDB_MACHINE_AMIGA)
	{
		uint16_t palette[16];

		File* file = File_Open(fileName, ReadOnly);
		if (!file) return false;
		uint32_t fileSize = File_GetSize(file);
		for (int n = 0; n < 16; n++) 
			VID_SetColor(n, 0);
		VID_ActivatePalette(); 
		File_Seek(file, 2);
		File_Read(file, palette, 32);
		File_Read(file, plane[0], 32000);
		File_Close(file);
	 	for (int n = 0; n < 16; n++) 
			VID_SetColor(n, palette[n]);
		VID_ActivatePalette();
		return true;
	}

	uint32_t palette[16];
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", 320*200);
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	size_t bufferSize = 32768;

	if (SCR_GetScreen(fileName, target, buffer, bufferSize, 
	                  output, 320, 200, palette))
	{
		uint8_t *in = output;

		VID_VSync();
		for (int n = 0; n < 16; n++) 
			VID_SetColor(n, 0);
		VID_ActivatePalette();

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
		
		VID_VSync();
		for (int n = 0; n < 16; n++) 
		{
			uint8_t r = (palette[n] >> 16) & 0xFF;
			uint8_t g = (palette[n] >>  8) & 0xFF;
			uint8_t b = (palette[n]      ) & 0xFF;
			VID_SetPaletteColor(n, r, g, b);
		}
		VID_ActivatePalette();
	}

	Free(buffer);
	Free(output);
	return true;
}

#endif