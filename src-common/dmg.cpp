#include <dmg.h>
#include <ddb.h>
#include <ddb_pal.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_bito.h>

#if defined(_STDCLIB) && !defined(NO_PRINTF)
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define PRINTF_WARNINGS
#endif

#define DMG_BUFFER_SIZE 	32768
#define DMG_MIN_FILE_SIZE 	0x0002C06
#define DMG_MAX_FILE_SIZE 	0x1000000

DMG* dmg = 0;

static DMG_Error dmgError = DMG_ERROR_NONE;
static void    (*dmg_warningHandler)(const char* message) = 0;
static uint8_t*  dmgTemporaryBuffer = 0;
static uint32_t  dmgTemporaryBufferSize = 0;

uint32_t DMG_GetTemporaryBufferSize()
{
	return dmgTemporaryBufferSize;
}

uint8_t* DMG_GetTemporaryBuffer(DMG_ImageMode mode)
{
	uint32_t size = 
			DMG_IS_INDEXED(mode) ? 2*DMG_BUFFER_SIZE : 
			DMG_IS_RGBA32(mode) ? 4*DMG_BUFFER_SIZE : DMG_BUFFER_SIZE;
	if (dmgTemporaryBufferSize < size && dmgTemporaryBuffer != 0)
	{
		Free(dmgTemporaryBuffer);
		dmgTemporaryBuffer = 0;
	}
	if (dmgTemporaryBuffer == 0)
	{
		dmgTemporaryBufferSize = size;
		dmgTemporaryBuffer = Allocate<uint8_t>("Decompression buffer", dmgTemporaryBufferSize);
	}
	
	return dmgTemporaryBuffer;
}
void DMG_FreeTemporaryBuffer()
{
	if (dmgTemporaryBuffer)
		Free(dmgTemporaryBuffer);
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  Error handling															 */
/* ───────────────────────────────────────────────────────────────────────── */

void DMG_SetWarningHandler(void (*handler)(const char* message))
{
	dmg_warningHandler = handler;
}

void DMG_Warning(const char* format, ...)
{
#ifdef PRINTF_WARNINGS
	if (dmg_warningHandler != 0)
	{		
#ifdef _STDCLIB
		char buffer[1024];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, 1024, format, args);
		va_end(args);
		dmg_warningHandler(buffer);
#else
		dmg_warningHandler(format);
#endif
	}
#endif
}

void DMG_SetError(DMG_Error error)
{
	dmgError = error;
}

DMG_Error DMG_GetError()
{
	return dmgError;
}

const char* DMG_GetErrorString()
{
	switch (dmgError)
	{
		case DMG_ERROR_NONE:
			return "No error";
		case DMG_ERROR_FILE_NOT_FOUND:
			return "File not found";
		case DMG_ERROR_OUT_OF_MEMORY:
			return "Out of memory";
		case DMG_ERROR_UNKNOWN_SIGNATURE:
			return "Unknown signature";
		case DMG_ERROR_READING_FILE:
			return "I/O Error reading file";
		case DMG_ERROR_SEEKING_FILE:
			return "I/O Error accessing file";
		case DMG_ERROR_WRITING_FILE:
			return "I/O Error writing file";
		case DMG_ERROR_INVALID_ENTRY_COUNT:
			return "Invalid entry count";
		case DMG_ERROR_FILE_TOO_SMALL:
			return "File too small";
		case DMG_ERROR_FILE_TOO_BIG:
			return "File too big";
		case DMG_ERROR_TRUNCATED_DATA_STREAM:
			return "Truncated data stream";
		case DMG_ERROR_DATA_STREAM_TOO_LONG:
			return "Data stream too long";
		case DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS:
			return "Data offset out of bounds";
		case DMG_ERROR_CORRUPTED_DATA_STREAM:
			return "Corrupted data stream";
		case DMG_ERROR_IMAGE_TOO_BIG:
			return "Image too big";
		case DMG_ERROR_BUFFER_TOO_SMALL:
			return "Buffer too small";
		case DMG_ERROR_ENTRY_IS_EMPTY:
			return "Entry is empty";
		case DMG_ERROR_INVALID_IMAGE:
			return "Invalid image format";
		case DMG_ERROR_DECOMPRESSION_BUFFER_MISSING:
			return "Decompression buffer missing";
		default:
			DMG_Warning("Unknown error code %d", dmgError);
			return "Unknown error";
	}
}

bool DMG_DecompressOldRLE (const uint8_t* data, uint16_t rleMask, uint16_t dataLength, uint8_t* buffer, int pixels, bool littleEndian)
{
	uint32_t nibbles = 0;
	uint8_t color = 0;
	uint8_t repetitions;
	const uint8_t* start = data;
	int nibbleCount = 0;

	uint8_t outc = 0;
	bool outcm = false;

	DMG_SetError(DMG_ERROR_NONE);

	while (pixels > 0)
	{
		if (nibbleCount == 0)
		{
			if (dataLength < 4)
			{
				if (dataLength == 0)
				{
					while (pixels-- > 0)
					{
						if (outcm)
							*buffer++ = color | (outc << 4);
						else 
							outc = color;
						outcm = !outcm;
					}
					break;
				}
				uint8_t buf[4] = { 0, 0, 0, 0 };
				uint8_t *b = buf;
				while (dataLength-- > 0)
					*b++ = *data++;
				nibbles = read32(buf, littleEndian);
			}
			else
			{
				nibbles = read32(data, littleEndian);
				data += 4;
				dataLength -= 4;
			}
			nibbleCount = 8;
		}
		
		nibbleCount--;
		color = 
			((nibbles & 0x00000080) >> 4) |
			((nibbles & 0x00008000) >> 13) |
			((nibbles & 0x00800000) >> 22) |
			((nibbles & 0x80000000) >> 31);
		nibbles <<= 1;
		
		if (outcm)
			*buffer++ = color | (outc << 4);
		else 
			outc = color;
		outcm = !outcm;

		pixels--;
		if (pixels == 0)
			break;

		if ((rleMask & (1 << color)) != 0) 
		{
			if (nibbleCount == 0)
			{
				if (dataLength < 4)
				{
					while (pixels-- > 0)
					{
						if (outcm)
							*buffer++ = color | (outc << 4);
						else 
							outc = color;
						outcm = !outcm;
					}
					break;
				}
				nibbles = read32(data, littleEndian);
				data += 4;
				dataLength -= 4;
				nibbleCount = 8;
			}

			nibbleCount--;
			repetitions = 
				((nibbles & 0x00000080) >> 4) |
				((nibbles & 0x00008000) >> 13) |
				((nibbles & 0x00800000) >> 22) |
				((nibbles & 0x80000000) >> 31);
			nibbles <<= 1;

			if (pixels < repetitions)
				repetitions = pixels;

			pixels -= repetitions;
			while (repetitions-- > 0)
			{
				if (outcm)
					*buffer++ = color | (outc << 4);
				else 
					outc = color;
				outcm = !outcm;
			}
		}
	}

	if (outcm)
		*buffer++ = (outc << 4);

	if (dataLength > 4)
		DMG_Warning("Data stream contains %d extra bytes (at offset %d)", dataLength, (int)(data - start));
	return true;
}

bool DMG_CopyImageData(uint8_t* ptr, uint16_t length, uint8_t* output, int pixels)
{
	uint32_t outputSize = (pixels + 1) & ~1;
	if (length > outputSize)
		length = outputSize;
	MemMove(output, ptr, length);
	return true;
}

uint8_t* DMG_Planar8To16(uint8_t* data, uint8_t* buffer, int length, uint32_t width)
{
	uint8_t*  ptr = (uint8_t*)data;
	uint8_t*  end = (uint8_t*)(data + length);
	uint32_t* out = (uint32_t*)buffer;

	uint32_t splitCounter = 0;
	if (width & 0xF)
		splitCounter = 1 + (width >> 4);

	while (ptr < end)
	{
		uint32_t a = 0;
		uint32_t b = 0;

		if (!--splitCounter)
		{
			splitCounter = 1 + (width >> 4);
			a |= ptr[0] << 24;
			a |= ptr[1] << 8;
			b |= ptr[2] << 24;
			b |= ptr[3] << 8;
			*out++ = a;
			*out++ = b;
			ptr   += 4;
		}
		else
		{
			a |= ptr[0] << 24;
			a |= ptr[1] << 8;
			b |= ptr[2] << 24;
			b |= ptr[3] << 8;
			a |= ptr[4] << 16;
			a |= ptr[5];
			b |= ptr[6] << 16;
			b |= ptr[7];

			*out++ = a;
			*out++ = b;
			ptr   += 8;
		}
	}
	return buffer;
}

bool DMG_Planar8ToPacked (const uint8_t* ptr, uint16_t length, uint8_t* output, int pixels, uint32_t width)
{
	const uint8_t* end = ptr + length;
	uint8_t* outputEnd = output + pixels;

	while (ptr < end-3 && output < outputEnd) {
		uint32_t word = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
		for (int n = 7; n >= 0; n--) {
			uint8_t color0 = 
				(((word >> 24) >> n) & 1) << 0 |
				(((word >> 16) >> n) & 1) << 1 |
				(((word >>  8) >> n) & 1) << 2 |
				(((word >>  0) >> n) & 1) << 3;
			n--;
			uint8_t color1 = 
				(((word >> 24) >> n) & 1) << 0 |
				(((word >> 16) >> n) & 1) << 1 |
				(((word >>  8) >> n) & 1) << 2 |
				(((word >>  0) >> n) & 1) << 3;
			*output++ = color1 | (color0 << 4);
			if (output == outputEnd) break;
		}
		ptr += 4;
	}
	return ptr == end && output == outputEnd;
}

void DMG_ConvertChunkyToPlanar(uint8_t *buffer, uint32_t bufferSize, uint32_t width)
{
	uint32_t *out = (uint32_t*)buffer;
	uint8_t *in = (uint8_t*)buffer;
	uint32_t x = 0;
	width >>= 1;
	for (unsigned n = 0; n < bufferSize; n += 8)
	{
		uint32_t p0    = 0;
		uint32_t p1    = 0;
		uint32_t mask0 = 0x00008000;
		uint32_t mask1 = 0x80000000;

		do
		{
			const uint8_t c = *in++;
			if (c & 0x10) p0 |= mask1;
			if (c & 0x20) p0 |= mask0;
			if (c & 0x40) p1 |= mask1;
			if (c & 0x80) p1 |= mask0;
			mask0 >>= 1;
			mask1 >>= 1;
			if (c & 0x01) p0 |= mask1;
			if (c & 0x02) p0 |= mask0;
			if (c & 0x04) p1 |= mask1;
			if (c & 0x08) p1 |= mask0;
			mask0 >>= 1;
			mask1 >>= 1;
			if (++x == width && mask0)
			{
				x = 0;
				n -= 4;
				break;
			}
		} while (mask0);

		*out++ = p0;
		*out++ = p1;
	}
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  Entry management														 */
/* ───────────────────────────────────────────────────────────────────────── */

DMG_Entry* DMG_GetEntry (DMG* dmg, uint8_t n)
{
	uint8_t header[6];
	uint16_t v;

	DMG_SetError(DMG_ERROR_NONE);
	if (dmg == 0)
		return 0;
	if (dmg->entries[n] == 0)
		return 0;
	if (dmg->entries[n]->type == DMGEntry_Empty)
		return dmg->entries[n];
	if (dmg->entries[n]->processed)
		return dmg->entries[n];

	if (DMG_ReadFromFile(dmg, dmg->entries[n]->fileOffset, header, 6) != 6)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return 0;
	}

	v = read16(header, dmg->littleEndian);
	dmg->entries[n]->width = v & 0x7FFF;
	dmg->entries[n]->compressed = (v & 0x8000) != 0;
	v = read16(header + 2, dmg->littleEndian);
	dmg->entries[n]->audioMode = (v & 0xC000) >> 12;
	dmg->entries[n]->height = v & 0x7FFF;
	dmg->entries[n]->length = read16(header + 4, dmg->littleEndian);

	// Actual limits should be related to screen mode, but
	// the meaning of that field is unknown
	if (dmg->entries[n]->type == DMGEntry_Image && (dmg->entries[n]->width > 1024 || dmg->entries[n]->height > 1024))
	{
		DMG_SetError(DMG_ERROR_IMAGE_TOO_BIG);
		return 0;
	}

	if (dmg->entries[n]->length + dmg->entries[n]->fileOffset + 6 > dmg->fileSize)
	{
		DMG_Warning("Entry %d: Data offset %04X-%04X out of bounds (0-%04X)", n,
			dmg->entries[n]->fileOffset,
			dmg->entries[n]->fileOffset + 6 + dmg->entries[n]->length - 1,
			dmg->fileSize);
		DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
		return 0;
	}

	dmg->entries[n]->processed = true;
	return dmg->entries[n];
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  File management														     */
/* ───────────────────────────────────────────────────────────────────────── */

bool DMG_ReadDOSEntries(DMG* dmg, DMG_Version version)
{
	int n, p;

	uint8_t* buffer = Allocate<uint8_t>("DMG temporary buffer", 2560);

	File_Seek(dmg->file, 0x06);
	if (File_Read(dmg->file, buffer, 2560) != 2560)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return false;
	}

	for (n = 0; n < 256; n++)
	{
		uint8_t* ptr     = buffer + n * 10;
		uint32_t offset  = read32(ptr, dmg->littleEndian);
		uint16_t flags   = read16(ptr + 4, dmg->littleEndian);
		int16_t  x       = (int16_t)read16(ptr + 6, dmg->littleEndian);
		int16_t  y       = (int16_t)read16(ptr + 8, dmg->littleEndian);

		if (offset != 0 && (offset < 0xA06 || offset >= dmg->fileSize))
		{
			DMG_Warning("Entry %d: Data offset %06X out of bounds (0-%06X)", n, offset, dmg->fileSize);
			DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
			Free(buffer);
			return false;
		}
		if (offset == 0)
			continue;
		
		dmg->entries[n] = Allocate<DMG_Entry>("DMG Entry");
		if (dmg->entries[n] == 0)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			Free(buffer);
			return false;
		}
		dmg->entries[n]->type = DMGEntry_Image;
		dmg->entries[n]->flags = flags;
		dmg->entries[n]->buffer = (flags & 0x0002) != 0;
		dmg->entries[n]->fixed = (flags & 0x0001) == 0;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->processed = false;

		for (p = 0; p < 16; p++) {
			if (version == DMG_Version1_EGA)
				dmg->entries[n]->RGB32Palette[p] = EGAPalette[p];
			else if (flags & 0x00800)
				dmg->entries[n]->RGB32Palette[p] = CGAPaletteRed[p];
			else
				dmg->entries[n]->RGB32Palette[p] = CGAPaletteCyan[p];
			dmg->entries[n]->EGAPalette[p] = p;
			dmg->entries[n]->CGAPalette[p] = p & 0x03;
		}
	}
	Free(buffer);
	return true;
}

bool DMG_ReadOldEntries(DMG* dmg)
{
	int n, p;

	uint8_t* buffer = Allocate<uint8_t>("DMG Temporary buffer", 44 * 256);

	File_Seek(dmg->file, 0x06);
	if (File_Read(dmg->file, buffer, 44 * 256) != 44 * 256)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return false;
	}

	for (n = 0; n < 256; n++)
	{
		uint8_t* ptr = buffer + n * 44;

		uint32_t offset     = read32(ptr, dmg->littleEndian);
		uint16_t flags      = read16(ptr + 4, dmg->littleEndian);
		int16_t  x          = (int16_t)read16(ptr + 6, dmg->littleEndian);
		int16_t  y          = (int16_t)read16(ptr + 8, dmg->littleEndian);
		uint8_t  firstColor = ptr[10];
		uint8_t  lastColor  = ptr[11];

		if (offset != 0 && (offset < 0x2C06 || offset >= dmg->fileSize))
		{
			DMG_Warning("Entry %d: Data offset %06X out of bounds (0-%06X)", n, offset, dmg->fileSize);
			DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
			Free(buffer);
			return false;
		}
		if (offset == 0)
			continue;

		dmg->entries[n] = Allocate<DMG_Entry>("DMG Entry");
		if (dmg->entries[n] == 0)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			Free(buffer);
			return false;
		}
		dmg->entries[n]->type = DMGEntry_Image;
		dmg->entries[n]->flags = flags;
		dmg->entries[n]->buffer = (flags & 0x0002) != 0;
		dmg->entries[n]->fixed = (flags & 0x0001) == 0;
		dmg->entries[n]->firstColor = firstColor;
		dmg->entries[n]->lastColor = lastColor;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->processed = false;

		if (offset == 0)
			dmg->entries[n]->type = DMGEntry_Empty;

		for (p = 0; p < 16; p++) 
		{
			uint16_t color = read16BE(ptr + 12 + p * 2);
			dmg->entries[n]->RGB32Palette[p] = Pal2RGB(color, false);
		}
	}

	Free(buffer);
	return true;
}

bool DMG_ReadNewEntries(DMG* dmg)
{
	int n, p;
	uint8_t sizeBuffer[4];
	uint32_t size;

	uint8_t* buffer = Allocate<uint8_t>("DMG Temporary buffer", 48 * 256);

	File_Seek(dmg->file, 0x06);
	if (File_Read(dmg->file, sizeBuffer, 4) != 4)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		Free(buffer);
		return false;
	}
	if (File_Read(dmg->file, buffer, 48 * 256) != 48 * 256)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		Free(buffer);
		return false;
	}
	size = read32(sizeBuffer, dmg->littleEndian);
	if (size != dmg->fileSize)
		DMG_Warning("Invalid file size %d on header (expected %d)", size, dmg->fileSize);

	for (n = 0; n < 256; n++)
	{
		uint8_t* ptr          = buffer + n * 48;
		uint32_t offset       = read32(ptr, dmg->littleEndian);
		uint16_t flags        = read16(ptr + 4, dmg->littleEndian);
		int16_t  x            = (int16_t)read16(ptr + 6, dmg->littleEndian);
		int16_t  y            = (int16_t)read16(ptr + 8, dmg->littleEndian);
		uint8_t  firstColor   = ptr[10];
		uint8_t  lastColor    = ptr[11];
		uint32_t CGAColors    = read32BE(ptr + 44);
		bool amigaPaletteHack = CGAColors == 0xDAADDAAD;

		if (offset != 0 && (offset < 0x300A || offset >= dmg->fileSize))
		{
			DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
			Free(buffer);
			return false;
		}
		if (offset == 0)
			continue;
		dmg->entries[n] = Allocate<DMG_Entry>("DMG Entry");
		if (dmg->entries[n] == 0)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			Free(buffer);
			return false;
		}
		dmg->entries[n]->type = (flags & 0x0010) ? DMGEntry_Audio : DMGEntry_Image;
		dmg->entries[n]->flags = flags;
		dmg->entries[n]->buffer = (flags & 0x0002) != 0;
		dmg->entries[n]->fixed = (flags & 0x0001) == 0;
		dmg->entries[n]->firstColor = firstColor;
		dmg->entries[n]->lastColor = lastColor;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->processed = false;

		if (offset == 0)
			dmg->entries[n]->type = DMGEntry_Empty;

		for (p = 0; p < 16; p++) 
		{
			uint16_t color = read16BE(ptr + 12 + p * 2);
			dmg->entries[n]->RGB32Palette[p] = Pal2RGB(color, amigaPaletteHack);
			dmg->entries[n]->EGAPalette[p] = amigaPaletteHack ? 0 : ((color >> 12) & 0x0F);
			dmg->entries[n]->CGAPalette[p] = amigaPaletteHack ? 0 : ((CGAColors >> (2*p)) & 0x03);
		}
		dmg->entries[n]->amigaPaletteHack = amigaPaletteHack;
		dmg->entries[n]->CGAMode = (flags & 0x0001) ? CGA_Red : CGA_Blue;
	}

	Free(buffer);
	return true;
}

int DMG_GetEntryCount(DMG* dmg)
{
	int n, i;
	int count = 0;

	uint32_t found[256];

	if (dmg == 0)
		return 0;
	for (n = 0; n < 256; n++)
	{
		if (dmg->entries[n] != 0 && dmg->entries[n]->type != DMGEntry_Empty)
		{
			uint32_t offset = dmg->entries[n]->fileOffset;
			bool repeated = false;
			for (i = 0; i < count; i++)
			{
				if (found[i] == offset)
				{
					repeated = true;
					break;
				}
			}
			if (!repeated)
				found[count++] = offset;
		}
	}
	return count;
}

DMG* DMG_Open(const char* filename, bool readOnly)
{
	DMG* d;
	File* file;
	uint8_t header[16];
	uint16_t signature;
	uint16_t entryCount;
	uint16_t realEntryCount;
	bool success;
	const char* extension = StrRChr(filename, '.');

	DMG_SetError(DMG_ERROR_NONE);
	
	file = File_Open(filename, readOnly ? ReadOnly : ReadWrite);
	if (file == 0)
	{
		DebugPrintf("Unable to open %s\n", filename);
		DMG_SetError(DMG_ERROR_FILE_NOT_FOUND);
		return 0;
	}

	size_t fileSize = File_GetSize(file);
	if (fileSize < DMG_MIN_FILE_SIZE)
	{
		DMG_SetError(DMG_ERROR_FILE_TOO_SMALL);
		File_Close(file);
		return 0;
	}
	if (fileSize > DMG_MAX_FILE_SIZE)
	{
		DMG_SetError(DMG_ERROR_FILE_TOO_BIG);
		File_Close(file);
		return 0;
	}

	d = Allocate<DMG>("DMG");
	if (d == 0)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		File_Close(file);
		return 0;
	}
	d->file = file;
	d->fileSize = (int)fileSize;

	// Read file header
	MemClear(header, sizeof(header));
	if (File_Read(file, header, 10) != 10)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		DMG_Close(d);
		return 0;
	}
	signature = read16BE(header);

	switch (signature)
	{
		case 0x0004:
			// Old version DAT file, big endian
			d->version = DMG_Version1;
			success = DMG_ReadOldEntries(d);
			break;

		case 0x0300:
			// New version DAT file, big endian
			d->version = DMG_Version2;
			d->littleEndian = false;
			d->screenMode = read16BE(header + 2);
			success = DMG_ReadNewEntries(d);
			break;

		case 0xFFFF:
			// New version DAT file, little endian
			d->version = DMG_Version2;
			d->littleEndian = true;
			d->screenMode = read16BE(header + 2);
			success = DMG_ReadNewEntries(d);
			break;

		case 0x0000:
			if (StrIComp(extension, ".ega") == 0)
			{
				d->version = DMG_Version1_EGA;
				d->littleEndian = true;
				d->screenMode = ScreenMode_EGA;
				success = DMG_ReadDOSEntries(d, DMG_Version1_EGA);
				break;
			}
			else if (StrIComp(extension, ".cga") == 0)
			{
				d->version = DMG_Version1_CGA;
				d->littleEndian = true;
				d->screenMode = ScreenMode_CGA;
				success = DMG_ReadDOSEntries(d, DMG_Version1_CGA);
				break;
			}
			// Fall through

		default:
			DMG_Warning("Unknown signature: %04X", signature);
			DMG_SetError(DMG_ERROR_UNKNOWN_SIGNATURE);
			success = false;
			break;
	}

	if (success == false)
	{
		DMG_Close(d);
		return 0;
	}

	entryCount = read16(header + 4, d->littleEndian);
	realEntryCount = DMG_GetEntryCount(d);
	if (entryCount != realEntryCount)
		DMG_Warning("Invalid entry count %d on file header (expected %d)", entryCount, realEntryCount);

	if (dmg == 0)
		dmg = d;
	return d;
}

uint32_t* DMG_GetEntryPalette(DMG* dmg, uint8_t index, DMG_ImageMode mode)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	if (entry == 0)
		return 0;
	if (DMG_IS_CGA(mode))
		return entry->CGAMode == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
	else if (DMG_IS_EGA(mode))
		return EGAPalette;
	else
		return entry->RGB32Palette;
}

void DMG_Close(DMG* d)
{
#ifndef NO_CACHE
	DMG_FreeImageCache(d);
#endif

	File_Close(d->file);
	Free(d);

	if (dmg == d)
		dmg = 0;
}

uint32_t DMG_ReadFromFile (DMG* dmg, uint32_t offset, void* buffer, uint32_t size)
{
	uint32_t total = 0;
	
	if (dmg->fileCacheBlockSize == 0 || offset < dmg->fileCacheOffset)
	{
		if (!File_Seek(dmg->file, offset))
			return 0;
		return File_Read(dmg->file, buffer, size);
	}

	offset -= dmg->fileCacheOffset;

	while (size > 0)
	{
		int block = offset / dmg->fileCacheBlockSize;
		uint32_t skip  = offset % dmg->fileCacheBlockSize;
		uint32_t count = dmg->fileCacheBlockSize - skip;
		if (block >= DMG_CACHE_BLOCKS)
			break;
		if (dmg->fileCacheBlocks[block] == 0)
			break;

		if (count > size)
			count = size;

		MemCopy (buffer, dmg->fileCacheBlocks[block] + skip, count);
		size -= count;
		total += count;
		if (size == 0)
			return total;

		offset += count;
		buffer = (uint8_t*)buffer + count;
	}
	
	if (!File_Seek(dmg->file, offset + dmg->fileCacheOffset))
		return 0;
	return total + File_Read(dmg->file, buffer, size);
}

uint32_t DMG_CalculateRequiredSize (DMG_Entry* entry, DMG_ImageMode mode)
{
	uint16_t width  = entry->width;
	uint16_t height = entry->height;

	switch (mode)
	{
		case ImageMode_Packed:
		case ImageMode_PackedEGA:
		case ImageMode_PackedCGA:
			return width * height / 2;

		case ImageMode_PlanarST:
			return ((width + 15) & ~15) * height / 2;

		case ImageMode_RGBA32:
		case ImageMode_RGBA32EGA:
		case ImageMode_RGBA32CGA:
			return width * height * 4;

		case ImageMode_Indexed:
		case ImageMode_IndexedCGA:
		case ImageMode_IndexedEGA:
			return width * height;

		case ImageMode_Audio:
			if (entry->type == DMGEntry_Audio)
				return entry->length;
			else if (dmg->screenMode == ScreenMode_CGA)
				return width * height / 4;
			return width * height / 2;

		default:
			return width * height / 2;
	}
}