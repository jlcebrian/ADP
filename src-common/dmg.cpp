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
#define DMG_MIN_FILE_SIZE 	16
#define DMG_MAX_FILE_SIZE 	0x1000000

DMG* dmg = 0;

static DMG_Error dmgError = DMG_ERROR_NONE;
static void    (*dmg_warningHandler)(const char* message) = 0;
static uint8_t*  dmgTemporaryBuffer = 0;
static uint32_t  dmgTemporaryBufferSize = 0;

static bool DMG_PreallocateZX0Scratch(DMG* dmg)
{
	uint32_t maxCompressedSize = 0;

	if (dmg == 0 || dmg->version != DMG_Version5)
		return true;

	for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
	{
		DMG_Entry* entry = dmg->entries[n];
		if (entry == 0 || entry->type != DMGEntry_Image)
			continue;
		if ((entry->flags & (DMG_FLAG_COMPRESSED | DMG_FLAG_ZX0)) != (DMG_FLAG_COMPRESSED | DMG_FLAG_ZX0))
			continue;
		if (entry->length > maxCompressedSize)
			maxCompressedSize = entry->length;
	}

	if (maxCompressedSize == 0)
		return true;

	dmg->zx0Scratch = Allocate<uint8_t>("DAT5 ZX0 scratch", maxCompressedSize, false);
	if (dmg->zx0Scratch == 0)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return false;
	}
	dmg->zx0ScratchSize = maxCompressedSize;
	dmg->zx0ScratchOwned = true;
	return true;
}

uint32_t DMG_GetTemporaryBufferSize()
{
	return dmgTemporaryBufferSize;
}

bool DMG_ReserveTemporaryBuffer(uint32_t size)
{
	if (dmgTemporaryBuffer != 0 && dmgTemporaryBufferSize >= size)
		return true;

	if (dmgTemporaryBuffer != 0)
	{
		Free(dmgTemporaryBuffer);
		dmgTemporaryBuffer = 0;
		dmgTemporaryBufferSize = 0;
	}

	dmgTemporaryBuffer = Allocate<uint8_t>("Decompression buffer", size);
	if (dmgTemporaryBuffer == 0)
		return false;

	dmgTemporaryBufferSize = size;
	return true;
}

uint8_t* DMG_GetTemporaryBuffer(DMG_ImageMode mode)
{
	uint32_t size = 
			DMG_IS_INDEXED(mode) ? 2*DMG_BUFFER_SIZE : 
			DMG_IS_RGBA32(mode) ? 4*DMG_BUFFER_SIZE : DMG_BUFFER_SIZE;
	if (!DMG_ReserveTemporaryBuffer(size))
		return 0;
	
	return dmgTemporaryBuffer;
}
void DMG_FreeTemporaryBuffer()
{
	if (dmgTemporaryBuffer)
	{
		Free(dmgTemporaryBuffer);
		dmgTemporaryBuffer = 0;
		dmgTemporaryBufferSize = 0;
	}
}

void DMG_SetZX0ScratchBuffer(DMG* d, uint8_t* buffer, uint32_t size, bool owned)
{
	if (d == 0)
		return;

	if (d->zx0Scratch != 0 && d->zx0ScratchOwned)
		Free(d->zx0Scratch);

	d->zx0Scratch = buffer;
	d->zx0ScratchSize = size;
	d->zx0ScratchOwned = owned;
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

bool DMG_UnpackChunkyPixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
    if (bitsPerPixel != 2 && bitsPerPixel != 4)
        return false;

    const uint32_t pixels = (uint32_t)width * height;
    uint32_t shift = 8 - bitsPerPixel;
    uint8_t mask = (1u << bitsPerPixel) - 1;
    uint8_t value = 0;
    int bitsLeft = 0;

    for (uint32_t i = 0; i < pixels; i++)
    {
        if (bitsLeft == 0)
        {
            value = *input++;
            bitsLeft = 8;
        }
        output[i] = (value >> shift) & mask;
        value <<= bitsPerPixel;
        bitsLeft -= bitsPerPixel;
    }
    return true;
}

bool DMG_PackChunkyPixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
    if (bitsPerPixel != 2 && bitsPerPixel != 4)
        return false;

    const uint32_t pixels = (uint32_t)width * height;
    const uint8_t mask = (1u << bitsPerPixel) - 1;
    uint8_t value = 0;
    int bitsFilled = 0;

    for (uint32_t i = 0; i < pixels; i++)
    {
        value = (uint8_t)((value << bitsPerPixel) | (input[i] & mask));
        bitsFilled += bitsPerPixel;
        if (bitsFilled == 8)
        {
            *output++ = value;
            value = 0;
            bitsFilled = 0;
        }
    }

    if (bitsFilled != 0)
        *output = (uint8_t)(value << (8 - bitsFilled));
    return true;
}

bool DMG_UnpackBitplaneBytes(const uint8_t* data, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
    if (bitsPerPixel == 0 || bitsPerPixel > 8)
        return false;

    const uint32_t wordsPerRow = (width + 15) >> 4;
    const uint32_t planeStride = wordsPerRow * height * 2;
    MemClear(output, (uint32_t)width * height);

    for (uint8_t plane = 0; plane < bitsPerPixel; plane++)
    {
        const uint8_t* planePtr = data + plane * planeStride;
        uint8_t planeMask = (uint8_t)(1u << plane);
        for (uint16_t y = 0; y < height; y++)
        {
            const uint8_t* rowPtr = planePtr + y * wordsPerRow * 2;
            uint8_t* dst = output + y * width;
            for (uint32_t wordIndex = 0; wordIndex < wordsPerRow; wordIndex++)
            {
                const uint8_t* src = rowPtr + wordIndex * 2;
                uint16_t value = (uint16_t)(src[0] << 8) | src[1];
                uint16_t baseX = (uint16_t)(wordIndex << 4);
                for (int bit = 15; bit >= 0; bit--)
                {
                    uint16_t x = (uint16_t)(baseX + (15 - bit));
                    if (x >= width)
                        break;
                    if ((value >> bit) & 1)
                        dst[x] |= planeMask;
                }
            }
        }
    }
    return true;
}

bool DMG_PackBitplaneBytes(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
    if (bitsPerPixel == 0 || bitsPerPixel > 8)
        return false;

    const uint32_t wordsPerRow = (width + 15) >> 4;
    const uint32_t planeStride = wordsPerRow * height * 2;
    MemClear(output, planeStride * bitsPerPixel);

    for (uint8_t plane = 0; plane < bitsPerPixel; plane++)
    {
        uint8_t* planePtr = output + plane * planeStride;
        uint8_t planeMask = (uint8_t)(1u << plane);
        for (uint16_t y = 0; y < height; y++)
        {
            const uint8_t* src = input + y * width;
            uint8_t* rowPtr = planePtr + y * wordsPerRow * 2;
            for (uint32_t wordIndex = 0; wordIndex < wordsPerRow; wordIndex++)
            {
                uint16_t baseX = (uint16_t)(wordIndex << 4);
                uint16_t word = 0;
                for (uint16_t bit = 0; bit < 16; bit++)
                {
                    uint16_t x = (uint16_t)(baseX + bit);
                    if (x < width && (src[x] & planeMask))
                        word |= (uint16_t)(0x8000 >> bit);
                }
                uint8_t* dst = rowPtr + wordIndex * 2;
                dst[0] = (uint8_t)(word >> 8);
                dst[1] = (uint8_t)word;
            }
        }
    }
    return true;
}

static bool DMG_ReadDAT5Entries(DMG* dmg)
{
    uint8_t header[16];
    uint32_t imageCount = 0;
    uint32_t audioCount = 0;
    uint32_t compressedCount = 0;
    uint32_t paletteCount = 0;

    if (DMG_ReadFromFile(dmg, 0, header, sizeof(header)) != sizeof(header))
    {
        DMG_SetError(DMG_ERROR_READING_FILE);
        return false;
    }

    dmg->version = DMG_Version5;
    dmg->littleEndian = false;
    dmg->targetWidth = read16BE(header + 0x06);
    dmg->targetHeight = read16BE(header + 0x08);
    dmg->firstEntry = (uint8_t)read16BE(header + 0x0A);
    dmg->lastEntry = (uint8_t)read16BE(header + 0x0C);
    dmg->colorMode = header[0x0E];

    if (dmg->lastEntry < dmg->firstEntry)
    {
        DMG_SetError(DMG_ERROR_INVALID_ENTRY_COUNT);
        return false;
    }

    if (dmg->targetWidth == 320 && dmg->targetHeight == 200)
        dmg->screenMode = (dmg->colorMode == DMG_DAT5_COLORMODE_I256) ? ScreenMode_VGA : ScreenMode_VGA16;
    else if (dmg->targetWidth == 640 && dmg->targetHeight == 200)
        dmg->screenMode = ScreenMode_HiRes;
    else if (dmg->targetWidth == 640 && dmg->targetHeight == 400)
        dmg->screenMode = ScreenMode_SHiRes;

    const uint32_t entryCount = dmg->lastEntry - dmg->firstEntry + 1;
    DebugPrintf("DAT5: reading %lu entry headers (%u-%u), mode=%u target=%ux%u\n",
        (unsigned long)entryCount,
        (unsigned)dmg->firstEntry, (unsigned)dmg->lastEntry,
        (unsigned)dmg->colorMode,
        (unsigned)dmg->targetWidth, (unsigned)dmg->targetHeight);
    dmg->entryBlock = Allocate<DMG_Entry>("DAT5 entries", entryCount);
    if (dmg->entryBlock == 0)
    {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }
    MemClear(dmg->entryBlock, sizeof(DMG_Entry) * entryCount);
    uint8_t* buffer = Allocate<uint8_t>("DAT5 entry headers", entryCount * 32);
    if (buffer == 0)
    {
        Free(dmg->entryBlock);
        dmg->entryBlock = 0;
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }
    if (DMG_ReadFromFile(dmg, 0x10, buffer, entryCount * 32) != entryCount * 32)
    {
        Free(buffer);
        Free(dmg->entryBlock);
        dmg->entryBlock = 0;
        DMG_SetError(DMG_ERROR_READING_FILE);
        return false;
    }

    for (uint32_t i = 0; i < entryCount; i++)
    {
        uint8_t index = (uint8_t)(dmg->firstEntry + i);
        uint8_t* ptr = buffer + i * 32;
        uint32_t offset = read32BE(ptr + 0x00);
        uint32_t size = read32BE(ptr + 0x04);
        uint8_t type = ptr[0x08];

        DMG_Entry* entry = dmg->entryBlock + i;
        dmg->entries[index] = entry;
        entry->type = (offset == 0) ? DMGEntry_Empty : (type == 1 ? DMGEntry_Audio : DMGEntry_Image);
        entry->fileOffset = offset;
        entry->length = size;
        entry->flags = DMG_FLAG_PROCESSED;

        if (entry->type == DMGEntry_Empty)
            continue;

        if (entry->type == DMGEntry_Image) imageCount++;
        else if (entry->type == DMGEntry_Audio) audioCount++;

        if (offset + size > dmg->fileSize)
        {
            Free(buffer);
            DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
            return false;
        }

        if (entry->type == DMGEntry_Image)
        {
            uint16_t flags = read16BE(ptr + 0x0A);
            entry->bitDepth = ptr[0x09];
            entry->x = (int16_t)read16BE(ptr + 0x0C);
            entry->y = (int16_t)read16BE(ptr + 0x0E);
            entry->width = read16BE(ptr + 0x10);
            entry->height = read16BE(ptr + 0x12);
            entry->firstColor = ptr[0x14];
            entry->lastColor = ptr[0x15];
            entry->paletteOffset = 0;
            entry->paletteColors = entry->lastColor >= entry->firstColor ? (uint16_t)(entry->lastColor - entry->firstColor + 1) : 0;
            entry->paletteSize = entry->paletteColors * 3;

            if ((flags & 0x0008) != 0)
            {
                entry->flags |= DMG_FLAG_COMPRESSED;
                entry->flags |= DMG_FLAG_ZX0;
                compressedCount++;
            }
            if ((flags & 0x0010) == 0)
                entry->flags |= DMG_FLAG_FIXED;
            if ((flags & 0x0020) != 0)
                entry->flags |= DMG_FLAG_BUFFERED;
            if ((flags & 0x0040) != 0)
                DMG_SetCGAMode(entry, CGA_Blue);
            else
                DMG_SetCGAMode(entry, CGA_Red);

            if (entry->paletteSize != 0)
            {
                paletteCount++;
                if (entry->length < entry->paletteSize)
                {
                    Free(buffer);
                    DMG_SetError(DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS);
                    return false;
                }
            }
        }
        else
        {
            entry->x = ptr[0x09];
            if ((read16BE(ptr + 0x0A) & 0x0001) != 0)
                entry->bitDepth = 16;
        }
    }

    Free(buffer);
    DebugPrintf("DAT5: loaded %lu image(s), %lu audio sample(s), %lu compressed, %lu palette prefix(es)\n",
        (unsigned long)imageCount,
        (unsigned long)audioCount,
        (unsigned long)compressedCount,
        (unsigned long)paletteCount);
    return true;
}

static bool DMG_DecodeDAT5PaletteBytes(DMG_Entry* entry, const uint8_t* palData)
{
    if (entry == 0 || entry->paletteColors == 0 || entry->paletteSize == 0)
        return true;

    if (entry->RGB32PaletteV5 == 0)
    {
        entry->RGB32PaletteV5 = Allocate<uint32_t>("DAT5 palette", entry->paletteColors);
        if (entry->RGB32PaletteV5 == 0)
        {
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            return false;
        }
    }

    for (uint16_t p = 0; p < entry->paletteColors; p++)
    {
        entry->RGB32PaletteV5[p] =
            0xFF000000 |
            (palData[p * 3 + 0] << 16) |
            (palData[p * 3 + 1] << 8) |
            (palData[p * 3 + 2] << 0);
    }
    return true;
}

static bool DMG_LoadDAT5Palette(DMG* dmg, uint8_t index, DMG_Entry* entry)
{
    if (dmg == 0 || entry == 0 || entry->paletteColors == 0 || entry->paletteSize == 0)
        return true;
    if (entry->RGB32PaletteV5 != 0)
    {
        DebugPrintf("DAT5 palette %u: cached (%u colors)\n", (unsigned)index, (unsigned)entry->paletteColors);
        return true;
    }

    const uint8_t* fileData = (const uint8_t*)DMG_GetFromFileCache(dmg, entry->fileOffset, entry->paletteColors * 3);
    if (fileData != 0)
    {
        DebugPrintf("DAT5 palette %u: file cache hit at %lu (%lu bytes)\n",
            (unsigned)index, (unsigned long)entry->fileOffset, (unsigned long)(entry->paletteColors * 3));
        return DMG_DecodeDAT5PaletteBytes(entry, fileData);
    }

    uint8_t palData[256 * 3];
    if (entry->paletteColors > 256)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }
    if (DMG_ReadFromFile(dmg, entry->fileOffset, palData, entry->paletteColors * 3) != entry->paletteColors * 3)
    {
        DMG_SetError(DMG_ERROR_READING_FILE);
        return false;
    }
    DebugPrintf("DAT5 palette %u: file read at %lu (%lu bytes)\n",
        (unsigned)index, (unsigned long)entry->fileOffset, (unsigned long)(entry->paletteColors * 3));
    return DMG_DecodeDAT5PaletteBytes(entry, palData);
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
	if (dmg->version == DMG_Version5)
		return dmg->entries[n];
	if ((dmg->entries[n]->flags & DMG_FLAG_PROCESSED) != 0)
		return dmg->entries[n];

	if (DMG_ReadFromFile(dmg, dmg->entries[n]->fileOffset, header, 6) != 6)
	{
		DMG_SetError(DMG_ERROR_READING_FILE);
		return 0;
	}

	v = read16(header, dmg->littleEndian);
	dmg->entries[n]->width = v & 0x7FFF;
    if ((v & 0x8000) != 0)
	    dmg->entries[n]->flags |= DMG_FLAG_COMPRESSED;
	v = read16(header + 2, dmg->littleEndian);
    // if ((v & 0x8000) != 0)   // Should already be correctly set from flags
    //     dmg->entries[n]->type = DMGEntry_Audio;
	dmg->entries[n]->height = v & 0x7FFF;
	dmg->entries[n]->length = read16(header + 4, dmg->littleEndian);

	// Actual limits should be related to screen mode
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

	dmg->entries[n]->flags |= DMG_FLAG_PROCESSED;
	return dmg->entries[n];
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  File management														     */
/* ───────────────────────────────────────────────────────────────────────── */

bool DMG_ReadV1DOSEntries(DMG* dmg, DMG_Version version)
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
		dmg->entries[n]->bitDepth = version == DMG_Version1_CGA ? 2 : 4;
		dmg->entries[n]->flags = 0;
        if ((flags & 0x0002) != 0)
		    dmg->entries[n]->flags |= DMG_FLAG_BUFFERED;
        if ((flags & 0x0001) == 0)
            dmg->entries[n]->flags |= DMG_FLAG_FIXED;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->flags &= ~DMG_FLAG_PROCESSED;

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

bool DMG_ReadV1Entries(DMG* dmg)
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
		dmg->entries[n]->bitDepth = 4;
		dmg->entries[n]->flags = 0;
        if ((flags & 0x0002) != 0)
		    dmg->entries[n]->flags |= DMG_FLAG_BUFFERED;
        if ((flags & 0x0001) == 0)
            dmg->entries[n]->flags |= DMG_FLAG_FIXED;
		dmg->entries[n]->firstColor = firstColor;
		dmg->entries[n]->lastColor = lastColor;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->flags &= ~DMG_FLAG_PROCESSED;

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

bool DMG_ReadV2Entries(DMG* dmg)
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
		dmg->entries[n]->bitDepth = 4;
		dmg->entries[n]->flags = 0;
        if ((flags & 0x0002) != 0)
            dmg->entries[n]->flags |= DMG_FLAG_BUFFERED;
        if ((flags & 0x0001) == 0)
            dmg->entries[n]->flags |= DMG_FLAG_FIXED;
		dmg->entries[n]->firstColor = firstColor;
		dmg->entries[n]->lastColor = lastColor;
		dmg->entries[n]->x = x;
		dmg->entries[n]->y = y;
		dmg->entries[n]->fileOffset = offset;
		dmg->entries[n]->flags &= ~DMG_FLAG_PROCESSED;

		if (offset == 0)
			dmg->entries[n]->type = DMGEntry_Empty;

		for (p = 0; p < 16; p++) 
		{
			uint16_t color = read16BE(ptr + 12 + p * 2);
			dmg->entries[n]->RGB32Palette[p] = Pal2RGB(color, amigaPaletteHack);
			dmg->entries[n]->EGAPalette[p] = amigaPaletteHack ? 0 : ((color >> 12) & 0x0F);
			dmg->entries[n]->CGAPalette[p] = amigaPaletteHack ? 0 : ((CGAColors >> (2*p)) & 0x03);
		}
        if (amigaPaletteHack)
            dmg->entries[n]->flags |= DMG_FLAG_AMIPALHACK;
        else
            dmg->entries[n]->flags &= ~DMG_FLAG_AMIPALHACK;
        DMG_SetCGAMode(dmg->entries[n], (flags & 0x0001) ? CGA_Red : CGA_Blue);
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
	DebugPrintf("DMG_Open(%s): size=%lu bytes\n", filename, (unsigned long)fileSize);
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
    MemClear(d, sizeof(DMG));
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

	if (header[0] == 'D' && header[1] == 'A' && header[2] == 'T' && header[3] == 0 && header[4] == 0 && header[5] == 5)
	{
		DebugPrintf("DMG_Open: detected DAT5 header\n");
		success = DMG_ReadDAT5Entries(d);
		entryCount = d->lastEntry >= d->firstEntry ? (uint16_t)(d->lastEntry - d->firstEntry + 1) : 0;
	}
	else switch (signature)
	{
		case 0x0004:
			// Old version DAT file, big endian
			d->version = DMG_Version1;
			success = DMG_ReadV1Entries(d);
			break;

		case 0x0300:
			// New version DAT file, big endian
			d->version = DMG_Version2;
			d->littleEndian = false;
			d->screenMode = read16BE(header + 2);
			success = DMG_ReadV2Entries(d);
			break;

		case 0xFFFF:
			// New version DAT file, little endian
			d->version = DMG_Version2;
			d->littleEndian = true;
			d->screenMode = read16BE(header + 2);
			success = DMG_ReadV2Entries(d);
			break;

		case 0x0000:
			if (StrIComp(extension, ".ega") == 0)
			{
				d->version = DMG_Version1_EGA;
				d->littleEndian = true;
				d->screenMode = ScreenMode_EGA;
				success = DMG_ReadV1DOSEntries(d, DMG_Version1_EGA);
				break;
			}
			else if (StrIComp(extension, ".cga") == 0)
			{
				d->version = DMG_Version1_CGA;
				d->littleEndian = true;
				d->screenMode = ScreenMode_CGA;
				success = DMG_ReadV1DOSEntries(d, DMG_Version1_CGA);
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

	if (!DMG_PreallocateZX0Scratch(d))
	{
		DMG_Close(d);
		return 0;
	}
	DebugPrintf("DMG_Open: header/entries ready (version=%d screenMode=%d)\n",
		(int)d->version, (int)d->screenMode);

	if (d->version != DMG_Version5)
    {
		entryCount = read16(header + 4, d->littleEndian);
    	realEntryCount = DMG_GetEntryCount(d);
	    if (entryCount != realEntryCount)
		    DMG_Warning("Invalid entry count %d on file header (expected %d)", entryCount, realEntryCount);
    }

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
		return DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
	else if (DMG_IS_EGA(mode))
		return EGAPalette;
    else if (dmg->version == DMG_Version5)
    {
        if (!DMG_LoadDAT5Palette(dmg, index, entry))
            return 0;
        return entry->RGB32PaletteV5;
    }
	else
		return entry->RGB32Palette;
}

uint16_t DMG_GetEntryPaletteSize(DMG* dmg, uint8_t index)
{
    DMG_Entry* entry = DMG_GetEntry(dmg, index);
    if (entry == 0)
        return 0;
    if (dmg->version == DMG_Version5)
    {
        if (dmg->colorMode == DMG_DAT5_COLORMODE_CGA)
            return 4;
        if (dmg->colorMode == DMG_DAT5_COLORMODE_EGA)
            return 16;
        return entry->paletteColors;
    }
    return 16;
}

uint8_t DMG_GetEntryFirstColor(DMG* dmg, uint8_t index)
{
    DMG_Entry* entry = DMG_GetEntry(dmg, index);
    if (entry == 0)
        return 0;
    return entry->firstColor;
}

void DMG_Close(DMG* d)
{
#ifndef NO_CACHE
	DMG_FreeImageCache(d);
#endif

    for (int i = 0; i < 256; i++)
    {
        if (d->entries[i] != 0 && d->entries[i]->RGB32PaletteV5 != 0)
            Free(d->entries[i]->RGB32PaletteV5);
    }
    if (d->entryBlock != 0)
        Free(d->entryBlock);
    else
    {
        for (int i = 0; i < 256; i++)
        {
            if (d->entries[i] != 0)
                Free(d->entries[i]);
        }
    }

    if (d->zx0Scratch != 0 && d->zx0ScratchOwned)
        Free(d->zx0Scratch);

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
            if (dmg != 0 && dmg->version == DMG_Version5)
            {
                if (entry->bitDepth <= 4)
                    return (width * height + 1) / 2;
                return width * height;
            }
			return width * height / 2;

		case ImageMode_Planar:
            return ((width + 7) >> 3) * height * entry->bitDepth;

		case ImageMode_PlanarST:
            if (dmg != 0 && dmg->version == DMG_Version5)
                return ((width + 7) >> 3) * height * entry->bitDepth;
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
            else if (dmg != 0 && dmg->version == DMG_Version5)
            {
                if (entry->bitDepth <= 4)
                    return (width * height + 1) / 2;
                return width * height;
            }
			else if (dmg->screenMode == ScreenMode_CGA)
				return width * height / 4;
			return width * height / 2;

		default:
			return width * height / 2;
	}
}
