#include <ddb.h>
#include <ddb_pal.h>
#include <os_file.h>

bool SCR_GetScreen (const char* fileName, DDB_Machine target, 
                    uint8_t* buffer, size_t bufferSize, 
                    uint8_t* output, int width, int height, uint32_t* palette)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}

	uint64_t size = File_GetSize(file);
	if (target == DDB_MACHINE_SPECTRUM)
	{
		if (size < 6912 || size > 7040)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			File_Close(file);
			return false;
		}
	}
	else if (size < 16384 || size > 32768)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		File_Close(file);
		return false;
	}

	if (bufferSize < size)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		File_Close(file);
		return false;
	}

	File_Read(file, buffer, size);
	File_Close(file);

	if (target == DDB_MACHINE_SPECTRUM)
	{
		uint64_t logicalSize = size;
		while (logicalSize > 6912 && (buffer[logicalSize - 1] == 0x00 || buffer[logicalSize - 1] == 0xE5))
			logicalSize--;
		if (logicalSize != 6912)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		for (int n = 0; n < 16; n++)
			palette[n] = ZXSpectrumPalette[n];

		for (int y = 0; y < 192 && y < height; y++)
		{
			uint32_t bitmapOffset = ((uint32_t)(y & 0xC0) << 5) |
			                      ((uint32_t)(y & 0x07) << 8) |
			                      ((uint32_t)(y & 0x38) << 2);
			uint32_t attrOffset = 6144 + ((uint32_t)(y >> 3) * 32);
			uint8_t* ptr = output + y * width;

			for (int x = 0; x < 32 && x * 8 < width; x++)
			{
				uint8_t bits = buffer[bitmapOffset + x];
				uint8_t attr = buffer[attrOffset + x];
				uint8_t bright = (attr & 0x40) ? 8 : 0;
				uint8_t ink = bright | (attr & 0x07);
				uint8_t paper = bright | ((attr >> 3) & 0x07);

				for (int bit = 0; bit < 8 && x * 8 + bit < width; bit++)
					*ptr++ = (bits & (0x80 >> bit)) ? ink : paper;
			}
		}
		return true;
	}

	if (target == DDB_MACHINE_ATARIST)
	{
		// PI1 file
		uint8_t* filePalette = buffer + 2;
		for (int n = 0; n < 16; n++) {			
			palette[n] = Pal2RGB((filePalette[n * 2] << 8) + filePalette[n * 2 + 1], false);
		}
		for (int y = 0 ; y < 200 && y < height; y++)
		{
			uint8_t* row = buffer + 34 + y * 160;
			uint8_t* ptr = output + y * width;
			for (int x = 0; x < 160; x += 8)
			{
				uint16_t bits0 = (row[x+0] << 8) | row[x+1];
				uint16_t bits1 = (row[x+2] << 8) | row[x+3];
				uint16_t bits2 = (row[x+4] << 8) | row[x+5];
				uint16_t bits3 = (row[x+6] << 8) | row[x+7];
				
				int mask = 0x8000;
				do
				{
					int color = ((bits0 & mask) ? 0x01 : 0x00);
					if (bits1 & mask) color |= 0x02;
					if (bits2 & mask) color |= 0x04;
					if (bits3 & mask) color |= 0x08;
					*ptr++ = color;
					mask >>= 1;
				}
				while (mask);
			}
		}
	}
	else if (target == DDB_MACHINE_AMIGA)
	{
		// Amiga SCR file
		uint8_t* filePalette = buffer + 2;
		for (int n = 0; n < 16; n++) {
			uint16_t c = (filePalette[n * 2] << 8) + filePalette[n * 2 + 1];
			palette[n] = 0xFF000000UL |
				((uint32_t)(c & 0xF00) << 12) |
				((uint32_t)(c & 0x0F0) << 8) |
				((uint32_t)(c & 0x00F) << 4);
		}
		for (int y = 0 ; y < 200 && y < height; y++)
		{
			uint8_t* row = buffer + 34 + y * 40;
			uint8_t* ptr = output + y * width;
			for (int x = 0; x < 40; x++)
			{
				uint16_t bits0 = row[x+0];
				uint16_t bits1 = row[x+8000];
				uint16_t bits2 = row[x+16000];
				uint16_t bits3 = row[x+24000];
				
				for (int b = 0; b < 8; b++)
				{
					int mask = 0x80 >> b;
					int color = ((bits0 & mask) ? 0x01 : 0x00) |
								((bits1 & mask) ? 0x02 : 0x00) |
								((bits2 & mask) ? 0x04 : 0x00) |
								((bits3 & mask) ? 0x08 : 0x00);
					*ptr++ = color;
				}
			}
		}
	}
	else if (target == DDB_MACHINE_IBMPC)
	{
		if (size <= 16384)
		{
			// CGA
			for (int n = 0; n < 16; n++) {
				palette[n] = CGAPaletteCyan[n & 3];
			}

			for (int y = 0 ; y < 200 && y < height; y++)
			{
				uint8_t* row = buffer + 80*(y >> 1) + 8192*(y & 1);
				uint8_t* ptr = output + y * width;
				for (int x = 0; x < 320; x++)
				{
					uint8_t c = row[x >> 2];
					int rot = 6 - ((x & 3) << 1);
					*ptr++ = (c >> rot) & 0x03;
				}
			}
		}
		else
		{
			uint8_t* data = buffer + 1;
			if (size <= 32048)
			{
				// EGA
				for (int n = 0; n < 16; n++) {	
					palette[n] = EGAPalette[n];
				}
			}
			else
			{
				// VGA
				uint8_t* filePalette = buffer + 1;
				data = buffer + 49;
				for (int n = 0; n < 16; n++) {
					uint16_t r = filePalette[n * 3];
					uint16_t g = filePalette[n * 3 + 1];
					uint16_t b = filePalette[n * 3 + 2];
					r = (r << 2) | (r & 0x03);
					g = (g << 2) | (g & 0x03);
					b = (b << 2) | (b & 0x03);
					palette[n] = 0xFF000000UL |
						((uint32_t)r << 16) |
						((uint32_t)g << 8) |
						(uint32_t)b;
				}
			}

			for (int y = 0 ; y < 200 && y < height; y++)
			{
				uint8_t* row = data + 40*y;
				int bitPlaneStride = 8000;
				for (int x = 0; x < 320; x++)
				{
					int offset = x >> 3;
					int mask = 0x80 >> (x & 7);
					uint8_t color = 
						((row[offset                   ] & mask) != 0 ? 0x01 : 0) |
						((row[offset + bitPlaneStride  ] & mask) != 0 ? 0x02 : 0) |
						((row[offset + bitPlaneStride*2] & mask) != 0 ? 0x04 : 0) |
						((row[offset + bitPlaneStride*3] & mask) != 0 ? 0x08 : 0);
					*output++ = color;
				}
			}
		}
	}

	return true;
}
