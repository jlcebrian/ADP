#include <dmg.h>
#include <ddb.h>
#include <ddb_pal.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_bito.h>

static uint8_t* DMG_GetEntryDataV5(DMG* dmg, uint8_t index, DMG_ImageMode mode, DMG_Entry* entry, uint8_t* buffer, uint32_t bufferSize)
{
	uint32_t indexedSize = entry->width * entry->height;
	uint32_t paletteBytes = entry->paletteColors * 3;
	uint32_t imageDataLength = entry->length >= paletteBytes ? entry->length - paletteBytes : 0;
	uint8_t* fileData = buffer + bufferSize - imageDataLength;
	uint8_t* tempFileData = 0;
	if (entry->length < paletteBytes || imageDataLength > bufferSize)
	{
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return 0;
	}

	if (mode == ImageMode_Audio)
	{
		if (DMG_ReadFromFile(dmg, entry->fileOffset, fileData, entry->length) != entry->length)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
		return fileData;
	}

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
		if (bufferSize >= decompressedSize + imageDataLength)
		{
			compressedData = buffer + bufferSize - imageDataLength;
		}
		else
		{
			compressedData = Allocate<uint8_t>("ZX0 input", imageDataLength, false);
			if (compressedData == 0)
			{
				DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
				return 0;
			}
			freeCompressedData = true;
		}
		if (DMG_ReadFromFile(dmg, entry->fileOffset + paletteBytes, compressedData, imageDataLength) != imageDataLength)
		{
			if (freeCompressedData)
				Free(compressedData);
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
		if (!DMG_DecompressZX0(compressedData, imageDataLength, buffer, decompressedSize))
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
		if (DMG_ReadFromFile(dmg, entry->fileOffset + paletteBytes, fileData, imageDataLength) != imageDataLength)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
	}

	if (entry->type != DMGEntry_Image)
	{
		return 0;
	}

	DMG_ColorPaletteMode paletteMode = ColorPaletteMode_Native;

	switch (dmg->screenMode)
	{
		case ScreenMode_CGA:
			paletteMode = ColorPaletteMode_CGA;
			break;
		case ScreenMode_EGA:
			paletteMode = ColorPaletteMode_EGA;
			break;
		default:
			break;
	}

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
			paletteMode = ColorPaletteMode_CGA;
			break;

		case DMG_DAT5_COLORMODE_EGA:
			if (!DMG_UnpackChunkyPixels(fileData, entry->width, entry->height, 4, buffer))
			{
				if (tempFileData)
					Free(tempFileData);
				DMG_SetError(DMG_ERROR_INVALID_IMAGE);
				return 0;
			}
			paletteMode = ColorPaletteMode_EGA;
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

	if (mode == ImageMode_Indexed)
	{
		if (paletteMode == ColorPaletteMode_CGA)
		{
			for (uint32_t i = 0; i < indexedSize; i++)
				buffer[i] &= 0x03;
		}
		else if (paletteMode == ColorPaletteMode_EGA)
		{
			for (uint32_t i = 0; i < indexedSize; i++)
				buffer[i] &= 0x0F;
		}
		if (tempFileData)
			Free(tempFileData);
		return buffer;
	}

	if (mode == ImageMode_Packed)
	{
		if (paletteMode == ColorPaletteMode_CGA)
		{
			MemCopy(buffer, fileData, imageDataLength);
			if (tempFileData)
				Free(tempFileData);
			return buffer;
		}
		if (paletteMode == ColorPaletteMode_EGA)
		{
			MemCopy(buffer, fileData, imageDataLength);
			if (tempFileData)
				Free(tempFileData);
			return buffer;
		}
		if (tempFileData)
			Free(tempFileData);
		DMG_SetError(DMG_ERROR_INVALID_IMAGE);
		return 0;
	}

	if (mode == ImageMode_RGBA32)
	{
		uint32_t* dst = (uint32_t*)buffer;
		uint32_t* colors = entry->RGB32PaletteV5;
		uint8_t firstColor = entry->firstColor;
		if (paletteMode == ColorPaletteMode_CGA)
			colors = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
		else if (paletteMode == ColorPaletteMode_EGA)
			colors = EGAPalette;
		else if (colors == 0)
			colors = DMG_GetEntryPalette(dmg, index);

		for (int32_t i = (int32_t)indexedSize - 1; i >= 0; i--)
		{
			uint8_t color = buffer[i];
			if (paletteMode == ColorPaletteMode_Native && (color < firstColor || color > entry->lastColor))
				dst[i] = 0xFF000000;
			else
				dst[i] = colors[paletteMode == ColorPaletteMode_Native ? (color - firstColor) : color];
		}
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

	DMG_ColorPaletteMode paletteMode = ColorPaletteMode_Native;
	if (dmg->screenMode == ScreenMode_CGA || dmg->colorMode == DAT5_COLORMODE_CGA)
		paletteMode = ColorPaletteMode_CGA;
	if (dmg->screenMode == ScreenMode_EGA || dmg->colorMode == DAT5_COLORMODE_EGA)
		paletteMode = ColorPaletteMode_EGA;

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
		// PANIC: No temporary buffer
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return 0;
	}

	if (dmg->version == DMG_Version5)
	{
		uint8_t* result = DMG_GetEntryDataV5(dmg, index, mode, entry, buffer, bufferSize);
		if (cache != 0)
			cache->populated = result != 0;
		return result;
	}

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
		if (false)
		{
		}
		#if DMG_SUPPORT_EGA_SOURCES
		else if (dmg->version == DMG_Version1_EGA)
		{
			success = DMG_DecompressEGA(fileData, entry->length, buffer, entry->width, entry->height);
		}
		#endif
		#if DMG_SUPPORT_CGA_SOURCES
		else if (dmg->version == DMG_Version1_CGA)
		{
			success = DMG_DecompressCGA(fileData, entry->length, buffer, entry->width, entry->height);
		}
		#endif
		else
		{
			#if !DMG_SUPPORT_CROSS_ENDIAN_SOURCES
			if (!DMG_IsClassicNativeDATByteOrder(dmg->littleEndian))
			{
				if (cache != 0)
					cache->populated = false;
				DMG_SetError(DMG_ERROR_INVALID_IMAGE);
				return 0;
			}
			#endif
			uint16_t mask = read16(fileData, dmg->littleEndian);
			uint32_t pixels = entry->width * entry->height;
			if (dmg->version != DMG_Version2)
			{
				success = DMG_DecompressOldRLE(fileData+2, mask, 
					entry->length-2, buffer, pixels, dmg->littleEndian);
			}
			else if (DMG_IsClassicNativeDATByteOrder(dmg->littleEndian) && mode == ImageMode_PlanarST)
			{
				success = DMG_DecompressNewRLEToPlanarST(fileData+2, mask, 
					entry->length-2, buffer, pixels, dmg->littleEndian);
				if (success)
				{
					if (cache != 0)
						cache->populated = true;
					return buffer;
				}
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
		#if DMG_SUPPORT_EGA_SOURCES
		else if (dmg->version == DMG_Version1_EGA)
			success = DMG_UncEGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
		#endif
		#if DMG_SUPPORT_CGA_SOURCES
		else if (dmg->version == DMG_Version1_CGA)
			success = DMG_UncCGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
		#endif
		#if DMG_SUPPORT_CROSS_ENDIAN_SOURCES
		else if (dmg->littleEndian)
			success = DMG_CopyImageData(fileData, entry->length, buffer, packedSize);
		#endif
		else if (mode != ImageMode_PlanarST)
			success = DMG_Planar8ToPacked(fileData, entry->length, buffer, packedSize, entry->width);
		else
			return DMG_ConvertPlanar8ToPlanarST(fileData, buffer, packedSize, entry->width);
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
			case ImageMode_Planar8:
				DMG_SetError(DMG_ERROR_INVALID_IMAGE);
				break;
			case ImageMode_PlanarFalcon:
				DMG_SetError(DMG_ERROR_INVALID_IMAGE);
				break;
			case ImageMode_PlanarST:
				DMG_ConvertPackedToPlanarST(buffer, requiredSize, entry->width);
				break;
			case ImageMode_Planar:
				DMG_SetError(DMG_ERROR_INVALID_IMAGE);
				break;
			case ImageMode_RGBA32:
			{
				if (bufferSize < requiredSize * 8)
				{
					DMG_Warning("Entry %d: Buffer too small for RGBA32 image (%d bytes required)", index, requiredSize * 4);
					if (cache != 0)
						cache->populated = false;
					DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
					return 0;
				}
				ptr = (uint32_t *)buffer + 2*requiredSize;
				if (paletteMode == ColorPaletteMode_CGA)
				{
					colors = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
					for (n = requiredSize - 1; n > 0; n--)
					{
						*--ptr = colors[buffer[n] & 0x0F];
						*--ptr = colors[buffer[n] >> 4];
					}
				}
				else if (paletteMode == ColorPaletteMode_EGA)
				{
					for (n = requiredSize - 1; n > 0; n--)
					{
						*--ptr = entry->EGAPalette[buffer[n] & 0x0F];
						*--ptr = entry->EGAPalette[buffer[n] >> 4];
					}
				}
				else
				{
					colors = entry->RGB32Palette;
					for (n = requiredSize - 1; n > 0; n--)
					{
						*--ptr = colors[buffer[n] & 0x0F];
						*--ptr = colors[buffer[n] >> 4];
					}
				}
				break;
			}
			case ImageMode_Indexed:
			{
				uint8_t* ptr = buffer + (requiredSize/2) - 1;
				for (n = requiredSize; n > 1; )
				{
					if (paletteMode == ColorPaletteMode_CGA)
						buffer[--n] = entry->CGAPalette[*ptr & 0x0F];
					else if (paletteMode == ColorPaletteMode_EGA)
						buffer[--n] = entry->EGAPalette[*ptr & 0x0F];
					else
						buffer[--n] = *ptr & 0x0F;

					if (paletteMode == ColorPaletteMode_CGA)
						buffer[--n] = entry->CGAPalette[*ptr-- >> 4];
					else if (paletteMode == ColorPaletteMode_EGA)
						buffer[--n] = entry->EGAPalette[*ptr-- >> 4];
					else
						buffer[--n] = *ptr-- >> 4;
				}
				break;
			}
		}
	}
	if (!success)
	{
		if (cache != 0)
			cache->populated = false;
		if (DMG_GetError() == DMG_ERROR_NONE)
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return 0;
	}

	if (cache != 0)
		cache->populated = true;

	return buffer;
}

uint8_t* DMG_GetEntryDataNative(DMG* dmg, uint8_t index)
{
	#if defined(_AMIGA) || defined(_ATARIST)
	return DMG_GetEntryDataPlanar(dmg, index);
	#else
	return DMG_GetEntryData(dmg, index, DMG_NATIVE_IMAGE_MODE);
	#endif
}
