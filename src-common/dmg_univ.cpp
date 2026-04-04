#include <dmg.h>
#include <ddb.h>
#include <ddb_pal.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_bito.h>

static uint8_t* DMG_GetEntryDataV5(DMG* dmg, uint8_t index, DMG_ImageMode mode, DMG_Entry* entry, uint8_t* buffer, uint32_t bufferSize)
{
    uint32_t indexedSize = entry->width * entry->height;
    uint8_t* fileData = buffer + bufferSize - entry->length;
    uint8_t* tempFileData = 0;
    if (entry->length > bufferSize)
    {
        DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
        return 0;
    }
    if (DMG_ReadFromFile(dmg, entry->fileOffset, fileData, entry->length) != entry->length)
    {
        DMG_SetError(DMG_ERROR_READING_FILE);
        return 0;
    }

    if (mode == ImageMode_Audio)
        return fileData;

    if ((entry->flags & DMG_FLAG_COMPRESSED) != 0)
    {
        if ((entry->flags & DMG_FLAG_ZX0) == 0)
        {
            DMG_SetError(DMG_ERROR_INVALID_IMAGE);
            return 0;
        }

        uint32_t decompressedSize = 0;
        switch (dmg->colorMode)
        {
            case DMG_DAT5_COLORMODE_CGA:
                decompressedSize = ((uint32_t)entry->width * entry->height + 3) / 4;
                break;
            case DMG_DAT5_COLORMODE_EGA:
                decompressedSize = ((uint32_t)entry->width * entry->height + 1) / 2;
                break;
            case DMG_DAT5_COLORMODE_I16:
            case DMG_DAT5_COLORMODE_I32:
            case DMG_DAT5_COLORMODE_I256:
                decompressedSize = ((uint32_t)(entry->width + 7) >> 3) * entry->height * entry->bitDepth;
                break;
            default:
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return 0;
        }

        if (decompressedSize > bufferSize)
        {
            DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
            return 0;
        }
        uint8_t* compressedData = 0;
        bool freeCompressedData = false;
        if (bufferSize >= decompressedSize + entry->length)
        {
            compressedData = buffer + bufferSize - entry->length;
        }
        else
        {
            compressedData = Allocate<uint8_t>("ZX0 input", entry->length, false);
            if (compressedData == 0)
            {
                DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
                return 0;
            }
            freeCompressedData = true;
        }
        if (DMG_ReadFromFile(dmg, entry->fileOffset, compressedData, entry->length) != entry->length)
        {
            if (freeCompressedData)
                Free(compressedData);
            DMG_SetError(DMG_ERROR_READING_FILE);
            return 0;
        }
        if (!DMG_DecompressZX0(compressedData, entry->length, buffer, decompressedSize))
        {
            if (freeCompressedData)
                Free(compressedData);
            DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
            return 0;
        }
        tempFileData = Allocate<uint8_t>("DAT5 decompressed", decompressedSize, false);
        if (tempFileData == 0)
        {
            if (freeCompressedData)
                Free(compressedData);
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            return 0;
        }
        MemCopy(tempFileData, buffer, decompressedSize);
        if (freeCompressedData)
            Free(compressedData);
        fileData = tempFileData;
    }
    else
    {
        if (DMG_ReadFromFile(dmg, entry->fileOffset, fileData, entry->length) != entry->length)
        {
            DMG_SetError(DMG_ERROR_READING_FILE);
            return 0;
        }
    }

    if (entry->type != DMGEntry_Image)
        return 0;

    switch (dmg->colorMode)
    {
        case DMG_DAT5_COLORMODE_CGA:
            if (!DMG_UnpackChunkyPixels(fileData, entry->width, entry->height, 2, buffer))
            {
                if (tempFileData)
                    Free(tempFileData);
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return 0;
            }
            break;

        case DMG_DAT5_COLORMODE_EGA:
            if (!DMG_UnpackChunkyPixels(fileData, entry->width, entry->height, 4, buffer))
            {
                if (tempFileData)
                    Free(tempFileData);
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return 0;
            }
            break;

        case DMG_DAT5_COLORMODE_I16:
        case DMG_DAT5_COLORMODE_I32:
        case DMG_DAT5_COLORMODE_I256:
            if (!DMG_UnpackBitplaneBytes(fileData, entry->width, entry->height, entry->bitDepth, buffer))
            {
                if (tempFileData)
                    Free(tempFileData);
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return 0;
            }
            break;

        default:
            if (tempFileData)
                Free(tempFileData);
            DMG_SetError(DMG_ERROR_INVALID_IMAGE);
            return 0;
    }

    if (mode == ImageMode_Indexed || mode == ImageMode_IndexedCGA || mode == ImageMode_IndexedEGA)
    {
        if (mode == ImageMode_IndexedCGA)
        {
            for (uint32_t i = 0; i < indexedSize; i++)
                buffer[i] &= 0x03;
        }
        else if (mode == ImageMode_IndexedEGA)
        {
            for (uint32_t i = 0; i < indexedSize; i++)
                buffer[i] &= 0x0F;
        }
        if (tempFileData)
            Free(tempFileData);
        return buffer;
    }

    if (mode == ImageMode_Packed || mode == ImageMode_PackedCGA || mode == ImageMode_PackedEGA)
    {
        if (dmg->colorMode == DMG_DAT5_COLORMODE_CGA && mode == ImageMode_PackedCGA)
        {
            MemCopy(buffer, fileData, entry->length);
            if (tempFileData)
                Free(tempFileData);
            return buffer;
        }
        if (dmg->colorMode == DMG_DAT5_COLORMODE_EGA && mode == ImageMode_PackedEGA)
        {
            MemCopy(buffer, fileData, entry->length);
            if (tempFileData)
                Free(tempFileData);
            return buffer;
        }
        if (tempFileData)
            Free(tempFileData);
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return 0;
    }

    if (mode == ImageMode_RGBA32 || mode == ImageMode_RGBA32CGA || mode == ImageMode_RGBA32EGA)
    {
        uint32_t* dst = (uint32_t*)buffer;
        uint32_t* colors = entry->RGB32PaletteV5;
        if (mode == ImageMode_RGBA32CGA)
            colors = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
        else if (mode == ImageMode_RGBA32EGA)
            colors = EGAPalette;

        for (int32_t i = (int32_t)indexedSize - 1; i >= 0; i--)
            dst[i] = colors[buffer[i]];
        if (tempFileData)
            Free(tempFileData);
        return buffer;
    }

    if (mode == ImageMode_Planar || mode == ImageMode_PlanarST)
    {
        if (tempFileData)
            Free(tempFileData);
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return 0;
    }

    if (tempFileData)
        Free(tempFileData);
    DMG_SetError(DMG_ERROR_INVALID_IMAGE);
    return 0;
}

uint8_t* DMG_GetEntryData(DMG* dmg, uint8_t index, DMG_ImageMode mode)
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

	uint32_t requiredSize = DMG_CalculateRequiredSize(entry, mode);

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
		buffer = DMG_GetTemporaryBuffer(mode);
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
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return 0;
	}

    if (dmg->version == DMG_Version5)
        return DMG_GetEntryDataV5(dmg, index, mode, entry, buffer, bufferSize);

#ifndef NO_CACHE
	fileData = (uint8_t*)DMG_GetFromFileCache(dmg, entry->fileOffset+6, entry->length);
	if (fileData == 0)
#endif
	{
		if (entry->length > bufferSize)
		{
			DMG_Warning("Entry %d: Invalid entry data (size too big for image dimensions)", index);
			DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
			return 0;
		}

		fileData = buffer + bufferSize - entry->length;
		if (DMG_ReadFromFile(dmg, entry->fileOffset + 6, fileData, entry->length) != entry->length)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
	}

	if (mode == ImageMode_Audio)
		return fileData;

	if ((entry->flags & DMG_FLAG_COMPRESSED) != 0)
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
			uint32_t pixels = entry->width * entry->height;
			if (dmg->version != DMG_Version2)
			{
				success = DMG_DecompressOldRLE(fileData+2, mask, 
					entry->length-2, buffer, pixels, dmg->littleEndian);
			}
			else if (!dmg->littleEndian && mode == ImageMode_PlanarST)
			{
				success = DMG_DecompressNewRLEPlanar(fileData+2, mask, 
					entry->length-2, buffer, pixels, dmg->littleEndian);
				if (success)
					return buffer;
			}
			else
			{
				success = DMG_DecompressNewRLE(fileData+2, mask, 
					entry->length-2, buffer, pixels, dmg->littleEndian);
			}
		}
	}
	else
	{
        // TODO: Handle ImageMode_Planar
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
		else if (mode != ImageMode_PlanarST)
			success = DMG_Planar8ToPacked(fileData, entry->length, buffer, packedSize, entry->width);
		else
			return DMG_Planar8To16(fileData, buffer, packedSize, entry->width);
	}
	if (success && mode != ImageMode_Packed)
	{
		uint32_t *ptr;
		uint32_t *colors;
		int n;
			
		switch (mode)
		{
			case ImageMode_Raw:
			case ImageMode_Packed:
				break;
			case ImageMode_PackedEGA:
			{
				for (n = requiredSize - 1; n > 0; n--)
				{
					buffer[n] = entry->EGAPalette[buffer[n] & 0x0F] |
						(entry->EGAPalette[buffer[n] >> 4] << 4);
				}
				break;
			}
			case ImageMode_PackedCGA:
			{
				for (n = requiredSize - 1; n > 0; n--)
				{
					buffer[n] = entry->CGAPalette[buffer[n] & 0x0F] |
						(entry->CGAPalette[buffer[n] >> 4] << 4);
				}
				break;
			}
			case ImageMode_PlanarST:
			{
				DMG_ConvertChunkyToPlanar(buffer, requiredSize, entry->width);
				break;
			}
            case ImageMode_Planar:
            {
                // TODO: Convert to independent plane bitmaps
                break;
            }
			case ImageMode_RGBA32:
			{
				if (bufferSize < requiredSize * 8)
				{
					DMG_Warning("Entry %d: Buffer too small for RGBA32 image (%d bytes required)", index, requiredSize * 4);
					DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
					return 0;
				}
				ptr = (uint32_t *)buffer + 2*requiredSize;
				for (n = requiredSize - 1; n > 0; n--) 
				{
					*--ptr = entry->RGB32Palette[buffer[n] & 0x0F];
					*--ptr = entry->RGB32Palette[buffer[n] >> 4];
				}
				break;
			}
			case ImageMode_RGBA32EGA:
			{
				if (bufferSize < requiredSize * 8)
				{
					DMG_Warning("Entry %d: Buffer too small for RGBA32 image (%d bytes required)", index, requiredSize * 4);
					DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
					return 0;
				}
				ptr = (uint32_t *)buffer + 2*requiredSize;
				for (n = requiredSize - 1; n > 0; n--) 
				{
					*--ptr = entry->EGAPalette[buffer[n] & 0x0F];
					*--ptr = entry->EGAPalette[buffer[n] >> 4];
				}
				break;
			}
			case ImageMode_RGBA32CGA:
			{
				if (bufferSize < requiredSize * 8)
				{
					DMG_Warning("Entry %d: Buffer too small for RGBA32 image (%d bytes required)", index, requiredSize * 4);
					DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
					return 0;
				}
				colors = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
				ptr = (uint32_t *)buffer + 2*requiredSize;
				for (n = requiredSize - 1; n > 0; n--) 
				{
					*--ptr = colors[buffer[n] & 0x0F];
					*--ptr = colors[buffer[n] >> 4];
				}
				break;
			}
			case ImageMode_Indexed:
			{
				uint8_t* ptr = buffer + (requiredSize/2) - 1;
				for (n = requiredSize; n > 1; )
				{
					buffer[--n] = *ptr & 0x0F;
					buffer[--n] = *ptr-- >> 4;
				}
				break;
			}
			case ImageMode_IndexedCGA:
			{
				uint8_t* ptr = buffer + (requiredSize/2) - 1;
				for (n = requiredSize; n > 1; )
				{
					buffer[--n] = entry->CGAPalette[*ptr & 0x0F];
					buffer[--n] = entry->CGAPalette[*ptr-- >> 4];
				}
				break;
			}
			case ImageMode_IndexedEGA:
			{
				uint8_t* ptr = buffer + (requiredSize/2) - 1;
				for (n = requiredSize; n > 1; )
				{
					buffer[--n] = entry->EGAPalette[*ptr & 0x0F];
					buffer[--n] = entry->EGAPalette[*ptr-- >> 4];
				}
				break;
			}
		}
	}
	if (!success)
	{
		if (DMG_GetError() == DMG_ERROR_NONE)
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return 0;
	}

	return buffer;
}
