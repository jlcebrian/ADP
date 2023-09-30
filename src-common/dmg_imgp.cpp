#include <dmg.h>
#include <ddb.h>
#include <ddb_pal.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_bito.h>

extern "C" void DecompressRLE (const void* data, uint32_t dataSize,
	void* output, uint32_t outputSize, uint16_t rleMask);

bool DMG_DecompressNewRLEPlanar (const uint8_t* d, uint16_t rleMask, 
	uint16_t dataLength, uint8_t* buffer, uint32_t width, bool littleEndian)
{
#if HAS_ASM_RLE
	DecompressRLE(d, dataLength, buffer, width, rleMask);
	return true;
#else
	// WARNING! Non-ASM RLE decompression is buggy, please use ASM implementaiton

	uint32_t nibbles;
	uint8_t repetitions;
	const uint32_t* data = (const uint32_t*) d;
	const uint32_t* end = (const uint32_t*)(d + ((dataLength+3)&~3));
	int nibbleCount = 0;

	uint32_t* out = (uint32_t*)buffer;
	uint32_t p0 = 0x00000000;
	uint32_t p1 = 0x00000000;
	uint32_t x0 = 0x80000000;
	uint32_t x1 = 0x00008000;
	uint32_t xc = 0;

	while (data < end)
	{
		if (nibbleCount == 0)
		{
			nibbles = fix32(*data++);
			nibbleCount = 7;
		}
		else
		{
			nibbles >>= 4;
			nibbleCount--;
		}
		
		if (nibbles & 0x01) p0 |= x0;
		if (nibbles & 0x02) p0 |= x1;
		if (nibbles & 0x04) p1 |= x0;
		if (nibbles & 0x08) p1 |= x1;
		x0 >>= 1;
		x1 >>= 1;
		xc++;
		if (!x1 || xc == width)
		{
			*out++ = p0;
			*out++ = p1;
			p0 = p1 = 0;
			x0 = 0x80000000;
			x1 = 0x00008000;
			if (xc == width)
				xc = 0;
		}

		if (rleMask & (1 << (nibbles & 0x0F))) 
		{
			uint32_t m0 = nibbles & 0x01 ? 0xFFFF0000 : 0;
			uint32_t m1 = nibbles & 0x04 ? 0xFFFF0000 : 0;
			if (nibbles & 0x02) m0 |= 0xFFFF;
			if (nibbles & 0x08) m1 |= 0xFFFF;

			if (nibbleCount == 0)
			{
				nibbles = fix32(*data++);
				nibbleCount = 7;
			}
			else
			{
				nibbles >>= 4;
				nibbleCount--;
			}

			repetitions = nibbles & 0x0F;

			while (repetitions--)
			{
				p0 |= m0 & (x0 | x1);
				p1 |= m1 & (x0 | x1);
				x0 >>= 1;
				x1 >>= 1;
				xc++;
				if (!x1 || xc == width)
				{
					*out++ = p0;
					*out++ = p1;
					p0 = p1 = 0;
					x0 = 0x80000000;
					x1 = 0x00008000;
					if (xc == width)
						xc = 0;
				}
			}
		}
	}
	if (x1 != 0x8000)
	{
		*out++ = p0;
		*out++ = p1;
	}
	return true;
#endif
}

uint8_t* DMG_GetEntryDataPlanar (DMG* dmg, uint8_t index)
{
	bool success;
	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	if (entry == 0)
		return 0;
	if (entry->type == DMGEntry_Empty)
		return 0;

	uint8_t* buffer = 0;
	uint8_t* fileData;
	uint32_t bufferSize = 0;

	unsigned requiredSize = DMG_CalculateRequiredSize(entry, ImageMode_PlanarST);

#ifndef NO_CACHE
	DMG_Cache* cache = DMG_GetImageCache (dmg, index, entry, requiredSize);
	if (cache)
	{
		buffer = (uint8_t*)(cache + 1);
		if (cache->populated)
			return buffer;
		bufferSize = cache->size;
		cache->populated = true;
	}
#endif

	if (buffer == 0 || bufferSize < requiredSize)
	{
		buffer = DMG_GetTemporaryBuffer(ImageMode_PlanarST);
		bufferSize = DMG_GetTemporaryBufferSize();
		if (bufferSize < requiredSize)
		{
			DMG_Warning("Entry %d: Internal buffer too small for entry (%d bytes required)", index, requiredSize);
			DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
			return 0;
		}
	}
	if (buffer == 0)
	{
		// PANIC: No decompression buffer
		DMG_SetError(DMG_ERROR_DECOMPRESSION_BUFFER_MISSING);
		return 0;
	}

#ifndef NO_CACHE
	fileData = (uint8_t*)DMG_GetFromFileCache(dmg, entry->fileOffset+6, entry->length);
	if (fileData == 0)
#endif
	{
		if (entry->length > bufferSize)
		{
			DMG_Warning("Entry %d: Invalid entry data (size too big for image dimensions)", index);
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
			return 0;
		}

		fileData = buffer + bufferSize - entry->length;
		if (DMG_ReadFromFile(dmg, entry->fileOffset + 6, fileData, entry->length) != entry->length)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
	}

	if (entry->compressed)
	{
		if (dmg->version == DMG_Version1_EGA)
		{
			success = DMG_DecompressEGA(fileData, entry->length, buffer, entry->width, entry->height);
		}
		else if (dmg->version == DMG_Version1_CGA)
		{
			success = DMG_DecompressCGA(fileData, entry->length, buffer, entry->width, entry->height);
		}
		else
		{
			uint16_t mask = read16(fileData, dmg->littleEndian);
			if (dmg->version != DMG_Version2)
			{
				success = DMG_DecompressOldRLE(fileData+2, mask, 
					entry->length-2, buffer, requiredSize*2, dmg->littleEndian);
			}
			else if (!dmg->littleEndian)
			{
				success = DMG_DecompressNewRLEPlanar(fileData+2, mask, 
					entry->length-2, buffer, entry->width, dmg->littleEndian);
				if (success)
					return buffer;
			}
			else
			{
				success = DMG_DecompressNewRLE(fileData+2, mask, 
					entry->length-2, buffer, requiredSize*2, dmg->littleEndian);
				if (success)
					return buffer;
			}
		}
	}
	else
	{
		uint32_t expectedSize = DMG_CalculateRequiredSize(entry, ImageMode_Raw);
		uint32_t packedSize = DMG_CalculateRequiredSize(entry, ImageMode_Packed);
		if (entry->length != expectedSize)
			success = false;
		else if (dmg->version == DMG_Version1_EGA)
			success = DMG_UncEGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
		else if (dmg->version == DMG_Version1_CGA)
			success = DMG_UncCGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
		else if (dmg->littleEndian)
			success = DMG_CopyImageData(fileData, entry->length, buffer, packedSize);
		else
			return DMG_Planar8To16(fileData, buffer, packedSize, entry->width);
	}
	if (success)
	{
		DMG_ConvertChunkyToPlanar(buffer, requiredSize, entry->width);
	}
	else
	{
		if (DMG_GetError() == DMG_ERROR_NONE)
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return 0;
	}

	return buffer;
}