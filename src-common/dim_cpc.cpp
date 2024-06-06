#ifdef HAS_VIRTUALFILESYSTEM

#include <dim_cpc.h>
#include <dim.h>
#include <os_mem.h>

#include <string.h>
#include <stdlib.h>

#pragma pack(push,1)

#define CPC_INFO_SIGNATURE 		"MV - CPCEMU Disk-File\r\nDisk-Info\r\n"
#define CPC_EXTENDED_SIGNATURE	"EXTENDED CPC DSK File\r\nDisk-Info\r\n"
#define CPC_TRACKINFO_SIGNATURE	"Track-Info\r\n"

struct CPC_DiskInfoBlock
{
	char     signature[34];
	char     creator[14];

	uint8_t  tracks;
	uint8_t  sides;
	uint16_t trackSize;		// Little endian
	uint8_t  trackSizes[204];
};

struct CPC_SectorInfo
{
	uint8_t track;
	uint8_t side;
	uint8_t id;
	uint8_t size;
	uint8_t fdc1;
	uint8_t fdc2;

	uint16_t dataLength;			// Only present on extended disks
};

struct CPC_TrackInfoBlock
{
	char signature[12];				// Track-Info\x0D\x0A
	char __unused0__[4];

	uint8_t trackNumber;
	uint8_t sideNumber;
	uint8_t __unused1__[2];
	uint8_t sectorSize;
	uint8_t sectorCount;
	uint8_t gap3Length;
	uint8_t fillerByte;

	CPC_SectorInfo sectors[232];
};

struct CPC_DirEntry
{
	uint8_t user;
	char    fileName[8];
	char    extension[3];
	uint8_t extentLow;
	uint8_t lastRecordByteCount;
	uint8_t extentHigh;
	uint8_t recordCount;
	uint8_t allocation[16];
};

struct CPC_DiskSpec
{
	uint8_t    diskType;
	uint8_t    sideness;
	uint8_t    tracksPerSide;
	uint8_t    sectorsPerTrack;
	uint8_t    sectorSizeLog;
	uint8_t    reservedTracks;
	uint8_t    blockShift;
	uint8_t    directoryBlocks;
	uint8_t    gapReadWrite;
	uint8_t    gapFormat;
	uint8_t    reserved[5];
	uint8_t    checksum;
};

#pragma pack(pop)

enum CPC_DiskType
{
	CPC_FORMAT_SPECTRUM,		// Also PCW SS range
	CPC_FORMAT_CPC_DATA,
	CPC_FORMAT_CPC_SYSTEM,
	CPC_FORMAT_PCW_DS,
};

struct CPC_Disk
{
	File* 		 file;
	uint32_t	 size;
	bool         extended;
	bool         doubleSide;
	bool         doubleTracks;

	CPC_DiskType diskType;
	uint16_t     sectorSize;
	uint16_t     tracksPerSide;
	uint16_t     blockSize;

	uint8_t      sectorsPerTrack;
	uint8_t      reservedTracks;
	uint8_t      directoryBlocks;

	CPC_DiskInfoBlock header;

	CPC_DirEntry *directory;
	uint32_t      directoryEntries;
};

uint32_t CPC_GetTrackOffsetInFile (CPC_Disk* disk, uint32_t trackNumber)
{
	if (!disk->extended)
		return 256 + disk->header.trackSize * trackNumber;

	uint32_t offset = 256;
	for (unsigned n = 0; n < trackNumber; n++)
		offset += disk->header.trackSizes[n] << 8;
	return offset;
}

uint32_t CPC_ReadTrack (CPC_Disk* disk, uint32_t trackNumber, uint32_t baseOffset, uint8_t* buffer, uint32_t bufferSize, uint32_t skip = 0)
{
	uint8_t trackInfoBlock[256];
	CPC_TrackInfoBlock* info = (CPC_TrackInfoBlock*) &trackInfoBlock;

	int offsetInFile = CPC_GetTrackOffsetInFile(disk, trackNumber);
	File_Seek(disk->file, offsetInFile);
	if (File_Read(disk->file, trackInfoBlock, 256) != 256)
	{
		DIM_SetError(DIMError_ReadError);
		return 0;
	}

	uint32_t size = 0;
	if (!disk->extended)
		size = info->sectorSize * info->sectorCount;
	else
	{
		for (int n = 0; n < info->sectorCount; n++)
			size += info->sectors[n].dataLength;
	}

	if (size > bufferSize + skip)
		size = bufferSize + skip;

	uint64_t pos = File_GetPosition(disk->file);

	int totalRead = 0;
	int previous = -1;
	for (int n = 0; n < info->sectorCount; n++)
	{
		int offset = 0;
		int current = 999;
		uint32_t currentSize = 0;
		int currentOffset = 0;
		for (int i = 0; i < info->sectorCount; i++)
		{
			int sectorSize = disk->extended ? info->sectors[i].dataLength : info->sectorSize * 256;
			if (info->sectors[i].id < current && info->sectors[i].id > previous)
			{
				currentOffset = offset;
				current = info->sectors[i].id;
				currentSize = sectorSize;
			}
			offset += sectorSize;
		}
		if (current == 999)
			return 0;
		previous = current;

		if (baseOffset >= currentSize)
		{
			baseOffset -= currentSize;
			continue;
		}

		currentOffset += baseOffset;
		currentSize -= baseOffset;
		baseOffset = 0;

		if (skip < currentSize)
		{
			if (currentSize > bufferSize + skip)
				currentSize = bufferSize + skip;

			currentSize -= skip;
			File_Seek(disk->file, pos + currentOffset + skip);
			if (File_Read(disk->file, buffer, currentSize) != currentSize)
			{
				DIM_SetError(DIMError_ReadError);
				return 0;
			}
			skip = 0;
		}
		else
		{
			skip -= currentSize;
		}
		bufferSize -= currentSize;
		totalRead += currentSize;
		buffer += currentSize;
		if (bufferSize == 0)
			return totalRead;
	}

	return totalRead;
}

uint32_t CPC_ReadBlock (CPC_Disk* disk, uint32_t blockIndex, uint8_t* buffer, uint32_t bufferSize, uint32_t skip = 0)
{
	uint32_t size = disk->blockSize > bufferSize ? bufferSize : disk->blockSize;
	uint32_t offset = blockIndex * disk->blockSize;
	uint32_t sector = offset / disk->sectorSize;
	uint32_t track = sector / disk->sectorsPerTrack;
	uint32_t trackSize = disk->sectorsPerTrack * disk->sectorSize;
	uint32_t trackOffset = offset - track * disk->sectorSize * disk->sectorsPerTrack;
	uint32_t result = CPC_ReadTrack(disk, disk->reservedTracks + track, trackOffset, buffer, size, skip);

	if (trackSize - trackOffset < size && result < size)
	{
		uint32_t remaining = size - result;
		result += CPC_ReadTrack(disk, disk->reservedTracks + track + 1, 0, buffer + trackSize - trackOffset, remaining);
	}
	return result;
}

CPC_Disk* CPC_OpenDisk (const char* fileName)
{
	uint8_t buffer[256];

	File* file = File_Open(fileName);
	if (file == NULL)
	{
		DIM_SetError(DIMError_FileNotFound);
		return NULL;
	}
	if (File_Read(file, buffer, 256) != 256)
	{
		File_Close(file);
		DIM_SetError(DIMError_ReadError);
		return NULL;
	}
	if (MemComp(buffer, CPC_EXTENDED_SIGNATURE, 8) != 0 &&
        MemComp(buffer, CPC_INFO_SIGNATURE, 8) != 0)
	{
		File_Close(file);
		DIM_SetError(DIMError_InvalidFile);
		return NULL;

	}
	CPC_Disk* disk = Allocate<CPC_Disk>("CPC Disk");
	if (disk == NULL)
	{
		File_Close(file);
		DIM_SetError(DIMError_OutOfMemory);
		return NULL;
	}
	memset(disk, 0, sizeof(CPC_Disk));

	disk->size = (uint32_t)File_GetSize(file);
	disk->file = file;
	disk->extended = buffer[0] == 'E';

	MemCopy(&disk->header, buffer, 256);

	bool ok = true;

	File_Seek(file, 256);
	if (File_Read(file, buffer, 256) != 256)
	{
		File_Close(file);
		Free(disk);
		DIM_SetError(DIMError_ReadError);
		return NULL;
	}

	CPC_TrackInfoBlock* firstTrackInfo = (CPC_TrackInfoBlock*)buffer;
	if (MemComp(firstTrackInfo->signature, CPC_TRACKINFO_SIGNATURE, 12) != 0)
	{
		DIM_SetError(DIMError_InvalidDisk);
		File_Close(disk->file);
		Free(disk);
		return NULL;
	}

	if (firstTrackInfo->sectors[0].id == 0x41 ||
		firstTrackInfo->sectors[0].id == 0xC1)
	{
		// CPC System or Data disk
		bool system = firstTrackInfo->sectors[0].id == 0x41;
		disk->diskType = system ? CPC_FORMAT_CPC_SYSTEM : CPC_FORMAT_CPC_DATA;
		disk->tracksPerSide = 40;
		disk->sectorsPerTrack = 9;
		disk->sectorSize = 512;
		disk->reservedTracks = system ? 2 : 0;
		disk->blockSize = 1024;
		disk->directoryBlocks = 2;
	}
	else
	{
		if (File_Read(file, buffer, 256) != 256)
		{
			File_Close(file);
			Free(disk);
			DIM_SetError(DIMError_ReadError);
			return NULL;
		}

		uint8_t firstByte = buffer[0];
		bool matches = true;
		for (int n = 1; matches && n < 10 ; n++)
			matches = buffer[n] == firstByte;

		if (matches)
		{
			// Assume this is a standard disk
			disk->diskType = CPC_FORMAT_SPECTRUM;
			disk->tracksPerSide = 40;
			disk->sectorsPerTrack = 9;
			disk->sectorSize = 512;
			disk->reservedTracks = 1;
			disk->directoryBlocks = 2;
			disk->blockSize = 1024;
		}
		else
		{
			// The disk actually contains a specs array
			CPC_DiskSpec* spec = (CPC_DiskSpec*)buffer;
			disk->diskType = (CPC_DiskType)spec->diskType;
			disk->doubleSide = (spec->sideness & 0x03) != 0;
			disk->doubleTracks = (spec->sideness & 0x80) != 0;
			disk->reservedTracks = spec->reservedTracks;
			disk->directoryBlocks = spec->directoryBlocks;
			disk->blockSize = (0x80 << spec->blockShift);

			ok = spec->diskType < 4;
		}
	}

	if (ok)
	{
		disk->directoryEntries = disk->blockSize * disk->directoryBlocks / 32;
		disk->directory = Allocate<CPC_DirEntry>("CPC Disk directory", disk->directoryEntries);
		if (disk->directory != NULL)
		{
			CPC_ReadTrack(disk, disk->reservedTracks, 0,
			              (uint8_t*)disk->directory,
			              disk->directoryEntries * 32);
			return disk;
		}
		else
		{
			DIM_SetError(DIMError_OutOfMemory);
			File_Close(disk->file);
			Free(disk);
			return NULL;
		}
	}

	DIM_SetError(DIMError_InvalidDisk);
	File_Close(disk->file);
	Free(disk);
	return NULL;
}

bool CPC_FindFile (CPC_Disk* disk, CPC_FindResults* result, const char* name)
{
	return CPC_FindFirstFile(disk, result, name);
}

bool CPC_FindFirstFile (CPC_Disk* disk, CPC_FindResults* result, const char* pattern)
{
	DIM_CopyPatternTo8D3(pattern, result->matchName, result->matchExtension);
	result->offset = -1;
	result->entries = 0;
	return CPC_FindNextFile (disk, result);
}

bool CPC_CheckOSHeader (CPC_FindResults* result, uint8_t* header)
{
	if (MemComp(header, "PLUS3DOS", 8) == 0)
	{
		uint8_t checksum = 0;
		for (int n = 0; n < 127; n++)
			checksum += header[n];
		if (header[127] != checksum)
			return false;
		if (result == NULL)
			return true;

		result->fileSize = header[16] + (header[17] << 8);
		uint16_t v0 = header[18] + (header[19] << 8);
		uint16_t v1 = header[20] + (header[21] << 8);
		switch (header[15])
		{
			case 0:
				if (v0 == 0x8000)
					snprintf(result->description, 32, "BASIC");
				else
					snprintf(result->description, 32, "BASIC LINE %d", v1);
				break;
			case 1:
				snprintf(result->description, 32, "DATA %c", header[19] - 64);
				break;
			case 2:
				snprintf(result->description, 32, "DATA %c$", header[19] - 64);
				break;
			case 3:
				if (v0 == 0)
					snprintf(result->description, 32, "CODE");
				else
					snprintf(result->description, 32, "CODE %d", v0);
				break;
		}
		return true;
	}

	uint32_t checksum = 0;
	for (int n = 0; n < 66; n++)
		checksum += header[n];
	if (checksum == 0 || (checksum & 0xFFFF) != (uint16_t)(header[67] + (header[68] << 8)))
		return false;
	if (result == NULL)
		return true;

	result->fileSize = header[64] + (header[65] << 8) + (header[66] << 16);
	uint16_t v0 = header[21] + (header[22] << 8);
	uint16_t v1 = header[26] + (header[27] << 8);
	switch (header[18])
	{
		case 0:
			snprintf(result->description, 32, "BASIC");
			break;
		case 1:
			snprintf(result->description, 32, "BASIC(P)");
			break;
		case 2:
			if (v1)
				snprintf(result->description, 32, "BINARY %d EXEC %d", v0, v1);
			else
				snprintf(result->description, 32, "BINARY %d", v0);
			break;
		case 3:
			if (v1)
				snprintf(result->description, 32, "BINARY(P) %d EXEC %d", v0, v1);
			else
				snprintf(result->description, 32, "BINARY(P) %d", v0);
			break;
		case 4:
			snprintf(result->description, 32, "SCREEN");
			break;
		case 5:
			snprintf(result->description, 32, "SCREEN(P)");
			break;
		case 6:
			snprintf(result->description, 32, "ASCII");
			break;
		case 7:
			snprintf(result->description, 32, "ASCII [protected]");
			break;
	}
	return true;
}

bool CPC_FindNextFile (CPC_Disk* disk, CPC_FindResults* result)
{
	bool found = false;
	uint8_t extent;
	uint8_t header[128];
	uint8_t name[8];
	uint8_t extension[3];

	uint16_t offset = result->offset;

	for (;;)
	{
		offset++;
		if (offset >= disk->directoryEntries)
			break;

		CPC_DirEntry* entry = disk->directory + offset;

		if (entry->user >= 16 || entry->fileName[0] == 0)
			continue;

		if (!found)
		{
			if (entry->extentLow != 0)
				continue;
			if (!DIM_MatchWildcards(entry->fileName, 8, result->matchName, 8) ||
				!DIM_MatchWildcards(entry->extension, 3, result->matchExtension, 3))
			{
				continue;
			}

			char* out = result->fileName;
			char* in  = entry->fileName;
			for (int n = 0; n < 8 && *in != ' '; n++)
				*out++ = *in++;
			in = entry->extension;
			if ((*in & 0x7F) != ' ')
			{
				*out++ = '.';
				for (int n = 0; n < 3 && *in != ' '; n++)
					*out++ = *in++ & 0x7F;
			}
			*out = 0;
			result->fileSize = 0;

			MemCopy(name, entry->fileName, 8);
			MemCopy(extension, entry->extension, 3);

			result->description[0] = 0;
			if (entry->allocation[0] != 0 &&
				CPC_ReadBlock(disk, entry->allocation[0], header, 128) == 128)
			{
				CPC_CheckOSHeader(result, header);
			}

			result->offset = offset;
			result->entries = 1;
			found = true;
		}
		else
		{
			if (MemComp(entry->fileName, name, 8) != 0 ||
				MemComp(entry->extension, extension, 3) != 0)
				continue;
			if (entry->extentLow != extent)
				continue;
			result->entries++;
		}

		for (int n = 0; n < 16; n++)
		{
			if (entry->allocation[n] != 0)
				result->fileSize += 1024;
		}
		extent = entry->extentLow + 1;
	}

	return found;
}

CPC_DirEntry* CPC_NextFileEntry (CPC_Disk* disk, CPC_DirEntry* current)
{
	CPC_DirEntry* end = disk->directory + disk->directoryEntries;
	for (CPC_DirEntry* entry = current + 1; entry < end; entry++)
	{
		if (MemComp(entry->fileName, current->fileName, 8) == 0 &&
			MemComp(entry->extension, current->extension, 3) == 0 &&
			entry->extentLow == current->extentLow + 1)
			return entry;
	}
	return NULL;
}

uint32_t CPC_ReadFile (CPC_Disk* disk, const char* name, uint8_t* buffer, uint32_t bufferSize)
{
	CPC_FindResults results;
	if (!CPC_FindFile(disk, &results, name))
		return 0;

	uint32_t totalRead = 0;

	CPC_DirEntry* entry = disk->directory + results.offset;
	for (int n = 0; n < results.entries; n++)
	{
		for (int i = 0; i < 16; i++)
		{
			if (entry->allocation[i] != 0)
			{
				uint32_t read = CPC_ReadBlock(disk, entry->allocation[i], buffer, bufferSize);
				if (n == 0 && i == 0 && read > 128 && CPC_CheckOSHeader(NULL, buffer))
				{
					if (bufferSize > read)
					{
						memmove(buffer, buffer + 128, read - 128);
						read -= 128;
					}
					else
					{
						read = CPC_ReadBlock(disk, entry->allocation[i], buffer, bufferSize, 128);
					}
				}
				if (read == 0)
					return 0;

				// printf("Read %d bytes from block %d\n", read, entry->allocation[i]);

				buffer += read;
				totalRead += read;
				bufferSize -= read;
				if (bufferSize == 0)
					return totalRead;
			}
		}
		entry = CPC_NextFileEntry(disk, entry);
		if (entry == NULL)
		{
			if (n + 1 < results.entries)
				printf("File %s: truncated file, missing extent %d\n", name, n + 1);
			break;
		}
	}
	printf("Total read: %d\n", totalRead);
	return totalRead;
}

uint32_t CPC_GetBlockCount (CPC_Disk* disk)
{
	uint32_t tracks = disk->tracksPerSide;
	if (disk->doubleSide) tracks *= 2;
	tracks -= disk->reservedTracks;
	return tracks * disk->sectorsPerTrack * disk->sectorSize / disk->blockSize;
}

uint64_t CPC_GetFreeSpace (CPC_Disk* disk)
{
	CPC_DirEntry* entry = disk->directory;

	bool used[256] = { 0 };

	for (unsigned n = 0; n < disk->directoryEntries; n++, entry++)
	{
		if (entry->user < 16 && entry->fileName[0] != 0)
		{
			for (int i = 0; i < 16; i++)
				if (entry->allocation[i] != 0)
					used[entry->allocation[i]] = true;
		}
	}

	uint32_t blocks = CPC_GetBlockCount(disk);
	uint32_t directoryBlocks = disk->directoryEntries * 32 / disk->blockSize;

	unsigned freeBlocks = 0;
	for (unsigned n = 0; n < blocks; n++)
		freeBlocks += !used[n];
	freeBlocks -= directoryBlocks;
	return freeBlocks * disk->blockSize;
}

void CPC_DumpInfo (CPC_Disk* disk)
{
	uint32_t tracks = disk->tracksPerSide;
	if (disk->doubleSide) tracks *= 2;
	uint32_t totalSize = tracks * disk->sectorSize * disk->sectorsPerTrack;

	printf("Disk size:           %7dK\n", (totalSize + 1023)/1024);
	printf("Disk format:     %12s\n",
	       disk->diskType == CPC_FORMAT_SPECTRUM   ? "Spectrum+3" :
	       disk->diskType == CPC_FORMAT_CPC_DATA   ? "CPC Data" :
	       disk->diskType == CPC_FORMAT_CPC_SYSTEM ? "CPC System" : "PCW DD");
	printf("-----------------------------\n");
	printf("Tracks:              %8d%s\n",
	       disk->tracksPerSide * (disk->doubleSide ? 2 : 1),
	       disk->doubleSide ? " (Double sided)" : "");
	printf("Sectors per track:   %8d\n", disk->sectorsPerTrack);
	printf("Bytes per sector:    %8d\n", disk->sectorSize);
	printf("Bytes per block:     %8d\n", disk->blockSize);
	printf("Total blocks:        %8d\n", CPC_GetBlockCount(disk));
	printf("Directory blocks:    %8d\n", disk->directoryBlocks);
	printf("Free data blocks:    %8u\n", (uint32_t)(CPC_GetFreeSpace(disk) / disk->blockSize));
}

void CPC_CloseDisk (CPC_Disk* disk)
{
	File_Close(disk->file);
	Free(disk->directory);
	Free(disk);
}

#endif