#include <dmg.h>
#include <ddb.h>
#include <ddb_pal.h>
#include <ddb_vid.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_bito.h>

extern "C" void DecompressRLE (const void* data, uint32_t dataSize,
	void* output, uint32_t outputSize, uint16_t rleMask);

#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_ORIGINAL_PROTO
extern "C" bool DecompressOldRLEToSTAsmOriginalPrototype(const void* data, uint32_t dataSize,
	void* output, uint32_t pixels, uint16_t rleMask);
#elif defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_EXPERIMENTAL_PROTO
extern "C" bool DecompressOldRLEToSTAsmExperimental(const void* data, uint32_t dataSize,
	void* output, uint32_t pixels, uint16_t rleMask);
#endif

#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_PLANAR_ASM
extern "C" bool DecompressOldRLEToPlanarAsm(const void* data, uint32_t dataSize,
	void* output, uint32_t pixels, uint16_t rleMask, uint32_t planeStride);
#endif

#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_PLANAR_ASM && VERIFY_OLD_RLE_PLANAR_ASM
static bool DMG_RunAndVerifyOldRLEToPlanarAsm(const DMG_Entry* entry, DMG* dmg,
	const uint8_t* data, uint16_t rleMask, uint16_t dataLength,
	uint8_t* buffer, uint32_t outputSize, uint32_t pixels, uint8_t index)
{
	DebugPrintf("Comparing old RLE vs Planar ASM");

	const uint32_t guardBytes = 32;
	const uint8_t guardPattern = 0xA5;
	uint8_t* asmStorage = Allocate<uint8_t>("old-rle-planar-asm-output", outputSize + guardBytes * 2, false);
	uint8_t* refStorage = Allocate<uint8_t>("old-rle-planar-ref-output", outputSize + guardBytes * 2, false);
	if (asmStorage == 0 || refStorage == 0)
	{
		DebugPrintf("WARNING: unable to allocate old-RLE planar verification buffers");
		if (asmStorage != 0)
			Free(asmStorage);
		if (refStorage != 0)
			Free(refStorage);
		return false;
	}
	uint8_t* asmOutput = asmStorage + guardBytes;
	uint8_t* refOutput = refStorage + guardBytes;

	MemSet(asmStorage, guardPattern, outputSize + guardBytes * 2);
	MemSet(refStorage, guardPattern, outputSize + guardBytes * 2);
	MemClear(asmOutput, outputSize);
	MemClear(refOutput, outputSize);

	uint32_t planeStride = outputSize >> 2;
	bool asmOk = DecompressOldRLEToPlanarAsm(data, dataLength, asmOutput, pixels, rleMask, planeStride);
	bool refOk = DMG_DecompressOldRLEToST_COnly(data, rleMask, dataLength, refOutput, (int)pixels);
	if (refOk)
		refOk = DMG_RepackClassicPlanar(entry, refOutput, outputSize, dmg->zx0Scratch, dmg->zx0ScratchSize);

	for (uint32_t guard = 0; guard < guardBytes; guard++)
	{
		if (asmStorage[guard] != guardPattern || asmStorage[guardBytes + outputSize + guard] != guardPattern)
		{
			DebugPrintf("WARNING: old-RLE planar ASM wrote outside output buffer in image %d", index);
			asmOk = false;
			break;
		}
	}

	if (!asmOk || !refOk)
	{
		DebugPrintf("WARNING: old-RLE planar verification setup failed (asm=%d ref=%d)", asmOk ? 1 : 0, refOk ? 1 : 0);
		if (refOk)
			MemCopy(buffer, refOutput, outputSize);
		Free(refStorage);
		Free(asmStorage);
		return refOk;
	}

	// if (MemComp(asmOutput, refOutput, outputSize) != 0)
	uint32_t wordCount = outputSize >> 1;
	uint16_t* asmWords = (uint16_t*)asmOutput;
	uint16_t* refWords = (uint16_t*)refOutput;
	for (uint32_t word = 0; word < wordCount; word++)
	{
		if (asmWords[word] != refWords[word])
		{
			DebugPrintf("WARNING: old-RLE planar ASM mismatch in image %d at %p, word %lu (asm=%04x ref=%04x)",
				index, &asmWords[word], (unsigned long)word, asmWords[word], refWords[word]);
			MemCopy(buffer, asmOutput, outputSize);
			Free(refStorage);
			Free(asmStorage);
			return true;
			break;
		}
	}

	DebugPrintf("Both versions match (%d bytes image, %d size output)", dataLength, outputSize);
	MemCopy(buffer, asmOutput, outputSize);
	Free(refStorage);
	Free(asmStorage);
	return true;
}
#endif

bool DMG_RepackClassicPlanar(const DMG_Entry* entry, uint8_t* data, uint32_t dataSize, uint8_t* scratch, uint32_t scratchSize)
{
	if (entry == 0 || data == 0 || scratch == 0)
		return false;
	if (entry->bitDepth != 0 && entry->bitDepth != 4)
		return false;

	uint32_t widthWords = (uint32_t)(entry->width + 15) >> 4;
	uint32_t planeStrideWords = widthWords * entry->height;
	uint32_t totalWords = planeStrideWords * 4;
	uint32_t totalBytes = totalWords * sizeof(uint16_t);
	if (totalBytes == 0 || totalBytes > dataSize || totalBytes > scratchSize)
		return false;

	uint16_t* src = (uint16_t*)data;
	uint16_t* dst0 = (uint16_t*)scratch;
	uint16_t* dst1 = dst0 + planeStrideWords;
	uint16_t* dst2 = dst1 + planeStrideWords;
	uint16_t* dst3 = dst2 + planeStrideWords;

	for (uint32_t n = 0; n < planeStrideWords; n++)
	{
		*dst0++ = *src++;
		*dst1++ = *src++;
		*dst2++ = *src++;
		*dst3++ = *src++;
	}

	MemCopy(data, scratch, totalBytes);
	return true;
}

static bool DMG_ConvertOldRLEPrototypeToPlanar(const DMG_Entry* entry, const uint8_t* data,
	uint32_t packedSize, uint8_t* output, uint32_t outputSize)
{
	if (entry == 0 || data == 0 || output == 0)
		return false;
	if (entry->bitDepth != 0 && entry->bitDepth != 4)
		return false;

	uint32_t widthWords = (uint32_t)(entry->width + 15) >> 4;
	uint32_t widthBlocks = (uint32_t)(entry->width + 7) >> 3;
	uint32_t planeStrideWords = widthWords * entry->height;
	uint32_t planeStrideBytes = planeStrideWords * sizeof(uint16_t);
	uint32_t totalBytes = planeStrideBytes * 4;
	uint32_t expectedPackedSize = widthBlocks * entry->height * 4;
	if (totalBytes == 0 || totalBytes > outputSize || packedSize != expectedPackedSize)
		return false;

	uint8_t* dst0 = output;
	uint8_t* dst1 = dst0 + planeStrideBytes;
	uint8_t* dst2 = dst1 + planeStrideBytes;
	uint8_t* dst3 = dst2 + planeStrideBytes;
	const uint8_t* src = data;

	for (uint32_t y = 0; y < entry->height; y++)
	{
		uint32_t rowWordOffset = y * widthWords * 2;
		for (uint32_t word = 0, block = 0; word < widthWords; word++, block += 2)
		{
			uint32_t dstOffset = rowWordOffset + word * 2;

			dst0[dstOffset] = *src++;
			dst1[dstOffset] = *src++;
			dst2[dstOffset] = *src++;
			dst3[dstOffset] = *src++;

			if (block + 1 < widthBlocks)
			{
				dst0[dstOffset + 1] = *src++;
				dst1[dstOffset + 1] = *src++;
				dst2[dstOffset + 1] = *src++;
				dst3[dstOffset + 1] = *src++;
			}
			else
			{
				dst0[dstOffset + 1] = 0;
				dst1[dstOffset + 1] = 0;
				dst2[dstOffset + 1] = 0;
				dst3[dstOffset + 1] = 0;
			}
		}
	}

	return true;
}

static bool DMG_EnsureDAT5PaletteFromPayload(DMG_Entry* entry, const uint8_t* payload)
{
    if (entry == 0 || entry->paletteColors == 0 || entry->paletteSize == 0 || entry->RGB32PaletteV5 != 0)
        return true;

    entry->RGB32PaletteV5 = Allocate<uint32_t>("DAT5 palette", entry->paletteColors);
    if (entry->RGB32PaletteV5 == 0)
    {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }

    for (uint16_t p = 0; p < entry->paletteColors; p++)
    {
        entry->RGB32PaletteV5[p] =
            0xFF000000 |
            (payload[p * 3 + 0] << 16) |
            (payload[p * 3 + 1] << 8) |
            (payload[p * 3 + 2] << 0);
    }
    return true;
}

static uint8_t* DMG_GetEntryDataPlanarV5(DMG* dmg, uint8_t index, DMG_Entry* entry, uint8_t* buffer, uint32_t bufferSize)
{
	if (entry->type != DMGEntry_Image)
		return 0;

	if (dmg->colorMode != DMG_DAT5_COLORMODE_I16 &&
		dmg->colorMode != DMG_DAT5_COLORMODE_I32 &&
		dmg->colorMode != DMG_DAT5_COLORMODE_I256)
	{
		DMG_SetError(DMG_ERROR_INVALID_IMAGE);
		return 0;
	}

	uint32_t requiredSize = ((uint32_t)(entry->width + 7) >> 3) * entry->height * entry->bitDepth;
    uint32_t paletteBytes = entry->paletteColors * 3;
    uint32_t imageDataLength = entry->length >= paletteBytes ? entry->length - paletteBytes : 0;
	if (requiredSize > bufferSize)
	{
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return 0;
	}
    if (entry->length < paletteBytes)
    {
        DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
        return 0;
    }

	if ((entry->flags & DMG_FLAG_COMPRESSED) != 0)
	{
		if ((entry->flags & DMG_FLAG_ZX0) == 0)
		{
			DMG_SetError(DMG_ERROR_INVALID_IMAGE);
			return 0;
		}

		const uint8_t* payloadData = (const uint8_t*)DMG_GetFromFileCache(dmg, entry->fileOffset, entry->length);
		uint8_t* scratch = 0;
		if (payloadData == 0)
		{
			if (bufferSize >= requiredSize + entry->length)
				scratch = buffer + bufferSize - entry->length;
			else if (dmg->zx0Scratch != 0 && dmg->zx0ScratchSize >= entry->length)
				scratch = dmg->zx0Scratch;
			else
			{
				DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
				return 0;
			}

			if (DMG_ReadFromFile(dmg, entry->fileOffset, scratch, entry->length) != entry->length)
			{
				DMG_SetError(DMG_ERROR_READING_FILE);
				return 0;
			}
			payloadData = scratch;
		}

        if (!DMG_EnsureDAT5PaletteFromPayload(entry, payloadData))
            return 0;

		if (!DMG_DecompressZX0(payloadData + paletteBytes, imageDataLength, buffer, requiredSize))
		{
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
			return 0;
		}
		return buffer;
	}

	if (imageDataLength != requiredSize)
	{
		DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return 0;
	}

	const uint8_t* payloadData = (const uint8_t*)DMG_GetFromFileCache(dmg, entry->fileOffset, entry->length);
	if (payloadData != 0)
	{
        if (!DMG_EnsureDAT5PaletteFromPayload(entry, payloadData))
            return 0;
		MemCopy(buffer, payloadData + paletteBytes, requiredSize);
		return buffer;
	}

    uint8_t* scratch = 0;
    if (bufferSize >= requiredSize + entry->length)
        scratch = buffer + bufferSize - entry->length;
    else if (dmg->zx0Scratch != 0 && dmg->zx0ScratchSize >= entry->length)
        scratch = dmg->zx0Scratch;
    else
    {
        DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
        return 0;
    }

	if (DMG_ReadFromFile(dmg, entry->fileOffset, scratch, entry->length) != entry->length)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return 0;
	}
    if (!DMG_EnsureDAT5PaletteFromPayload(entry, scratch))
        return 0;
    MemCopy(buffer, scratch + paletteBytes, requiredSize);
	return buffer;
}

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
	DMG_Cache* cache = 0;
	bool fileDataSharesOutputBuffer = false;

	unsigned requiredSize = DMG_CalculateRequiredSize(entry, ImageMode_PlanarST);

#ifndef NO_CACHE
	cache = DMG_GetImageCache (dmg, index, entry, requiredSize);
	if (cache)
	{
		buffer = (uint8_t*)(cache + 1);
		if (cache->populated)
		{
			if (cache->processed)
				entry->flags |= DMG_FLAG_PLANAR_MAJOR;
			else
				entry->flags &= ~DMG_FLAG_PLANAR_MAJOR;
			return buffer;
		}
		bufferSize = cache->size;
	}
#endif
	entry->flags &= ~DMG_FLAG_PLANAR_MAJOR;

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

	if (dmg->version == DMG_Version5)
	{
		uint8_t* result = DMG_GetEntryDataPlanarV5(dmg, index, entry, buffer, bufferSize);
		if (result != 0 && cache != 0)
			cache->populated = true;
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
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
			return 0;
		}

		fileData = buffer + bufferSize - entry->length;
		fileDataSharesOutputBuffer = true;
		if (DMG_ReadFromFile(dmg, entry->fileOffset + 6, fileData, entry->length) != entry->length)
		{
			DMG_SetError(DMG_ERROR_READING_FILE);
			return 0;
		}
	}

	bool chunky = false;
	bool planarMajorOutput = false;

	if ((entry->flags & DMG_FLAG_COMPRESSED) != 0)
	{
		if (dmg->version == DMG_Version1_EGA)
		{
			success = DMG_DecompressEGA(fileData, entry->length, buffer, entry->width, entry->height);
			chunky = true;
		}
		else if (dmg->version == DMG_Version1_CGA)
		{
			success = DMG_DecompressCGA(fileData, entry->length, buffer, entry->width, entry->height);
			chunky = true;
		}
		else
		{
			uint16_t mask = read16(fileData, dmg->littleEndian);
			if (dmg->version != DMG_Version2)
			{
				uint32_t t0, t1;
				uint32_t prototypeToPlanarMs = 0;
				VID_GetMilliseconds(&t0);
				const uint8_t* oldRLEData = fileData + 2;
				uint8_t* oldRLEOutput = buffer;
				bool prototypeNeedsPlanarConvert = false;
				uint32_t prototypePackedSize = 0;
				#if defined(_AMIGA) && HAS_ASM_RLE && (OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_ORIGINAL_PROTO || OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_EXPERIMENTAL_PROTO)
				if (!dmg->littleEndian)
				{
					prototypePackedSize = ((uint32_t)(entry->width + 7) >> 3) * entry->height * 4;
					if (prototypePackedSize > requiredSize)
					{
						DMG_SetError(DMG_ERROR_INVALID_IMAGE);
						return 0;
					}

					if ((!fileDataSharesOutputBuffer && bufferSize >= requiredSize + prototypePackedSize) ||
						(fileDataSharesOutputBuffer && bufferSize >= requiredSize + entry->length + prototypePackedSize))
					{
						oldRLEOutput = buffer + requiredSize;
						prototypeNeedsPlanarConvert = true;
					}
					else if (dmg->zx0Scratch != 0 && dmg->zx0ScratchSize >= prototypePackedSize)
					{
						oldRLEOutput = dmg->zx0Scratch;
						prototypeNeedsPlanarConvert = true;
					}
					else
						DebugPrintf("WARNING: old-RLE prototype path skipped; no scratch buffer available for direct planar conversion");

					if (prototypeNeedsPlanarConvert && oldRLEOutput == buffer && fileDataSharesOutputBuffer && bufferSize < requiredSize + entry->length)
					{
						if (dmg->zx0Scratch != 0 && dmg->zx0ScratchSize >= entry->length && oldRLEOutput != dmg->zx0Scratch)
						{
							MemCopy(dmg->zx0Scratch, fileData, entry->length);
							oldRLEData = dmg->zx0Scratch + 2;
						}
						else
						{
							DebugPrintf("WARNING: old-RLE prototype path skipped due to overlapping input/output buffers");
							prototypeNeedsPlanarConvert = false;
							oldRLEOutput = buffer;
						}
					}
				}
				#endif
				#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_PLANAR_ASM
				if (!dmg->littleEndian)
				{
					if (fileDataSharesOutputBuffer && bufferSize < requiredSize + entry->length)
					{
						if (dmg->zx0Scratch != 0 && dmg->zx0ScratchSize >= entry->length)
						{
							MemCopy(dmg->zx0Scratch, fileData, entry->length);
							oldRLEData = dmg->zx0Scratch + 2;
						}
						else
							DebugPrintf("WARNING: old-RLE planar ASM skipped due to input/output buffer overlap");
					}
					uint32_t planeStride = requiredSize >> 2;
					if (oldRLEData != fileData + 2 || !fileDataSharesOutputBuffer || bufferSize >= requiredSize + entry->length)
					{
						#if VERIFY_OLD_RLE_PLANAR_ASM
						success = DMG_RunAndVerifyOldRLEToPlanarAsm(entry, dmg, oldRLEData, mask,
							entry->length - 2, buffer, requiredSize, requiredSize * 2, index);
						#else
						success = DecompressOldRLEToPlanarAsm(oldRLEData, entry->length - 2,
							buffer, requiredSize * 2, mask, planeStride);
						#endif
						planarMajorOutput = success;
						if (!success)
							DebugPrintf("WARNING: old-RLE planar ASM deferred to PlanarST path");
					}
					else
						success = DMG_DecompressOldRLEToST(oldRLEData, mask,
							entry->length-2, buffer, requiredSize*2, dmg->littleEndian);
				}
				else
				#endif
				#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_ORIGINAL_PROTO
				if (!dmg->littleEndian && prototypeNeedsPlanarConvert)
				{
					success = DecompressOldRLEToSTAsmOriginalPrototype(oldRLEData, entry->length - 2,
						oldRLEOutput, requiredSize * 2, mask);
					if (success)
					{
						uint32_t tConvert0, tConvert1;
						VID_GetMilliseconds(&tConvert0);
						success = DMG_ConvertOldRLEPrototypeToPlanar(entry, oldRLEOutput,
							prototypePackedSize, buffer, requiredSize);
						VID_GetMilliseconds(&tConvert1);
						prototypeToPlanarMs = tConvert1 - tConvert0;
						planarMajorOutput = success;
					}
				}
				else
				#endif
				#if defined(_AMIGA) && HAS_ASM_RLE && OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_EXPERIMENTAL_PROTO
				if (!dmg->littleEndian && prototypeNeedsPlanarConvert)
				{
					success = DecompressOldRLEToSTAsmExperimental(oldRLEData, entry->length - 2,
						oldRLEOutput, requiredSize * 2, mask);
					if (success)
					{
						uint32_t tConvert0, tConvert1;
						VID_GetMilliseconds(&tConvert0);
						success = DMG_ConvertOldRLEPrototypeToPlanar(entry, oldRLEOutput,
							prototypePackedSize, buffer, requiredSize);
						VID_GetMilliseconds(&tConvert1);
						prototypeToPlanarMs = tConvert1 - tConvert0;
						planarMajorOutput = success;
					}
				}
				else
				#endif
				{
					success = DMG_DecompressOldRLEToST(oldRLEData, mask,
						entry->length-2, oldRLEOutput, requiredSize*2, dmg->littleEndian);
				}
				VID_GetMilliseconds(&t1);
				if (planarMajorOutput)
					DebugPrintf("Decompressed old RLE image %d directly to Planar in %dms", index, t1-t0);
				else
					DebugPrintf("Decompressed old RLE image %d in %dms", index, t1-t0);
				#if defined(_AMIGA) && HAS_ASM_RLE && (OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_ORIGINAL_PROTO || OLD_RLE_ROUTINE == OLD_RLE_ROUTINE_ST_ASM_EXPERIMENTAL_PROTO)
					if (success && prototypeNeedsPlanarConvert)
						DebugPrintf("Converted old RLE prototype image %d directly to Planar in %dms (%d bytes)", index, prototypeToPlanarMs, prototypePackedSize);
				#endif
			}
			else if (!dmg->littleEndian)
			{
				DebugPrintf("Decompressing new RLE image %d (fast)", index);
				success = DMG_DecompressNewRLEPlanar(fileData+2, mask, 
					entry->length-2, buffer, entry->width, dmg->littleEndian);
			}
			else
			{
				DebugPrintf("Decompressing new RLE image %d (slow)", index);
				success = DMG_DecompressNewRLE(fileData+2, mask, 
					entry->length-2, buffer, requiredSize*2, dmg->littleEndian);
			}
		}
	}
	else
	{
		uint32_t expectedSize = DMG_CalculateRequiredSize(entry, ImageMode_Raw);
		uint32_t packedSize = DMG_CalculateRequiredSize(entry, ImageMode_Packed);
		if (entry->length != expectedSize)
		{
			DebugPrintf("WARNING: expectedSize(%d) != packedSize(%d) in image %d", expectedSize, packedSize, index);
			success = false;
		}
		else if (dmg->version == DMG_Version1_EGA)
		{
			success = DMG_UncEGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
			chunky = true;
		}
		else if (dmg->version == DMG_Version1_CGA)
		{
			success = DMG_UncCGAToPacked(fileData, entry->width, entry->height, buffer, packedSize);
			chunky = true;
		}
		else if (dmg->littleEndian)
		{
			success = DMG_CopyImageData(fileData, entry->length, buffer, packedSize);
			chunky = true;
		}
		else
		{
			uint32_t t0, t1;
			DebugPrintf("Converting raw planar image %d to PlanarST", index);
			VID_GetMilliseconds(&t0);
			DMG_Planar8To16(fileData, buffer, packedSize, entry->width);
			VID_GetMilliseconds(&t1);
			DebugPrintf("Converted raw planar image %d to PlanarST in %dms", index, t1-t0);
			success = true;
		}
	}

	if (success)
	{
		if (chunky)
		{
			uint32_t t0, t1;
			VID_GetMilliseconds(&t0);
			DMG_ConvertChunkyToPlanar(buffer, requiredSize, entry->width);
			VID_GetMilliseconds(&t1);
			DebugPrintf("Converted chunky image %d to planar in %dms", index, t1-t0);
		}

		#ifdef _AMIGA
		if (!planarMajorOutput)
		{
			uint32_t t0, t1;
			VID_GetMilliseconds(&t0);
			if (!DMG_RepackClassicPlanar(entry, buffer, requiredSize, dmg->zx0Scratch, dmg->zx0ScratchSize))
			{
				DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
				if (cache != 0)
					cache->populated = false;
				return 0;
			}
			VID_GetMilliseconds(&t1);
			DebugPrintf("Converted PlanarST image %d to Planar in %dms", index, t1-t0);
		}
		if (planarMajorOutput)
			entry->flags |= DMG_FLAG_PLANAR_MAJOR;
		if (cache != 0)
			cache->processed = true;
		#endif

		if (cache != 0)
			cache->populated = true;
	}
	else
	{
		if (cache != 0)
			cache->populated = false;
		if (DMG_GetError() == DMG_ERROR_NONE)
			DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return 0;
	}

	return buffer;
}
