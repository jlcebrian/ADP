#include <ddb.h>
#include <dmg.h>
#include <os_bito.h>
#include <os_mem.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ───────────────────────────────────────────────────────────────────────── */
/*  DAT File manipulation												     */
/* ───────────────────────────────────────────────────────────────────────── */

static void DMG_TruncateFile(DMG* dmg, uint32_t size)
{
	File_Truncate(dmg->file, size);
	dmg->fileSize = size;
}

static bool DMG_UpdateOffsets (DMG* dmg, uint32_t from, uint32_t to, bool add, int32_t value)
{
	int n;
	int changes = 0;
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);
	int offset = dmg->version == DMG_Version1 ? 0x06 : 0x0A;
	int size = dmg->version == DMG_Version1 ? 44 : 48;

	if (!File_Seek(dmg->file, offset))
	{
		DMG_SetError(DMG_ERROR_SEEKING_FILE);
		return false;
	}
	if (File_Read(dmg->file, buffer, size * 256) != size * 256)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return false;
	}

	for (n = 0; n < 256; n++)
	{
		uint32_t offset = read32(buffer + n * size, dmg->littleEndian);
		if (offset >= from && offset < to)
		{
			if (add)
				offset += value;
			else
				offset = value;
			write32(buffer + n * size, offset, dmg->littleEndian);
			changes++;
		}
	}
	if (changes == 0)
		return true;

	if (!File_Seek(dmg->file, offset))
	{
		DMG_SetError(DMG_ERROR_SEEKING_FILE);
		return false;
	}
	if (File_Write(dmg->file, buffer, size * 256) != size * 256)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	for (n = 0; n < 256; n++)
	{
		uint32_t offset = read32(buffer + n * size, dmg->littleEndian);
		if (dmg->entries[n] != 0)
			dmg->entries[n]->fileOffset = offset;
	}
	return true;
}

static bool DMG_AddValueToOffsets (DMG* dmg, uint32_t from, uint32_t to, int32_t value)
{
	return DMG_UpdateOffsets(dmg, from, to, true, value);
}

static bool DMG_SetOffsets (DMG* dmg, uint32_t from, uint32_t to, int32_t value)
{
	return DMG_UpdateOffsets(dmg, from, to, false, value);
}

static bool DMG_RemoveBlock (DMG* dmg, uint32_t offset, uint32_t size)
{
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);
	uint32_t bufferSize = DMG_GetTemporaryBufferSize();
	uint32_t remaining = dmg->fileSize - offset;
	uint32_t start = offset;

	if (remaining <= size)
	{
		DMG_TruncateFile(dmg, offset);
		DMG_SetOffsets(dmg, offset, dmg->fileSize, offset);
		return true;
	}

	remaining -= size;

	while (remaining > 0)
	{
		uint32_t chunkSize = remaining > bufferSize ? bufferSize : remaining;
		uint32_t chunkOffset = offset + size;

		if (!File_Seek(dmg->file, chunkOffset + size))
		{
			DMG_SetError(DMG_ERROR_SEEKING_FILE);
			return false;
		}
		if (File_Read(dmg->file, buffer, chunkSize) != chunkSize)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return false;
		}
		if (!File_Seek(dmg->file, offset))
		{
			DMG_SetError(DMG_ERROR_SEEKING_FILE);
			return false;
		}
		if (File_Write(dmg->file, buffer, chunkSize) != chunkSize)
		{
			DMG_SetError(DMG_ERROR_WRITING_FILE);
			return false;
		}
		remaining -= chunkSize;
		offset += chunkSize;
	}

	DMG_TruncateFile(dmg, offset);
	DMG_AddValueToOffsets(dmg, start, dmg->fileSize, -size);
	dmg->fileSize -= size;
	return true;
}

static bool DMG_InsertBlock (DMG* dmg, uint32_t offset, uint32_t size)
{
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);
	uint32_t bufferSize = DMG_GetTemporaryBufferSize();
	uint32_t remaining = dmg->fileSize - offset;

	if (bufferSize < size)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return false;
	}

	while (remaining > 0)
	{
		uint32_t chunkSize = remaining > bufferSize ? bufferSize : remaining;
		uint32_t chunkOffset = offset + remaining - chunkSize;

		if (!File_Seek(dmg->file, chunkOffset))
		{
			DMG_SetError(DMG_ERROR_SEEKING_FILE);
			return false;
		}
		if (File_Read(dmg->file, buffer, chunkSize) != chunkSize)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return false;
		}
		if (File_Write(dmg->file, buffer, chunkSize) != chunkSize)
		{
			DMG_SetError(DMG_ERROR_WRITING_FILE);
			return false;
		}
		remaining -= chunkSize;
	}

	dmg->fileSize += size;
	return DMG_AddValueToOffsets(dmg, offset, dmg->fileSize, size);
}

static void WritePalette(uint8_t* ptr, DMG_Entry* entry)
{
	int n;
	for (n = 0; n < 16; n++)
	{
		uint32_t color = entry->RGB32Palette[n];
		uint8_t e = entry->EGAPalette[n] & 0x0F;
		uint8_t r = (color >> 20) & 0x0F;
		uint8_t g = (color >> 12) & 0x0F;
		uint8_t b = (color >>  4) & 0x0F;

		if ((entry->flags & DMG_FLAG_AMIPALHACK) == 0)
		{
			b = (b >> 1) | ((b << 3) & 0x08);
			g = (g >> 1) | ((g << 3) & 0x08);
			r = (r >> 1) | ((r << 3) & 0x08);
		}
		ptr[0] = (e << 4) | r;
		ptr[1] = (g << 4) | b;
		ptr += 2;
	}
}

bool DMG_UpdateFileHeader (DMG* dmg)
{
	uint8_t header[6];
	int size = dmg->version == DMG_Version1 ? 2 : 6;
	write16(header + 0x00, DMG_GetEntryCount(dmg), false);
	write32(header + 0x02, dmg->fileSize, false);
	if (!File_Seek(dmg->file, 0x04))
	{
		DMG_SetError(DMG_ERROR_SEEKING_FILE);
		return false;
	}
	if (File_Write(dmg->file, header, 6) != 6)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	return true;
}

bool DMG_UpdateEntry (DMG* dmg, uint8_t index)
{
	int n;
	int size = dmg->version == DMG_Version1 ? 44 : 48;
	int offset = (dmg->version == DMG_Version1 ? 0x06 : 0x0A) + index * size;
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);

	DMG_Entry* entry = dmg->entries[index];
	if (entry == 0)
		return false;

	bool littleEndian = dmg->littleEndian;
	int entryCount = DMG_GetEntryCount(dmg);

	uint16_t flags = 0x0000;
	if (dmg->version == DMG_Version1)
	{
		if (entry->flags & DMG_FLAG_FIXED)
			flags |= 0x01;
		if (entry->flags & DMG_FLAG_BUFFERED)
			flags |= 0x02;
	}
	else
	{
		if (entry->flags & DMG_FLAG_FIXED)
			flags |= 0x04;
		if (entry->flags & DMG_FLAG_BUFFERED)
			flags |= 0x02;
		if (DMG_GetCGAMode(entry) == CGA_Red)
			flags |= 0x01;
		if (entry->type == DMGEntry_Audio)
			flags |= 0x10;
	}

	write32(buffer + 0x00, entry->fileOffset, littleEndian);
	write16(buffer + 0x04, flags, littleEndian);
	write16(buffer + 0x06, entry->x, littleEndian);
	write16(buffer + 0x08, entry->y, littleEndian);
	*(buffer + 0x0A) = entry->firstColor;
	*(buffer + 0x0B) = entry->lastColor;
	WritePalette(buffer + 0x0C, entry);

	if (dmg->version != DMG_Version1)
	{
		if ((entry->flags & DMG_FLAG_AMIPALHACK) != 0)
		{
			write32(buffer + 0x2C, 0xDAADDAAD, littleEndian);
		}
		else
		{
			uint32_t cgaPalette = 0;
			for (n = 0; n < 16; n++)
				cgaPalette |= (entry->CGAPalette[n] & 0x03) << (n * 2);
			write32(buffer + 0x2C, cgaPalette, littleEndian);
		}
	}

	if (!File_Seek(dmg->file, offset))
	{
		DMG_SetError(DMG_ERROR_SEEKING_FILE);
		return false;
	}
	if (File_Write(dmg->file, buffer, size) != size)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	return DMG_UpdateFileHeader(dmg);
}

bool DMG_IsBlockInUse(DMG* dmg, uint32_t offset, uint32_t size)
{
	int n;

	for (n = 0; n < 256; n++)
	{
		DMG_Entry* entry = DMG_GetEntry(dmg, n);
		if (entry->type == DMGEntry_Empty)
			continue;
		if (entry->fileOffset < offset+size && entry->fileOffset + entry->length > offset)
			return true;
	}
	return false;
}

bool DMG_IsBlockShared(DMG* dmg, uint8_t index)
{
	DMG_Entry* original = DMG_GetEntry(dmg, index);
	int n;
	uint32_t offset = original->fileOffset;
	uint32_t size = original->length + 6;

	for (n = 0; n < 256; n++)
	{
		DMG_Entry* entry = DMG_GetEntry(dmg, n);
		if (n == index)
			continue;
		if (entry->type == DMGEntry_Empty)
			continue;
		if (entry->fileOffset < offset+size && entry->fileOffset + entry->length > offset)
			return true;
	}
	return false;
}

bool DMG_RemoveEntry(DMG* dmg, uint8_t index)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, index);

	if (entry == NULL)
		return false;

	if (entry->type != DMGEntry_Empty)
	{
		uint32_t offset = entry->fileOffset;
		uint32_t size = entry->length + 6;
		entry->fileOffset = 0;
		entry->type = DMGEntry_Empty;
		if (size > 0 && !DMG_IsBlockInUse(dmg, offset, size))
			DMG_RemoveBlock(dmg, offset, size);
		DMG_UpdateEntry(dmg, index);
		return true;
	}

	DMG_SetError(DMG_ERROR_ENTRY_IS_EMPTY);
	return true;
}

uint8_t DMG_FindFreeEntry (DMG* dmg)
{
	int n;

	for (n = 0; n < 256; n++)
	{
		DMG_Entry* entry = DMG_GetEntry(dmg, n);
		if (entry->type == DMGEntry_Empty)
			return n;
	}
	DMG_Warning("No free entries remaining in DMG");
	return 255;
}

bool DMG_SetEntryPalette (DMG* dmg, uint8_t index, uint32_t* palette)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	int n;

	if (entry == NULL)
	{
		if (dmg->entries[index] == NULL)
		{
			dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
			if (dmg->entries[index] == NULL)
			{
				DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
				return false;
			}
			entry = dmg->entries[index];
			entry->type = DMGEntry_Empty;
		}
		else
		{
			return false;
		}
	}

	for (n = 0; n < 16; n++)
		entry->RGB32Palette[n] = palette[n];
	return DMG_UpdateEntry(dmg, index);
}

bool DMG_SetAudioData (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t size, DMG_KHZ freq)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	uint8_t header[6];

	if (entry->type != DMGEntry_Empty)
	{
		if (!DMG_RemoveEntry(dmg, index))
			return false;
	}

	File_Seek(dmg->file, dmg->fileSize);
	write16(header + 0x00, 0, dmg->littleEndian);
	write16(header + 0x02, 0x8000, dmg->littleEndian);
	write16(header + 0x04, size, dmg->littleEndian);

	if (File_Write(dmg->file, header, 6) != 6)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	if (File_Write(dmg->file, buffer, size) != size)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}

	entry->type = DMGEntry_Audio;
	entry->fileOffset = dmg->fileSize;
	entry->length = size;
	entry->x = freq;
	dmg->fileSize += size + 6;
	return DMG_UpdateEntry(dmg, index);
}

const char* DMG_DescribeFreq(DMG_KHZ freq)
{
	switch (freq)
	{
		case DMG_5KHZ:    return "5KHz";
		case DMG_7KHZ:    return "7KHz";
		case DMG_9_5KHZ:  return "9.5KHz";
		case DMG_15KHZ:   return "15KHz";
		case DMG_20KHZ:   return "20KHz";
		case DMG_30KHZ:   return "30KHz";
        case DMG_44_1KHZ: return "44.1KHz";
        case DMG_48KHZ:   return "48KHz";
		default: 		  return "Unknown frequency";
	}
}

bool DMG_SetImageData (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t width, uint16_t height, uint16_t size, bool compressed)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	uint8_t header[6];

	if (entry->type != DMGEntry_Empty)
	{
		if (!DMG_RemoveEntry(dmg, index))
			return false;
	}

	File_Seek(dmg->file, dmg->fileSize);
	write16(header + 0x00, width | (compressed ? 0x8000 : 0), dmg->littleEndian);
	write16(header + 0x02, height, dmg->littleEndian);
	write16(header + 0x04, size, dmg->littleEndian);

	if (File_Write(dmg->file, header, 6) != 6)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	if (File_Write(dmg->file, buffer, size) != size)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return false;
	}
	dmg->fileSize += size + 6;

	entry->type = DMGEntry_Image;
    entry->bitDepth = 4;
	entry->fileOffset = dmg->fileSize;
	entry->width = width;
	entry->height = height;
	entry->length = size;
	return DMG_UpdateEntry(dmg, index);
}

DMG* DMG_Create(const char* filename)
{
	File* file;
	DMG* dmg;
	uint8_t header[10];
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);

	file = File_Create(filename);
	if (file == NULL)
	{
		DMG_SetError(DMG_ERROR_CREATING_FILE);
		return NULL;
	}

	write16(header + 0x00, 0x0300, false);
	write16(header + 0x02, 0, false);			// Screen mode
	write16(header + 0x04, 0, false);			// Entry count
	write32(header + 0x06, 0x300A, false);		// File size
	if (File_Write(file, header, 10) != 10)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		return NULL;
	}

	dmg = Allocate<DMG>("DMG");
	if (dmg == NULL)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	dmg->version = DMG_Version2;
	dmg->screenMode = ScreenMode_VGA16;
	dmg->littleEndian = 0;
	const char* dot = strrchr(filename, '.');
	if (dot != 0)
	{
		if (stricmp(dot, ".ega") == 0)
		{
			dmg->version = DMG_Version1_EGA;
			dmg->screenMode = ScreenMode_EGA;
			dmg->littleEndian = 1;
		}
		else if (stricmp(dot, ".cga") == 0)
		{
			dmg->version = DMG_Version1_CGA;
			dmg->screenMode = ScreenMode_CGA;
			dmg->littleEndian = 1;
		}
	}
	dmg->file = file;
	dmg->fileSize = 0x300A;
	for (int n = 0; n < 256; n++)
	{
		dmg->entries[n] = Allocate<DMG_Entry>("DMG Entry");
		if (dmg->entries[n] == 0)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			DMG_Close(dmg);
			return 0;
		}
	}

	int entrySize = 48;
	if (dmg->version == DMG_Version1_CGA ||
	    dmg->version == DMG_Version1_EGA)
		entrySize = 10;
	else if (dmg->version == DMG_Version1)
		entrySize = 44;
	int totalSize = entrySize * 256;
	memset(buffer, 0, totalSize);
	if (File_Write(file, buffer, totalSize) != totalSize)
	{
		DMG_SetError(DMG_ERROR_WRITING_FILE);
		DMG_Close(dmg);
		return NULL;
	}
	return dmg;
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  Compression															     */
/* ───────────────────────────────────────────────────────────────────────── */

bool CompressImage (uint8_t* pixels, int pixelCount,
	uint8_t* buffer, size_t bufferSize, bool* compressed, uint16_t* size)
{
	int compressedColorSize[16];
	int uncompressedColorSize[16];
	int totalCompressedSize = 0;
	int totalUncompressedSize = (pixelCount + 1)/2;
	int mask = 0;
	int n, i, v;
	int nibbles = 0;
	uint8_t* start = buffer;
	uint8_t* ptr;

	// Detect the optimal compression mask, and
	// mesure the size of the compressed stream

	memset(compressedColorSize, 0, sizeof(compressedColorSize));
	memset(uncompressedColorSize, 0, sizeof(uncompressedColorSize));
	ptr = pixels;
	n = 0;
	while (n < pixelCount)
	{
		uint8_t color = (*ptr++ & 0x0F);
		int repeat = 1;

		n++;
		while (repeat < 16 && n < pixelCount && *ptr == color)
		{
			repeat++;
			n++;
			ptr++;
		}
		compressedColorSize[color] += 2;
		uncompressedColorSize[color] += repeat;
	}

	for (n = 0; n < 16; n++)
	{
		if (compressedColorSize[n] < uncompressedColorSize[n])
		{
			mask |= 1 << n;
			totalCompressedSize += compressedColorSize[n];
		}
		else
		{
			totalCompressedSize += uncompressedColorSize[n];
		}
	}

	// Adjust sizes from nibble counts to final buffer sizes

	totalCompressedSize = (totalCompressedSize + 1)/2;
	totalCompressedSize = 2 + ((totalCompressedSize + 3) & ~3);
	totalUncompressedSize = (totalUncompressedSize + 3) & ~3;

	// Return an uncompressed image if compression is suboptimal
	// Uncompressed stream is packed in 8-pixel 32-bit words
	// with first color in the lowest 4 bits (big endian storage)

	if (totalCompressedSize >= totalUncompressedSize)
	{
		*compressed = false;
		*size = totalUncompressedSize;
		if (*size > bufferSize)
		{
			DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
			return false;
		}
		for (n = 0; n < pixelCount; n += 8)
		{
			uint32_t v = 0;
			int part = 8;
			if (n + part > pixelCount)
				part = pixelCount - n;

			for (i = 0; i < part; i++)
			{
				uint8_t c = pixels[n + i];
				v |= c << (i * 4);
			}
			write32(buffer + n/2, v, false);
		}
		return true;
	}

	// Return a compressed image, with the nibble stream
	// packed in 32-bit words (big endian) right to left

	*compressed = true;
	*size = totalCompressedSize;
	if (*size > bufferSize)
	{
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return false;
	}

	write16(buffer + 0x00, mask, false);
	buffer += 2;
	i = 0;
	v = 0;
	for (n = 0; n < pixelCount; )
	{
		uint8_t c = pixels[n++];

		v |= c << (i * 4);
		if (++i == 8)
		{
			write32(buffer, v, false);
			buffer += 4;
			i = v = 0;
		}
		if (mask & (1 << c))
		{
			uint8_t r = 0;
			while (n < pixelCount && pixels[n] == c && r < 15)
				n++, r++;

			v |= r << (i * 4);
			if (++i == 8)
			{
				write32(buffer, v, false);
				buffer += 4;
				i = v = 0;
			}
		}
	}
	if (i != 0)
	{
		write32(buffer, v, false);
		buffer += 4;
	}

	if (buffer != start + *size)
	{
		DMG_Warning("Buffer size mismatch: %d bytes used, %d expected", (int)(buffer - start), *size);
		*size = (int)(buffer - start);
	}
	return true;
}