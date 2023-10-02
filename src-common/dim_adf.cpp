#ifdef HAS_VIRTUALFILESYSTEM

#include <dim_adf.h>
#include <dim.h>
#include <os_file.h>
#include <os_bito.h>
#include <os_mem.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

static void fix32(ADF_RootBlock* root)
{
	uint8_t* ptr = (uint8_t*) root;
	uint8_t* nameStart = &root->nameLength;
	uint8_t* nameEnd = nameStart + 31;
	uint8_t* end = ptr + 512;

	while (ptr < end)
	{
		if (!(ptr >= nameStart && ptr < nameEnd))
			*(uint32_t*)ptr = read32(ptr, false);
		ptr += 4;
	}
}

static void fix32(ADF_HeaderBlock* header)
{
	uint8_t* ptr = (uint8_t*) header;
	uint8_t* commentStart = &header->commentLength;
	uint8_t* commentEnd = commentStart + 23;
	uint8_t* nameStart = &header->nameLength;
	uint8_t* nameEnd = nameStart + 31;
	uint8_t* end = ptr + 512;

	while (ptr < end)
	{
		if (!(ptr >= nameStart && ptr < nameEnd) &&
			!(ptr >= commentStart && ptr < commentEnd))
			*(uint32_t*)ptr = read32(ptr, false);
		ptr += 4;
	}
}

bool ADF_LoadBitmapFromDisk (ADF_Disk* disk)
{
	if (disk->root.bitmapFlag != -1)
		return false;

	uint32_t data[128];
	uint32_t size = (disk->numBlocks - 2 + 31) / 32;
	uint32_t current = 0;

	if (disk->bitmap == 0)
		disk->bitmap = Allocate<uint32_t>("ADF Disk", size);

	for (int n = 0; n < 25 && current < size && disk->root.bitmapPage[n]; n++)
	{
		uint32_t blockIndex = disk->root.bitmapPage[n];
		if (!File_Seek(disk->file, blockIndex*512) ||
			 File_Read(disk->file, &data, 512) != 512)
			return false;
		
		uint32_t checksum = 0;
		data[0] = fix32(data[0]);
		for (int i = 1; i < 128; i++)
		{
			data[i] = fix32(data[i]);
			checksum -= data[i];
		}
		if (checksum != data[0])
			return false;

		for (int i = 1; i < 128 && current < size; i++)
			disk->bitmap[current++] = data[i];
	}
	return true;
}

uint64_t ADF_GetFreeSpace (ADF_Disk* disk)
{
	if (disk->bitmap == 0 && !ADF_LoadBitmapFromDisk(disk))
		return 0;

	uint64_t count = 0;
	uint32_t mask = 0x00000001;
	uint32_t offset = 0;
	for (unsigned i = 0; i < disk->numBlocks; i++)
	{
		if (disk->bitmap[offset] & mask)
			count++;
		mask <<= 1;
		if (mask == 0)
		{
			mask = 0x00000001;
			offset++;
		}
	}
	return count * 512;
}

ADF_Disk* ADF_OpenDisk (const char* filename)
{
	ADF_Disk* disk = Allocate<ADF_Disk>("ADF Disk");
	if (disk == 0)
	{
		DIM_SetError(DIMError_OutOfMemory);
		return 0;
	}

	File* file = File_Open(filename);
	if (file == 0)
	{
		DIM_SetError(DIMError_FileNotFound);
		Free(disk);
		return 0;
	}
	if (File_Read(file, disk->boot, 1024) != 1024)
	{
		DIM_SetError(DIMError_ReadError);
		File_Close(file);
		Free(disk);
		return 0;
	}

	disk->file = file;
	disk->size = (uint32_t)File_GetSize(file);
	disk->numBlocks = disk->size / 512;
	disk->rootBlock = read32(&disk->boot[8], false);
	
	bool ok = false;

	if (memcmp(disk->boot, "DOS", 3) == 0 && disk->boot[3] < 2 &&
		disk->rootBlock < disk->numBlocks)
	{
		disk->fs = disk->boot[3] == 0 ? ADF_OFS : ADF_FFS;
		File_Seek(file, 512*disk->rootBlock);
		if (File_Read(file, &disk->root, 512) != 512)
		{
			DIM_SetError(DIMError_ReadError);
			ADF_CloseDisk(disk);
			return 0;
		}

		fix32(&disk->root);

		memcpy(disk->cwd[0].hashTable, disk->root.hashTable, 72*4);
		disk->cwd[0].hashTableEntries = disk->root.hashTableEntries;
		strcpy(disk->cwd[0].name, "");
		disk->cwd[0].block = disk->rootBlock;
		disk->cwdIndex = 0;

		disk->hashTableSize = disk->root.hashTableEntries;

		ok = disk->hashTableSize <= 72;
	}

	if (!ok)
	{
		DIM_SetError(DIMError_InvalidDisk);
		ADF_CloseDisk(disk);
		return 0;
	}
	return disk;
}

void ADF_CloseDisk (ADF_Disk* disk)
{
	if (disk->bitmap)
		Free(disk->bitmap);
	File_Close(disk->file);
	Free(disk);
}

bool ADF_FindFile (ADF_Disk* disk, ADF_FindResults* result, const char* name)
{
	return ADF_FindFirstFile(disk, result, name);
}

bool ADF_FindFirstFile (ADF_Disk* disk, ADF_FindResults* result, const char* pattern)
{
	result->nextBlock = ADF_INVALID_BLOCK;
	result->hashIndex = -1;
	result->block     = ADF_INVALID_BLOCK;
	
	if (pattern == NULL)
		pattern = "*";
	strncpy(result->pattern, pattern, 30);
	result->pattern[30] = 0;
	result->patternLen = (uint8_t)strlen(result->pattern);
	
	return ADF_FindNextFile(disk, result);
}

bool ADF_FindNextFile (ADF_Disk* disk, ADF_FindResults* result)
{
	while(true)
	{
		if (result->nextBlock == ADF_INVALID_BLOCK)
		{
			do
			{
				result->hashIndex++;
				if (result->hashIndex >= (int)disk->cwd[disk->cwdIndex].hashTableEntries)
				{
					DIM_SetError(DIMError_FileNotFound);
					return false;
				}
				result->nextBlock = disk->cwd[disk->cwdIndex].hashTable[result->hashIndex];
			}
			while (result->nextBlock == 0);
		}
		
		uint32_t blockIndex = result->nextBlock;
		if (blockIndex == 0)
			continue;
		if (blockIndex >= disk->numBlocks)
		{
			DIM_SetError(DIMError_InvalidDisk);
			return false;
		}

		File_Seek(disk->file, 512*blockIndex);
		if (File_Read(disk->file, &disk->block, 512) != 512)
		{
			DIM_SetError(DIMError_ReadError);
			return false;
		}
		fix32(&disk->block);

		if (disk->block.type != 2 || disk->block.headerKey != blockIndex)
		{
			DIM_SetError(DIMError_InvalidDisk);
			return false;
		}

		uint8_t len = disk->block.nameLength;
		if (len > 30) len = 30;
		strncpy(result->fileName, disk->block.name, len);
		result->fileName[len] = 0;
		result->fileSize = disk->block.fileSize;
		result->block = blockIndex;
		result->nextBlock = disk->block.hashChain;
		result->directory = (disk->block.secondaryType == 2);

		if (!DIM_MatchWildcards(result->fileName, len, result->pattern, result->patternLen))
			continue;

		return true;
	}
}

uint32_t ADF_ReadFile (ADF_Disk* disk, const char* path, uint8_t* buffer, uint32_t size)
{
	ADF_FindResults r;
	if (!ADF_FindFile(disk, &r, path))
		return false;

	uint8_t  data[512];
	uint8_t  headerSize = disk->fs == ADF_OFS ? 24 : 0;
	uint16_t dataSize = 512 - headerSize;
	uint32_t block = disk->block.firstData;
	uint32_t index = 71;
	uint32_t total = 0;
	if (disk->block.dataBlocks[index] != block)
	{
		DIM_SetError(DIMError_ReadError);
		return total;
	}
	while (total < size)
	{
		if (!File_Seek(disk->file, block*512))
			return total;
		if (File_Read(disk->file, &data, 512) != 512)
			return total;

		uint32_t remaining = size-total;
		uint32_t length = remaining > dataSize ? dataSize : remaining;
		memcpy(buffer, data+headerSize, length);
		total += length;
		buffer += length;
		if (total >= size)
			break;

		if (index-- == 0)
		{
			index = 71;
			if (disk->block.extension == 0)
			{
				DIM_SetError(DIMError_ReadError);
				return total;
			}
			if (!File_Seek(disk->file, disk->block.extension*512))
				return total;
			if (File_Read(disk->file, &disk->block, 512) != 512)
				return total;
			fix32(&disk->block);
		}
		block = disk->block.dataBlocks[index];
	}
	return total;
}

uint32_t ADF_GetVolumeLabel(ADF_Disk* disk, char* buffer, uint32_t bufferSize)
{
	if (bufferSize == 0)
		return 0;
	if (disk->root.nameLength == 0 ||
		disk->root.name[0] == ' ' ||
		disk->root.name[0] == 0)
		return 0;

	uint32_t len = disk->root.nameLength;
	if (len > 30) len = 30;
	if (len > bufferSize-1) len = bufferSize-1;
	memcpy(buffer, disk->root.name, len);
	buffer[len] = 0;
	return len;
}

void ADF_DumpInfo (ADF_Disk* disk)
{
	time_t     tc, ta;
	struct tm  ts;
	char       creation[80];
	char       access[80];

	// Get current time

	// Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
	tc = disk->root.cDays * 3600 * 24;
	ts = *localtime(&tc);
	strftime(creation, sizeof(creation), "%Y-%m-%d", &ts);
	ta = disk->root.aDays * 3600 * 24;
	ts = *localtime(&ta);
	strftime(access, sizeof(access), "%Y-%m-%d", &ts);

	printf("%.*s\n\n", disk->root.nameLength, disk->root.name);
	printf("Disk size:           %7dK\n", (disk->size + 1023) / 1024);
	printf("Creation:    %16s\n", creation);
	printf("Access:      %16s\n", access);
	printf("-----------------------------\n");
	printf("Total blocks:        %8d\n", disk->numBlocks);
	printf("Free blocks:         %8d\n", (uint32_t)(ADF_GetFreeSpace(disk) / 512));
	printf("Root block index:    %8d\n", disk->rootBlock);
}

uint32_t ADF_GetCWD (ADF_Disk* disk, char* buffer, uint32_t bufferSize)
{
	if (bufferSize == 0)
		return 0;

	char* end = buffer + bufferSize - 1;
	char* out = buffer;
	*out++ = '\\';

	for (int n = 1; n <= disk->cwdIndex && out < end; n++)
	{
		const char* in = disk->cwd[n].name;
		while (out < end && *in)
			*out++ = *in++;
		if (out < end && n < disk->cwdIndex-1)
			*out++ = '\\';
	}
	*out = 0;
	return (uint32_t)(out - buffer);
}

bool ADF_ChangeDirectory (ADF_Disk* disk, const char* name)
{
	ADF_FindResults r;
	if (!ADF_FindFile(disk, &r, name))
		return false;
	if (!r.directory)
	{
		DIM_SetError(DIMError_NotADirectory);
		return false;
	}
	if (disk->cwdIndex == ADF_MAX_DEPTH-1)
	{
		DIM_SetError(DIMError_CommandNotSupported);
		return false;
	}

	int index = ++disk->cwdIndex;
	memcpy(disk->cwd[index].name, r.fileName, 32);
	disk->cwd[index].block = r.block;
	disk->cwd[index].hashTableEntries = 72;
	memcpy(disk->cwd[index].hashTable, disk->block.dataBlocks, 72*4);
	return true;
}

#endif