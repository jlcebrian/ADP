#include <dmg.h>
#include <os_mem.h>
#include <os_lib.h>

#ifndef DEBUG_IMAGE_CACHE
#define DEBUG_IMAGE_CACHE 0
#endif

static uint32_t useCounter = 0;

void DMG_SetupFileCache (DMG* dmg, uint32_t freeMemory, void(*progressFunction)(uint16_t))
{
	uint32_t blockSize = freeMemory;

#if DEBUG_IMAGE_CACHE
	DebugPrintf("Setting up file cache (%ld bytes per alloc)\n", (long)blockSize);
#endif

	for (int n = 0; n < DMG_CACHE_BLOCKS; n++)
	{
		if (dmg->fileCacheBlocks[n])
		{
			Free(dmg->fileCacheBlocks[n]);
			dmg->fileCacheBlocks[n] = 0;
		}
	}
	
	dmg->fileCacheOffset = 0;
	dmg->fileCacheBlockSize = 0;
    uint32_t fileCacheEnd = 0;
    bool haveBufferedRange = false;

	for (int n = 0; n < 256; n++)
	{
		if (dmg->entries[n] == 0 || 
			dmg->entries[n]->type == DMGEntry_Empty)
			continue;
        DMG_Entry* entry = dmg->entries[n];
        if ((entry->flags & DMG_FLAG_BUFFERED) == 0)
            continue;

        uint32_t entryStart = entry->fileOffset;
        uint32_t entryEnd = entry->fileOffset + entry->length;
        if (dmg->version != DMG_Version5)
            entryEnd += 6;

		if (!haveBufferedRange || dmg->fileCacheOffset > entryStart)
			dmg->fileCacheOffset = entryStart;
        if (!haveBufferedRange || fileCacheEnd < entryEnd)
            fileCacheEnd = entryEnd;
        haveBufferedRange = true;

	}

    if (!haveBufferedRange || fileCacheEnd <= dmg->fileCacheOffset)
    {
        if (progressFunction)
            progressFunction(255);
        return;
    }

	uint32_t fileSize = File_GetSize(dmg->file);
    if (fileCacheEnd > fileSize)
        fileCacheEnd = fileSize;
	uint32_t remaining = fileCacheEnd - dmg->fileCacheOffset;
	uint32_t cacheableSize = remaining;
	if (blockSize > remaining || blockSize == 0)
		blockSize = remaining;

#if DEBUG_IMAGE_CACHE
	DebugPrintf("File cache source range: offset=%lu size=%lu bytes\n",
		(unsigned long)dmg->fileCacheOffset, (unsigned long)remaining);
#endif

	if (progressFunction)
		progressFunction(64);

	File_Seek(dmg->file, dmg->fileCacheOffset);

	int n;
	for (n = 0; n < DMG_CACHE_BLOCKS; n++)
	{
		uint8_t* ptr = Allocate<uint8_t>("DAT Cache", blockSize, false);
		if (ptr == 0)
		{
			if (n == 0)
			{
				if (blockSize > 8192)
				{
					blockSize = 8192;
					n--;
					continue;
				}
				DebugPrintf("File cache allocation failed (no cache!)\n");
				return;
			}
			break;
		}

		uint32_t amount = blockSize > remaining ? remaining : blockSize;
		uint32_t blockOffset = dmg->fileCacheOffset + (cacheableSize - remaining);
#if DEBUG_IMAGE_CACHE
		DebugPrintf("File cache block %d: reading %lu bytes from file offset %lu\n",
			n, (unsigned long)amount, (unsigned long)blockOffset);
#endif

		dmg->fileCacheBlocks[n] = ptr;

		while (amount > 0)
		{
			uint32_t part = amount < 32768 ? amount : 32768;
			uint32_t read = File_Read(dmg->file, ptr, part);

			if (read < part) 	// Read error
			{
				Free(dmg->fileCacheBlocks[n]);
				dmg->fileCacheBlocks[n] = 0;

				if (progressFunction)
					progressFunction(256);

				DebugPrintf("File cache allocation partial (read error!)\n");
				return;
			}
			remaining -= read;
			amount -= read;
			ptr += read;
			
			if (progressFunction)
			{
				uint32_t cached = cacheableSize - remaining;
				uint32_t progress = 64 + (cacheableSize == 0 ? 192 : cached * 192 / cacheableSize);
				if (progress > 255)
					progress = 255;
				progressFunction(progress);
			}
		}
			
		if (remaining == 0)
			break;
	}
	dmg->fileCacheBlockSize = blockSize;

#if DEBUG_IMAGE_CACHE
	DebugPrintf("File cache allocated: %ld bytes x %ld block(s)\n", (long)blockSize, (long)(n+1));
#endif
}

void* DMG_GetFromFileCache(DMG* dmg, uint32_t offset, uint32_t size)
{
	if (dmg->fileCacheBlockSize == 0)
		return 0;
	if (offset < dmg->fileCacheOffset)
		return 0;
	offset -= dmg->fileCacheOffset;

	int block = offset / dmg->fileCacheBlockSize;
	uint32_t skip  = offset % dmg->fileCacheBlockSize;
	uint32_t remaining = dmg->fileCacheBlockSize - skip;
	if (block >= DMG_CACHE_BLOCKS || dmg->fileCacheBlocks[block] == 0)
		return 0;
	if (remaining < size)
		return 0;

	return dmg->fileCacheBlocks[block] + skip;
}

void DMG_FreeImageCache(DMG* dmg)
{
	if (dmg->cache != 0)
	{
		Free(dmg->cache);
		dmg->cache = 0;
	}
}

bool DMG_SetupImageCache (DMG* dmg, uint32_t bytes)
{
	if (dmg->cache != 0)
	{
		Free(dmg->cache);
		dmg->cache = 0;
	}

	// Minimum size for a non-compressed packed screen
	if (bytes < 32768)
		return false;

	while (true)
	{
		dmg->cache = (DMG_Cache*)Allocate<uint8_t>("DMG Image Cache", bytes, false);
		if (dmg->cache != 0)
			break;
		if (bytes < 65536)
		{
			DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
			dmg->cacheSize = 0;
			DebugPrintf("Image cache allocation failed\n");
			return false;
		}
		bytes >>= 1;
	}
	dmg->cacheSize = bytes;
	dmg->cacheFree = bytes;
	dmg->cacheTail = dmg->cache;
	MemClear(dmg->cacheBitmap, sizeof(dmg->cacheBitmap));
#if DEBUG_IMAGE_CACHE
	DebugPrintf("Image cache allocated (%ld bytes)\n", (long)bytes);
#endif
	return true;
}

bool DMG_RemoveOlderCacheItem (DMG* dmg, bool force)
{
	if (dmg->cache == 0)
		return false;

	uint32_t olderTime = 0;
	DMG_Cache* older = 0;
	DMG_Cache* item = dmg->cache;
	while (item != dmg->cacheTail)
	{
		if ((force || !item->buffer) && 
			(older == 0 || olderTime > item->time))
		{
			olderTime = item->time;
			older = item;
		}
		item = item->next;
	}
	if (older == 0)
	{
#if DEBUG_IMAGE_CACHE
		DebugPrintf("Image cache evict: no removable item (force=%d free=%lu)\n",
			force ? 1 : 0, (unsigned long)dmg->cacheFree);
#endif
		return false;
	}

	uint32_t space = older->size + sizeof(DMG_Cache);
	uint8_t removedIndex = older->index;
#if DEBUG_IMAGE_CACHE
	DebugPrintf("Image cache evict: index=%u size=%lu buffered=%d free=%lu gain=%lu force=%d\n",
		(unsigned)removedIndex,
		(unsigned long)older->size,
		older->buffer ? 1 : 0,
		(unsigned long)dmg->cacheFree,
		(unsigned long)space,
		force ? 1 : 0);
#endif
	uint32_t offsetNext = (uint8_t*)older->next - (uint8_t*)dmg->cache;
	uint32_t spaceAfter = dmg->cacheSize - dmg->cacheFree - offsetNext;
	if (spaceAfter > 0)
		MemMove(older, older->next, spaceAfter);

	dmg->cacheTail = (DMG_Cache*)((uint8_t*)dmg->cacheTail - space);
	dmg->cacheFree += space;

	dmg->cacheBitmap[removedIndex >> 3] &= ~(1 << (removedIndex & 7));

	item = older;
	while (item != dmg->cacheTail)
	{
		item->next = (DMG_Cache*)((uint8_t*)item->next - space);
		item = item->next;		            
	}
	return true;
}

DMG_Cache* DMG_GetImageCache (DMG* dmg, uint8_t index, DMG_Entry* entry, uint32_t size)
{
	if (dmg->cache == 0)
		return 0;

	if (dmg->cacheBitmap[index >> 3] & (1 << (index & 7)))
	{
		DMG_Cache* item = dmg->cache;
		while (item != dmg->cacheTail)
		{
			if (item->index == index)
			{
				if (item->size < size)
				{
					DebugPrintf("PANIC: Stored image cache is smaller than required size!!!\n");
					return 0;
				}
				item->time = useCounter++;
#if DEBUG_IMAGE_CACHE
				DebugPrintf("Image cache lookup: index=%u hit size=%lu required=%lu free=%lu\n",
					(unsigned)index,
					(unsigned long)item->size,
					(unsigned long)size,
					(unsigned long)dmg->cacheFree);
#endif
				return item;
			}
			item = item->next;
		}
		DebugPrintf("PANIC: Image cache bitmap mismatch\n");
	}

	dmg->cacheBitmap[index >> 3] |= (1 << (index & 7));

	// We're adding 32 bytes of extra pdding to help decompression
	// inside the same buffer as read

	uint32_t required = ((size + 31) & ~31) + sizeof(DMG_Cache);
#if DEBUG_IMAGE_CACHE
	DebugPrintf("Image cache lookup: index=%u miss required=%lu payload=%lu free=%lu total=%lu\n",
		(unsigned)index,
		(unsigned long)required,
		(unsigned long)size,
		(unsigned long)dmg->cacheFree,
		(unsigned long)dmg->cacheSize);
#endif
	if (required > dmg->cacheSize)
	{
		DebugPrintf("Image cache miss: required block too large for cache\n");
		return 0;
	}
	if (dmg->cacheFree < required)
	{
		bool force = dmg->cacheSize < 96*1024;
#if DEBUG_IMAGE_CACHE
		DebugPrintf("Image cache compact: need=%lu free=%lu force=%d\n",
			(unsigned long)required, (unsigned long)dmg->cacheFree, force ? 1 : 0);
#endif
		while (DMG_RemoveOlderCacheItem(dmg, force)) 
		{
			if (dmg->cacheFree >= required)
				break;
		}
		if (dmg->cacheFree < required && !force && (entry->flags & DMG_FLAG_BUFFERED))
		{
			while (DMG_RemoveOlderCacheItem(dmg, true))
			{
				if (dmg->cacheFree >= required)
					break;
			}
		}
		if (dmg->cacheFree < required)
		{
			DebugPrintf("Image cache miss: insufficient space after compaction (need=%lu free=%lu)\n",
				(unsigned long)required, (unsigned long)dmg->cacheFree);
			return 0;
		}
	}

	DMG_Cache* item = dmg->cacheTail;
	dmg->cacheTail = (DMG_Cache*)((uint8_t*)dmg->cacheTail + required);
	item->next = dmg->cacheTail;
	item->size = size;
	item->time = useCounter++;
	item->index = index;
	item->buffer = (entry->flags & DMG_FLAG_BUFFERED) != 0;
	item->populated = false;

	dmg->cacheFree -= required;
#if DEBUG_IMAGE_CACHE
	DebugPrintf("Image cache insert: index=%u required=%lu remaining=%lu buffered=%d\n",
		(unsigned)index,
		(unsigned long)required,
		(unsigned long)dmg->cacheFree,
		item->buffer ? 1 : 0);
#endif
	return item;
}
