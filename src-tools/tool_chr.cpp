#include <img.h>
#include <ddb.h>
#include <os_file.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

static char newFileName[1024 + 16];

static uint8_t charset[2048];
static uint8_t indexed[128 * 128];
static uint32_t palette[16] = { 0xFF000000, 0xFFFFFFFF };

void TracePrintf(const char* format, ...)
{
}

static void ShowHelp()
{
	printf("CHR Character Set utility for DAAD " VERSION_STR "\n\n");
	printf("Converts between DAAD character sets and PNG files.\n\n");
	printf("Usage: chr <input.png> [<output.chr>]\n");
	printf("       chr <input.chr> [<output.png>]\n");
}

static const char* ChangeExtension(const char* fileName, const char* extension)
{
	strncpy(newFileName, fileName, 1024);
	char* ptr = strrchr(newFileName, '.');
	if (ptr == NULL)
		ptr = newFileName + strlen(newFileName);
	strcpy(ptr, extension);
	return newFileName;
}

void ConvertCharsetToIndexed (uint8_t* charset, uint8_t* pixels)
{
	for (int i = 0; i < 256; i++)
	{
		int destx = (i & 0x0F) * 8;
		int desty = (i >> 4) * 8;

		uint8_t* src = charset + i * 8;
		uint8_t* dst = pixels + destx + desty * 128;
		for (int y = 0; y < 8; y++)
		{
			uint8_t b = *src++;
			for (int x = 0; x < 8; x++)
			{
				dst[x] = (b & 0x80) ? 1 : 0;
				b <<= 1;
			}
			dst += 128;
		}
	}
}

static void ConvertIndexedToCharset (uint8_t* pixels, uint8_t* charset)
{
	for (int i = 0; i < 256; i++)
	{
		int srcx = (i & 0x0F) * 8;
		int srcy = (i >> 4) * 8;

		uint8_t* src = pixels + srcx + srcy * 128;
		uint8_t* dst = charset + i * 8;
		for (int y = 0; y < 8; y++)
		{
			uint8_t b = 0;
			for (int x = 0; x < 8; x++)
			{
				b <<= 1;
				b |= *src++ & 1;
			}
			*dst++ = b;
			src += 128 - 8;
		}
	}
}

int main (int argc, char *argv[])
{
	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	File* file = File_Open(argv[1]);
	if (file == NULL)
	{
		printf("Error: File not found: %s\n", argv[1]);
		return 1;
	}

	uint8_t header[8];
	if (File_Read(file, header, 8) != 8)
	{
		printf("Error: unable to read from file: %s\n", argv[1]);
		File_Close(file);
		return 1;
	}

	if (   header[0] == 0x89 
		&& header[1] == 0x50 
		&& header[2] == 0x4E 
		&& header[3] == 0x47 
		&& header[4] == 0x0D 
		&& header[5] == 0x0A 
		&& header[6] == 0x1A 
		&& header[7] == 0x0A)		// PNG signature
	{
		File_Close(file);

		uint16_t width, height;

		if (!LoadPNGIndexed16(argv[1], indexed, 128 * 128, &width, &height, palette))
		{
			printf("Error: unable to read from file: %s\n", argv[1]);
			return 1;
		}
		if (width != 128 || height != 128)
		{
			printf("Error: file is not a 128x128 PNG: %s\n", argv[1]);
			return 1;
		}
		ConvertIndexedToCharset(indexed, charset);

		const char* outputFileName = (argc > 2 ? argv[2] : ChangeExtension(argv[1], ".chr"));
		file = File_Create(outputFileName);
		if (file == NULL)
		{
			printf("Error: unable to write to file: %s\n", outputFileName);
			return 1;
		}

		char header[128];
		memset(header, 0, 128);
		
		// Header seem to come from AMSDOS, but it has some weird stuff inside
		strncpy(header+1, argv[1], 8);
		for (char* ptr = header+1; ptr < header+9; ptr++)
		{
			*ptr = toupper(*ptr);
			if (*ptr == '.' || *ptr == 0)
			{
				*ptr = ' ';
				while (ptr < header+9)
					*++ptr = ' ';
			}
		}
		strncpy(header+9, "CHR", 3);
		header[0x12] = 2;
		header[0x41] = 8;
		header[0x43] = 0x24;
		header[0x44] = 0x02;
		if (File_Write(file, header, 128) != 128)
		{
			printf("Error: unable to write to file: %s\n", outputFileName);
			File_Close(file);
			return 1;
		}
		
		if (File_Write(file, charset, 2048) != 2048)
		{
			printf("Error: unable to write to file: %s\n", outputFileName);
			File_Close(file);
			return 1;
		}
		File_Close(file);
		printf("%s written.\n", outputFileName);
	}
	else
	{
		uint64_t size = File_GetSize(file);
		if (size != 2176)
		{
			printf("Error: File is not a PNG or DAAD CHR file: %s\n", argv[1]);
			File_Close(file);
			return 1;
		}

		File_Seek(file, 128);
		if (File_Read(file, charset, 2048) != 2048)
		{
			printf("Error: unable to read from file: %s\n", argv[1]);
			File_Close(file);
			return 1;
		}
		ConvertCharsetToIndexed(charset, indexed);

		const char* outputFileName = (argc > 2 ? argv[2] : ChangeExtension(argv[1], ".png"));
		if (!SavePNGIndexed16(outputFileName, indexed, 128, 128, palette))
		{
			printf("Error: unable to write to file: %s\n", outputFileName);
			File_Close(file);
			return 1;
		}
		printf("%s written.\n", outputFileName);
	}

	File_Close(file);

	return 0;
}