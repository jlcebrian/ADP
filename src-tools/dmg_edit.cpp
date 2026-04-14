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

static bool DMG_UpdateEntryV5(DMG* dmg, uint8_t index);
static bool DMG_InsertBlock(DMG* dmg, uint32_t offset, uint32_t size);
static bool DMG_LoadEntryStoredData(DMG* dmg, DMG_Entry* entry);
static bool DMG_WriteClassicPayload(File* file, DMG* dmg, DMG_Entry* entry);
static bool DMG_WriteDAT5Payload(File* file, DMG* dmg, uint8_t index, DMG_Entry* entry);

static void DMG_MarkDirty(DMG* dmg)
{
    if (dmg != 0)
        dmg->dirty = true;
}

static void DMG_FreeStoredData(DMG_Entry* entry)
{
    if (entry != 0 && entry->storedData != 0 && entry->ownsStoredData)
        Free(entry->storedData);
    if (entry != 0)
    {
        entry->storedData = 0;
        entry->storedDataSize = 0;
        entry->ownsStoredData = false;
    }
}

static bool DMG_StoreEntryData(DMG_Entry* entry, const uint8_t* buffer, uint32_t size)
{
    DMG_FreeStoredData(entry);
    if (size == 0)
        return true;

    entry->storedData = Allocate<uint8_t>("DMG entry data", size);
    if (entry->storedData == 0)
    {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }
    MemCopy(entry->storedData, buffer, size);
    entry->storedDataSize = size;
    entry->ownsStoredData = true;
    return true;
}

static bool DMG_LoadEntryStoredData(DMG* dmg, DMG_Entry* entry)
{
    if (dmg == 0 || entry == 0 || entry->type == DMGEntry_Empty)
        return true;
    if (entry->storedData != 0)
        return true;

    uint32_t offset = entry->fileOffset;
    uint32_t size = entry->length;
    if (dmg->version == DMG_Version5)
    {
        if (entry->type == DMGEntry_Image)
        {
            uint32_t paletteBytes = entry->paletteColors * 3;
            if (entry->length < paletteBytes)
            {
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return false;
            }
            offset += paletteBytes;
            size -= paletteBytes;
        }
    }
    else
    {
        offset += 6;
    }

    if (size == 0)
        return true;

    uint8_t* data = Allocate<uint8_t>("DMG entry data", size);
    if (data == 0)
    {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }
    if (DMG_ReadFromFile(dmg, offset, data, size) != size)
    {
        Free(data);
        DMG_SetError(DMG_ERROR_READING_FILE);
        return false;
    }
    entry->storedData = data;
    entry->storedDataSize = size;
    entry->ownsStoredData = true;
    return true;
}

static bool DMG_WriteZeroBlock(DMG* dmg, uint32_t offset, uint32_t size)
{
	uint8_t* buffer = DMG_GetTemporaryBuffer(ImageMode_RGBA32);
	uint32_t bufferSize = DMG_GetTemporaryBufferSize();
	if (buffer == 0 || bufferSize == 0)
	{
		DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
		return false;
	}

	MemClear(buffer, bufferSize);
	while (size > 0)
	{
		uint32_t chunkSize = size > bufferSize ? bufferSize : size;
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
		offset += chunkSize;
		size -= chunkSize;
	}
	return true;
}

static bool DMG_EnsureDAT5EntryRange(DMG* dmg, uint8_t index)
{
	if (dmg == 0 || dmg->version != DMG_Version5)
		return true;
	if (index >= dmg->firstEntry && index <= dmg->lastEntry)
		return true;

	uint8_t originalFirst = dmg->firstEntry;
	uint8_t originalLast = dmg->lastEntry;

	if (index < originalFirst)
	{
		uint32_t insertEntries = (uint32_t)(originalFirst - index);
		uint32_t insertSize = insertEntries * 32;
		dmg->firstEntry = index;
		if (!DMG_InsertBlock(dmg, 0x10, insertSize))
		{
			dmg->firstEntry = originalFirst;
			return false;
		}
		if (!DMG_WriteZeroBlock(dmg, 0x10, insertSize))
			return false;
	}

	if (index > originalLast)
	{
		uint32_t oldCount = (uint32_t)(dmg->lastEntry - dmg->firstEntry + 1);
		uint32_t insertEntries = (uint32_t)(index - originalLast);
		uint32_t insertOffset = 0x10 + oldCount * 32;
		uint32_t insertSize = insertEntries * 32;
		dmg->lastEntry = index;
		if (!DMG_InsertBlock(dmg, insertOffset, insertSize))
		{
			dmg->lastEntry = originalLast;
			return false;
		}
		if (!DMG_WriteZeroBlock(dmg, insertOffset, insertSize))
			return false;
	}

	return DMG_UpdateFileHeader(dmg);
}

static bool DMG_GetDAT5UsedRange(DMG* dmg, uint8_t* first, uint8_t* last)
{
    uint8_t usedFirst = 255;
    uint8_t usedLast = 0;
    bool found = false;

    if (dmg == 0 || dmg->version != DMG_Version5)
        return false;

    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
    {
        DMG_Entry* entry = dmg->entries[n];
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (!found)
        {
            usedFirst = (uint8_t)n;
            usedLast = (uint8_t)n;
            found = true;
        }
        else
        {
            if (n < usedFirst) usedFirst = (uint8_t)n;
            if (n > usedLast) usedLast = (uint8_t)n;
        }
    }

    if (!found)
        return false;
    if (first) *first = usedFirst;
    if (last) *last = usedLast;
    return true;
}

static void DMG_TruncateFile(DMG* dmg, uint32_t size)
{
	File_Truncate(dmg->file, size);
	dmg->fileSize = size;
}

static bool DMG_BlockOverlaps(uint32_t offsetA, uint32_t sizeA, uint32_t offsetB, uint32_t sizeB)
{
    if (sizeA == 0 || sizeB == 0)
        return false;
    return offsetA < offsetB + sizeB && offsetA + sizeA > offsetB;
}

static bool DMG_UpdateOffsets (DMG* dmg, uint32_t from, uint32_t to, bool add, int32_t value)
{
    if (dmg->version == DMG_Version5)
    {
        for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
        {
            DMG_Entry* entry = dmg->entries[n];
            bool changed = false;
            if (entry == 0)
                continue;

            if (entry->fileOffset >= from && entry->fileOffset < to)
            {
                entry->fileOffset = add ? entry->fileOffset + value : value;
                changed = true;
            }
            if (changed && !DMG_UpdateEntryV5(dmg, n))
                return false;
        }
        return true;
    }

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
	uint32_t oldFileSize = dmg->fileSize;
	uint32_t remaining = dmg->fileSize - offset;
	uint32_t start = offset;

	if (remaining <= size)
	{
		DMG_TruncateFile(dmg, offset);
		DMG_SetOffsets(dmg, offset, oldFileSize, offset);
		return true;
	}

	remaining -= size;

	while (remaining > 0)
	{
		uint32_t chunkSize = remaining > bufferSize ? bufferSize : remaining;
		uint32_t chunkOffset = offset + size;

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
	DMG_AddValueToOffsets(dmg, start, oldFileSize, -size);
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

static bool DMG_UpdateEntryV5(DMG* dmg, uint8_t index)
{
    if (dmg == 0 || index < dmg->firstEntry || index > dmg->lastEntry)
    {
        DMG_SetError(DMG_ERROR_INVALID_ENTRY_COUNT);
        return false;
    }
    return true;
}

bool DMG_UpdateFileHeader (DMG* dmg)
{
    if (dmg == 0)
        return false;
    DMG_MarkDirty(dmg);
    return true;
}

static bool DMG_IsDAT5BlockInUse(DMG* dmg, uint8_t index, uint32_t offset, uint32_t size)
{
    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
    {
        if (n == index)
            continue;
        DMG_Entry* entry = dmg->entries[n];
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (DMG_BlockOverlaps(offset, size, entry->fileOffset, entry->length))
            return true;
    }
    return false;
}

static bool DMG_RemoveDAT5EntryBlocks(DMG* dmg, uint8_t index)
{
    DMG_Entry* entry = dmg->entries[index];
    if (entry == 0 || entry->type == DMGEntry_Empty)
        return true;

    uint32_t blockOffset[1];
    uint32_t blockSize[1];
    int blockCount = 0;

    if (entry->fileOffset != 0 && entry->length != 0 &&
        !DMG_IsDAT5BlockInUse(dmg, index, entry->fileOffset, entry->length))
    {
        blockOffset[blockCount] = entry->fileOffset;
        blockSize[blockCount] = entry->length;
        blockCount++;
    }

    for (int i = 0; i < blockCount; i++)
    {
        if (!DMG_RemoveBlock(dmg, blockOffset[i], blockSize[i]))
            return false;
    }
    return true;
}

bool DMG_UpdateEntry (DMG* dmg, uint8_t index)
{
    if (dmg == 0 || dmg->entries[index] == 0)
        return false;
    DMG_MarkDirty(dmg);
    if (dmg->version == DMG_Version5)
        return DMG_UpdateEntryV5(dmg, index);
    return true;
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
        DMG_FreeStoredData(entry);
        entry->fileOffset = 0;
        entry->length = 0;
        entry->paletteOffset = 0;
        entry->paletteColors = 0;
        entry->paletteSize = 0;
        if (entry->RGB32PaletteV5 != 0)
        {
            Free(entry->RGB32PaletteV5);
            entry->RGB32PaletteV5 = 0;
        }
        entry->type = DMGEntry_Empty;
        return DMG_UpdateEntry(dmg, index);
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
    return DMG_SetEntryPaletteEx(dmg, index, palette, 16);
}

bool DMG_SetEntryPaletteEx (DMG* dmg, uint8_t index, uint32_t* palette, uint16_t paletteSize)
{
    return DMG_SetEntryPaletteRange(dmg, index, palette, paletteSize, 0, paletteSize == 0 ? 0 : (uint8_t)(paletteSize - 1));
}

bool DMG_SetEntryPaletteRange(DMG* dmg, uint8_t index, uint32_t* palette, uint16_t paletteSize, uint8_t firstColor, uint8_t lastColor)
{
	if (dmg->version == DMG_Version5 && !DMG_EnsureDAT5EntryRange(dmg, index))
		return false;

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

    if (dmg->version == DMG_Version5)
    {
        if ((paletteSize == 0 && (firstColor != 0 || lastColor != 0)) ||
            (paletteSize != 0 && lastColor < firstColor) ||
            (paletteSize != 0 && (uint16_t)(lastColor - firstColor + 1) != paletteSize))
        {
            DMG_SetError(DMG_ERROR_INVALID_ENTRY_COUNT);
            return false;
        }
        if (entry->RGB32PaletteV5 != 0)
            Free(entry->RGB32PaletteV5);
        entry->RGB32PaletteV5 = 0;
        entry->paletteColors = paletteSize;
        entry->firstColor = paletteSize == 0 ? 0 : firstColor;
        entry->lastColor = paletteSize == 0 ? 0 : lastColor;
        entry->paletteSize = paletteSize * 3;
        entry->paletteOffset = 0;

        if (paletteSize != 0)
        {
            entry->RGB32PaletteV5 = Allocate<uint32_t>("DAT5 palette", paletteSize);
            if (entry->RGB32PaletteV5 == 0)
            {
                DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
                return false;
            }
            for (n = 0; n < paletteSize; n++)
                entry->RGB32PaletteV5[n] = palette[n];
        }
        DMG_MarkDirty(dmg);
        return true;
    }

	for (n = 0; n < 16; n++)
		entry->RGB32Palette[n] = palette[n];
	DMG_MarkDirty(dmg);
	return true;
}

bool DMG_SetAudioData (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t size, DMG_KHZ freq)
{
	if (dmg->version == DMG_Version5 && !DMG_EnsureDAT5EntryRange(dmg, index))
		return false;

	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	uint8_t header[6];

    if (dmg->version == DMG_Version1_CGA ||
        dmg->version == DMG_Version1_EGA ||
        dmg->version == DMG_Version1_PCW)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    if (dmg->version == DMG_Version5)
    {
		if (entry == 0)
		{
			if (dmg->entries[index] == 0)
			{
				dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
				if (dmg->entries[index] == 0)
				{
					DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
					return false;
				}
				entry = dmg->entries[index];
				MemClear(entry, sizeof(DMG_Entry));
				entry->type = DMGEntry_Empty;
			}
			else
				return false;
		}

        if (entry->type != DMGEntry_Empty)
            DMG_FreeStoredData(entry);
        if (!DMG_StoreEntryData(entry, buffer, size))
            return false;
        entry->type = DMGEntry_Audio;
        entry->fileOffset = 0;
        entry->length = size;
        entry->x = freq;
        entry->flags |= DMG_FLAG_PROCESSED;
        return DMG_UpdateEntry(dmg, index);
    }

    if (entry == 0)
    {
        if (dmg->entries[index] == 0)
        {
            dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
            if (dmg->entries[index] == 0)
            {
                DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
                return false;
            }
            entry = dmg->entries[index];
            MemClear(entry, sizeof(DMG_Entry));
            entry->type = DMGEntry_Empty;
        }
        else
            return false;
    }

	if (entry->type != DMGEntry_Empty)
	{
        DMG_FreeStoredData(entry);
    }
    if (!DMG_StoreEntryData(entry, buffer, size))
        return false;

	entry->type = DMGEntry_Audio;
	entry->fileOffset = 0;
	entry->length = size;
	entry->x = freq;
    entry->flags |= DMG_FLAG_PROCESSED;
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
    return DMG_SetImageDataEx(dmg, index, buffer, width, height, size, compressed, 4);
}

bool DMG_SetImageDataEx (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t width, uint16_t height, uint32_t size, bool compressed, uint8_t bitDepth)
{
	if (dmg->version == DMG_Version5 && !DMG_EnsureDAT5EntryRange(dmg, index))
		return false;

	DMG_Entry* entry = DMG_GetEntry(dmg, index);
	uint8_t header[6];

    if (dmg->version == DMG_Version5)
    {
		if (entry == 0)
		{
			if (dmg->entries[index] == 0)
			{
				dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
				if (dmg->entries[index] == 0)
				{
					DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
					return false;
				}
				entry = dmg->entries[index];
				MemClear(entry, sizeof(DMG_Entry));
				entry->type = DMGEntry_Empty;
			}
			else
				return false;
		}

        if (entry->type != DMGEntry_Empty)
            DMG_FreeStoredData(entry);
        if (!DMG_StoreEntryData(entry, buffer, size))
            return false;

        entry->type = DMGEntry_Image;
        entry->bitDepth = bitDepth;
        entry->width = width;
        entry->height = height;
        entry->paletteSize = entry->paletteColors * 3;
        entry->length = entry->paletteSize + size;
        entry->flags &= ~(DMG_FLAG_COMPRESSED | DMG_FLAG_ZX0);
        if (compressed)
            entry->flags |= DMG_FLAG_COMPRESSED | DMG_FLAG_ZX0;
        entry->paletteOffset = 0;
        entry->fileOffset = 0;
        entry->flags |= DMG_FLAG_PROCESSED;
        return DMG_UpdateEntry(dmg, index);
    }

    if (entry == 0)
    {
        if (dmg->entries[index] == 0)
        {
            dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
            if (dmg->entries[index] == 0)
            {
                DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
                return false;
            }
            entry = dmg->entries[index];
            MemClear(entry, sizeof(DMG_Entry));
            entry->type = DMGEntry_Empty;
        }
        else
            return false;
    }

	if (entry->type != DMGEntry_Empty)
	{
        DMG_FreeStoredData(entry);
	}
    if (!DMG_StoreEntryData(entry, buffer, size))
        return false;

	entry->type = DMGEntry_Image;
    entry->bitDepth = bitDepth;
	entry->fileOffset = 0;
	entry->width = width;
	entry->height = height;
	entry->length = size;
    if (compressed)
        entry->flags |= DMG_FLAG_COMPRESSED;
    else
        entry->flags &= ~DMG_FLAG_COMPRESSED;
    entry->flags |= DMG_FLAG_PROCESSED;
	return DMG_UpdateEntry(dmg, index);
}

DMG* DMG_CreateDAT5(const char* filename, DMG_DAT5ColorMode colorMode, uint16_t width, uint16_t height, uint8_t firstEntry, uint8_t lastEntry)
{
    File* file = File_Create(filename);
    if (file == NULL)
    {
        DMG_SetError(DMG_ERROR_CREATING_FILE);
        return NULL;
    }

    uint8_t header[16];
    MemClear(header, sizeof(header));
    header[0] = 'D';
    header[1] = 'A';
    header[2] = 'T';
    header[4] = 0;
    header[5] = 5;
    write16(header + 0x06, width, false);
    write16(header + 0x08, height, false);
    write16(header + 0x0A, firstEntry, false);
    write16(header + 0x0C, lastEntry, false);
    header[0x0E] = colorMode;

    if (File_Write(file, header, sizeof(header)) != sizeof(header))
    {
        File_Close(file);
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        return NULL;
    }

    uint32_t entryCount = lastEntry - firstEntry + 1;
    uint8_t* emptyHeaders = Allocate<uint8_t>("DAT5 empty headers", entryCount * 32);
    if (emptyHeaders == NULL)
    {
        File_Close(file);
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    MemClear(emptyHeaders, entryCount * 32);
    if (File_Write(file, emptyHeaders, entryCount * 32) != entryCount * 32)
    {
        Free(emptyHeaders);
        File_Close(file);
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        return NULL;
    }
    Free(emptyHeaders);

    DMG* dmg = Allocate<DMG>("DMG");
    if (dmg == NULL)
    {
        File_Close(file);
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    MemClear(dmg, sizeof(DMG));
    dmg->file = file;
    dmg->fileSize = 16 + entryCount * 32;
    dmg->dirty = false;
    dmg->version = DMG_Version5;
    dmg->littleEndian = false;
    dmg->colorMode = colorMode;
    dmg->targetWidth = width;
    dmg->targetHeight = height;
    dmg->firstEntry = firstEntry;
    dmg->lastEntry = lastEntry;
    dmg->screenMode =
        (width == 640 && height == 400) ? ScreenMode_SHiRes :
        (width == 640 && height == 200) ? ScreenMode_HiRes :
		(colorMode == DMG_DAT5_COLORMODE_CGA) ? ScreenMode_CGA :
		(colorMode == DMG_DAT5_COLORMODE_EGA) ? ScreenMode_EGA :
		(DMG_DAT5ModePlaneCount(colorMode) >= 8) ? ScreenMode_VGA :
		ScreenMode_VGA16;

    dmg->entryBlock = Allocate<DMG_Entry>("DAT5 entries", entryCount);
    if (dmg->entryBlock == NULL)
    {
        DMG_Close(dmg);
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    MemClear(dmg->entryBlock, sizeof(DMG_Entry) * entryCount);

    for (uint32_t i = firstEntry; i <= lastEntry; i++)
    {
        dmg->entries[i] = dmg->entryBlock + (i - firstEntry);
        dmg->entries[i]->type = DMGEntry_Empty;
        dmg->entries[i]->flags = DMG_FLAG_PROCESSED;
    }
    return dmg;
}

DMG* DMG_CreateFormat(const char* filename, DMG_Version version)
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

    int entrySize = 48;
    int headerSize = 10;
    uint16_t signature = 0x0300;
    bool writeFileSize = true;
    bool littleEndian = false;
    DDB_ScreenMode screenMode = ScreenMode_VGA16;
    uint32_t initialFileSize = 0x300A;

    switch (version)
    {
        case DMG_Version1:
            entrySize = 44;
            headerSize = 6;
            signature = 0x0004;
            initialFileSize = 0x2C06;
            writeFileSize = false;
            littleEndian = false;
            screenMode = ScreenMode_VGA16;
            break;
        case DMG_Version2:
            entrySize = 48;
            headerSize = 10;
            signature = 0x0300;
            initialFileSize = 0x300A;
            writeFileSize = true;
            littleEndian = false;
            screenMode = ScreenMode_VGA16;
            break;
        case DMG_Version1_EGA:
            entrySize = 10;
            headerSize = 6;
            signature = 0x0000;
            initialFileSize = 0x0A06;
            writeFileSize = false;
            littleEndian = true;
            screenMode = ScreenMode_EGA;
            break;
        case DMG_Version1_CGA:
            entrySize = 10;
            headerSize = 6;
            signature = 0x0000;
            initialFileSize = 0x0A06;
            writeFileSize = false;
            littleEndian = true;
            screenMode = ScreenMode_CGA;
            break;
        case DMG_Version1_PCW:
            entrySize = 10;
            headerSize = 6;
            signature = 0x0000;
            initialFileSize = 0x0A06;
            writeFileSize = false;
            littleEndian = true;
            screenMode = ScreenMode_HiRes;
            break;
        default:
            DMG_SetError(DMG_ERROR_CREATING_FILE);
            File_Close(file);
            return NULL;
    }

    MemClear(header, sizeof(header));
    write16(header + 0x00, signature, false);
    write16(header + 0x02, version == DMG_Version1_PCW ? 0x0004 : screenMode, false);
    write16(header + 0x04, 0, littleEndian);
    if (writeFileSize)
        write32(header + 0x06, initialFileSize, littleEndian);
    if (File_Write(file, header, headerSize) != (uint32_t)headerSize)
    {
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        File_Close(file);
        return NULL;
    }

	dmg = Allocate<DMG>("DMG");
	if (dmg == NULL)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
    MemClear(dmg, sizeof(DMG));
	dmg->version = version;
	dmg->screenMode = screenMode;
	dmg->littleEndian = littleEndian;
	dmg->file = file;
	dmg->fileSize = initialFileSize;
    dmg->dirty = false;
	for (int n = 0; n < 256; n++)
	{
		dmg->entries[n] = Allocate<DMG_Entry>("DMG Entry");
		if (dmg->entries[n] == 0)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			DMG_Close(dmg);
			return 0;
		}
        MemClear(dmg->entries[n], sizeof(DMG_Entry));
        dmg->entries[n]->type = DMGEntry_Empty;
	}

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

DMG* DMG_Create(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    if (dot != 0)
    {
        if (stricmp(dot, ".ega") == 0)
            return DMG_CreateFormat(filename, DMG_Version1_EGA);
        if (stricmp(dot, ".cga") == 0)
            return DMG_CreateFormat(filename, DMG_Version1_CGA);
    }
    return DMG_CreateFormat(filename, DMG_Version2);
}

bool DMG_ReuseEntryData(DMG* dmg, uint8_t index, uint8_t originalIndex)
{
    if (dmg == 0 || index == originalIndex)
        return false;

    DMG_Entry* source = DMG_GetEntry(dmg, originalIndex);
    if (source == 0 || source->type == DMGEntry_Empty)
    {
        DMG_SetError(DMG_ERROR_ENTRY_IS_EMPTY);
        return false;
    }

    if (dmg->version == DMG_Version5 && !DMG_EnsureDAT5EntryRange(dmg, index))
        return false;

    DMG_Entry* target = DMG_GetEntry(dmg, index);
    if (target == 0)
    {
        if (dmg->entries[index] == 0)
        {
            dmg->entries[index] = Allocate<DMG_Entry>("DMG Entry");
            if (dmg->entries[index] == 0)
            {
                DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
                return false;
            }
            target = dmg->entries[index];
            MemClear(target, sizeof(DMG_Entry));
            target->type = DMGEntry_Empty;
        }
        else
        {
            return false;
        }
    }

    if (target->type != DMGEntry_Empty && !DMG_RemoveEntry(dmg, index))
        return false;

    if (!DMG_LoadEntryStoredData(dmg, source))
        return false;

    uint8_t* sourceData = source->storedData;
    uint32_t sourceDataSize = source->storedDataSize;
    MemCopy(target, source, sizeof(DMG_Entry));
    target->storedData = 0;
    target->storedDataSize = 0;
    target->ownsStoredData = false;
    if (dmg->version == DMG_Version5 && source->RGB32PaletteV5 != 0 && source->paletteColors != 0)
    {
        target->RGB32PaletteV5 = Allocate<uint32_t>("DAT5 palette", source->paletteColors);
        if (target->RGB32PaletteV5 == 0)
        {
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            return false;
        }
        MemCopy(target->RGB32PaletteV5, source->RGB32PaletteV5, source->paletteColors * sizeof(uint32_t));
    }
    if (sourceDataSize != 0 && !DMG_StoreEntryData(target, sourceData, sourceDataSize))
        return false;
    target->fileOffset = 0;
    target->flags |= DMG_FLAG_PROCESSED;
    return DMG_UpdateEntry(dmg, index);
}

static bool DMG_WriteClassicPayload(File* file, DMG* dmg, DMG_Entry* entry)
{
    uint8_t header[6];
    if (!DMG_LoadEntryStoredData(dmg, entry))
        return false;

    if (entry->type == DMGEntry_Image)
    {
        write16(header + 0x00, entry->width | ((entry->flags & DMG_FLAG_COMPRESSED) ? 0x8000 : 0), dmg->littleEndian);
        write16(header + 0x02, entry->height, dmg->littleEndian);
        write16(header + 0x04, (uint16_t)entry->storedDataSize, dmg->littleEndian);
    }
    else
    {
        write16(header + 0x00, 0, dmg->littleEndian);
        write16(header + 0x02, 0x8000, dmg->littleEndian);
        write16(header + 0x04, (uint16_t)entry->storedDataSize, dmg->littleEndian);
    }

    if (File_Write(file, header, sizeof(header)) != sizeof(header))
    {
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        return false;
    }
    if (entry->storedDataSize != 0 && File_Write(file, entry->storedData, entry->storedDataSize) != entry->storedDataSize)
    {
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        return false;
    }
    return true;
}

static bool DMG_WriteDAT5Payload(File* file, DMG* dmg, uint8_t index, DMG_Entry* entry)
{
    if (entry->type == DMGEntry_Audio)
    {
        if (!DMG_LoadEntryStoredData(dmg, entry))
            return false;
        if (entry->storedDataSize != 0 && File_Write(file, entry->storedData, entry->storedDataSize) != entry->storedDataSize)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
        return true;
    }

    if (!DMG_LoadEntryStoredData(dmg, entry))
        return false;

    uint32_t paletteBytes = entry->paletteColors * 3;
    entry->paletteSize = paletteBytes;
    if (paletteBytes != 0)
    {
        uint32_t* palette = DMG_GetEntryStoredPalette(dmg, index);
        if (palette == 0)
            return false;
        uint8_t* pal = DMG_GetTemporaryBuffer(ImageMode_RGBA32);
        if (pal == 0 || DMG_GetTemporaryBufferSize() < paletteBytes)
        {
            DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
            return false;
        }
        for (uint16_t i = 0; i < entry->paletteColors; i++)
        {
            pal[i * 3 + 0] = (uint8_t)(palette[i] >> 16);
            pal[i * 3 + 1] = (uint8_t)(palette[i] >> 8);
            pal[i * 3 + 2] = (uint8_t)palette[i];
        }
        if (File_Write(file, pal, paletteBytes) != paletteBytes)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
    }

    if (entry->storedDataSize != 0 && File_Write(file, entry->storedData, entry->storedDataSize) != entry->storedDataSize)
    {
        DMG_SetError(DMG_ERROR_WRITING_FILE);
        return false;
    }
    return true;
}

bool DMG_Save(DMG* dmg)
{
    if (dmg == 0 || dmg->file == 0)
        return false;
    if (!dmg->dirty)
        return true;

    if (dmg->version == DMG_Version5)
    {
        for (uint32_t i = dmg->firstEntry; i <= dmg->lastEntry; i++)
        {
            DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)i);
            if (entry == 0 || entry->type == DMGEntry_Empty)
                continue;
            if (!DMG_LoadEntryStoredData(dmg, entry))
                return false;
            if (entry->type == DMGEntry_Image && entry->paletteColors != 0 && DMG_GetEntryStoredPalette(dmg, (uint8_t)i) == 0)
                return false;
        }
    }
    else
    {
        for (int i = 0; i < 256; i++)
        {
            DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)i);
            if (entry == 0 || entry->type == DMGEntry_Empty)
                continue;
            if (!DMG_LoadEntryStoredData(dmg, entry))
                return false;
        }
    }

    if (!File_Seek(dmg->file, 0))
    {
        DMG_SetError(DMG_ERROR_SEEKING_FILE);
        return false;
    }
    File_Truncate(dmg->file, 0);

    if (dmg->version == DMG_Version5)
    {
        uint32_t entryCount = dmg->lastEntry >= dmg->firstEntry ? (uint32_t)(dmg->lastEntry - dmg->firstEntry + 1) : 0;
        uint32_t offset = 16 + entryCount * 32;
        for (uint32_t i = dmg->firstEntry; i <= dmg->lastEntry; i++)
        {
            DMG_Entry* entry = dmg->entries[i];
            if (entry == 0 || entry->type == DMGEntry_Empty)
                continue;
            if (entry->type == DMGEntry_Image)
            {
                if (!DMG_LoadEntryStoredData(dmg, entry))
                    return false;
                entry->paletteSize = entry->paletteColors * 3;
                entry->length = entry->paletteSize + entry->storedDataSize;
            }
            else
            {
                if (!DMG_LoadEntryStoredData(dmg, entry))
                    return false;
                entry->length = entry->storedDataSize;
            }
            entry->fileOffset = offset;
            offset += entry->length;
        }

        uint8_t header[16];
        MemClear(header, sizeof(header));
        header[0] = 'D';
        header[1] = 'A';
        header[2] = 'T';
        header[4] = 0;
        header[5] = 5;
        write16(header + 0x06, dmg->targetWidth, false);
        write16(header + 0x08, dmg->targetHeight, false);
        write16(header + 0x0A, dmg->firstEntry, false);
        write16(header + 0x0C, dmg->lastEntry, false);
        header[0x0E] = dmg->colorMode;
        if (File_Write(dmg->file, header, sizeof(header)) != sizeof(header))
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }

        for (uint32_t i = dmg->firstEntry; i <= dmg->lastEntry; i++)
        {
            uint8_t buffer[32];
            MemClear(buffer, sizeof(buffer));
            DMG_Entry* entry = dmg->entries[i];
            if (entry != 0 && entry->type != DMGEntry_Empty)
            {
                write32(buffer + 0x00, entry->fileOffset, false);
                write32(buffer + 0x04, entry->length, false);
                buffer[0x08] = entry->type == DMGEntry_Audio ? 1 : 0;
                if (entry->type == DMGEntry_Image)
                {
                    uint16_t flags = 0;
                    buffer[0x09] = entry->bitDepth;
                    if (entry->flags & DMG_FLAG_ZX0) flags |= 0x0008;
                    if ((entry->flags & DMG_FLAG_FIXED) == 0) flags |= 0x0010;
                    if (entry->flags & DMG_FLAG_BUFFERED) flags |= 0x0020;
                    if (DMG_GetCGAMode(entry) == CGA_Blue) flags |= 0x0040;
                    write16(buffer + 0x0A, flags, false);
                    write16(buffer + 0x0C, entry->x, false);
                    write16(buffer + 0x0E, entry->y, false);
                    write16(buffer + 0x10, entry->width, false);
                    write16(buffer + 0x12, entry->height, false);
                    buffer[0x14] = entry->firstColor;
                    buffer[0x15] = entry->lastColor;
                }
                else
                {
                    buffer[0x09] = (uint8_t)entry->x;
                }
            }
            if (File_Write(dmg->file, buffer, sizeof(buffer)) != sizeof(buffer))
            {
                DMG_SetError(DMG_ERROR_WRITING_FILE);
                return false;
            }
        }

        for (uint32_t i = dmg->firstEntry; i <= dmg->lastEntry; i++)
        {
            DMG_Entry* entry = dmg->entries[i];
            if (entry == 0 || entry->type == DMGEntry_Empty)
                continue;
            if (!DMG_WriteDAT5Payload(dmg->file, dmg, (uint8_t)i, entry))
                return false;
        }

        dmg->fileSize = offset;
        File_Truncate(dmg->file, dmg->fileSize);
        dmg->dirty = false;
        return true;
    }

    int entrySize = dmg->version == DMG_Version1 ? 44 :
        (dmg->version == DMG_Version1_CGA || dmg->version == DMG_Version1_EGA || dmg->version == DMG_Version1_PCW) ? 10 : 48;
    int headerSize = dmg->version == DMG_Version2 ? 10 : 6;
    uint32_t dataOffset = headerSize + entrySize * 256;

    for (int i = 0; i < 256; i++)
    {
        DMG_Entry* entry = dmg->entries[i];
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (!DMG_LoadEntryStoredData(dmg, entry))
            return false;
        entry->fileOffset = dataOffset;
        entry->length = entry->storedDataSize;
        dataOffset += 6 + entry->storedDataSize;
    }

    uint8_t header[10];
    MemClear(header, sizeof(header));
    if (dmg->version == DMG_Version1)
    {
        write16(header + 0x00, 0x0004, false);
        write16(header + 0x02, dmg->screenMode, false);
        write16(header + 0x04, DMG_GetEntryCount(dmg), false);
        if (File_Write(dmg->file, header, 6) != 6)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
    }
    else if (dmg->version == DMG_Version2)
    {
        write16(header + 0x00, dmg->littleEndian ? 0xFFFF : 0x0300, false);
        write16(header + 0x02, dmg->screenMode, false);
        write16(header + 0x04, DMG_GetEntryCount(dmg), dmg->littleEndian);
        write32(header + 0x06, dataOffset, dmg->littleEndian);
        if (File_Write(dmg->file, header, 10) != 10)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
    }
    else
    {
        write16(header + 0x00, 0x0000, false);
        write16(header + 0x02, dmg->version == DMG_Version1_PCW ? 0x0004 : dmg->screenMode, false);
        write16(header + 0x04, DMG_GetEntryCount(dmg), true);
        if (File_Write(dmg->file, header, 6) != 6)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
    }

    for (int i = 0; i < 256; i++)
    {
        uint8_t buffer[48];
        MemClear(buffer, sizeof(buffer));
        DMG_Entry* entry = dmg->entries[i];
        if (entry != 0 && entry->type != DMGEntry_Empty)
        {
            uint16_t flags = 0;
            if (dmg->version == DMG_Version1)
            {
                if (entry->flags & DMG_FLAG_FIXED) flags |= 0x01;
                if (entry->flags & DMG_FLAG_BUFFERED) flags |= 0x02;
            }
            else if (dmg->version == DMG_Version1_CGA || dmg->version == DMG_Version1_EGA || dmg->version == DMG_Version1_PCW)
            {
                if (entry->flags & DMG_FLAG_BUFFERED) flags |= 0x02;
                if ((entry->flags & DMG_FLAG_FIXED) == 0) flags |= 0x01;
                if (dmg->version == DMG_Version1_CGA && DMG_GetCGAMode(entry) == CGA_Red) flags |= 0x0800;
            }
            else
            {
                if (entry->flags & DMG_FLAG_FIXED) flags |= 0x04;
                if (entry->flags & DMG_FLAG_BUFFERED) flags |= 0x02;
                if (DMG_GetCGAMode(entry) == CGA_Red) flags |= 0x01;
                if (entry->type == DMGEntry_Audio) flags |= 0x10;
            }

            write32(buffer + 0x00, entry->fileOffset, dmg->littleEndian);
            write16(buffer + 0x04, flags, dmg->littleEndian);
            write16(buffer + 0x06, entry->x, dmg->littleEndian);
            write16(buffer + 0x08, entry->y, dmg->littleEndian);
            if (entrySize > 10)
            {
                buffer[0x0A] = entry->firstColor;
                buffer[0x0B] = entry->lastColor;
                WritePalette(buffer + 0x0C, entry);
            }
            if (entrySize == 48)
            {
                if ((entry->flags & DMG_FLAG_AMIPALHACK) != 0)
                    write32(buffer + 0x2C, 0xDAADDAAD, dmg->littleEndian);
                else
                {
                    uint32_t cgaPalette = 0;
                    for (int n = 0; n < 16; n++)
                        cgaPalette |= (entry->CGAPalette[n] & 0x03) << (n * 2);
                    write32(buffer + 0x2C, cgaPalette, dmg->littleEndian);
                }
            }
        }
        if (File_Write(dmg->file, buffer, entrySize) != (uint32_t)entrySize)
        {
            DMG_SetError(DMG_ERROR_WRITING_FILE);
            return false;
        }
    }

    for (int i = 0; i < 256; i++)
    {
        DMG_Entry* entry = dmg->entries[i];
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (!DMG_WriteClassicPayload(dmg->file, dmg, entry))
            return false;
    }

    dmg->fileSize = dataOffset;
    File_Truncate(dmg->file, dmg->fileSize);
    dmg->dirty = false;
    return true;
}

/* ───────────────────────────────────────────────────────────────────────── */
/*  Compression															     */
/* ───────────────────────────────────────────────────────────────────────── */

bool CompressImage (uint8_t* pixels, int pixelCount,
	uint8_t* buffer, size_t bufferSize, bool* compressed, uint16_t* size, bool debug)
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
        if (debug)
        {
            printf("Color %02X: uncompressed nibbles=%d, compressed nibbles=%d\n",
               n, uncompressedColorSize[n], compressedColorSize[n]);
        }
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
    if (debug)
    {
        printf("Final mask: %04X\n", mask);
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
