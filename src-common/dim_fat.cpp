#ifdef HAS_VIRTUALFILESYSTEM

#include <dim_fat.h>
#include <os_mem.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define FAT_SECTOR_SIZE            512u
#define FAT_BOOT_SIGNATURE         0x29
#define FAT_DIRECTORY_ENTRY_SIZE   32
#define FAT_MAX_DIRECTORY_DEPTH    32
#define FAT_MAX_PATH               256

#pragma pack(push,1)

typedef struct _FAT_BootSector
{
	uint8_t    jmpBoot[3];
	uint8_t    OEMName[8];
	uint16_t   bytesPerSector;
	uint8_t    sectorsPerCluster;
	uint16_t   reservedSectorCount;
	uint8_t    numFATs;
	uint16_t   maxRootEntries;
	uint16_t   totalSectorsFAT16;
	uint8_t    mediaType;				// Ignore
	uint16_t   sectorsPerFAT;
	uint16_t   sectorsPerTrack;
	uint16_t   numHeads;
	uint32_t   hiddenSectors;			// Ignore
	uint32_t   totalSectorsFAT32;
	uint8_t    driveNumber;				// Ignore
	uint8_t    reserved;				// Ignore
	uint8_t    bootSignature;			// 0x29 to indicate next 3 fields are valid
	uint32_t   volumeID;
	uint8_t    volumeLabel[11];
	uint8_t    fileSystemType[8];		// Informative only, usually "FAT12   " or "FAT16   "
	uint8_t    codeReserved[448];
	uint16_t   signature;
} 
FAT_BootSector;

#pragma pack(pop)

typedef enum
{
	FAT_FAT12,
	FAT_FAT16,
	FAT_FAT32,
}
FAT_FATType;

#define FAT_UNUSED    0u
#define FAT_RESERVED  0xFFFFFFF0u
#define FAT_BAD       0xFFFFFFF7u
#define FAT_LAST      0xFFFFFFFFu

struct FAT_Disk
{
	File*       	file;
	uint32_t		size;
	uint32_t    	numSectors;
	uint32_t        numClusters;

	FAT_BootSector* bootSector;
	FAT_FATType		fatType;
	uint8_t*		fat;
	bool            fatModified;

	uint32_t    	rootFolderSector;
	uint32_t        firstDataSector;

	uint32_t        cwdFirstSector;
	FAT_DirEntry    cwd[FAT_MAX_DIRECTORY_DEPTH];
	int 			cwdDepth;
};

static FAT_Disk* currentDisk = NULL;
static uint32_t  currentSector = 0xFFFFFFFF;
static uint8_t   sectorData[FAT_SECTOR_SIZE];

uint32_t  FAT_ReadFileFromDE        (FAT_Disk* dist, FAT_DirEntry* entry, uint8_t* buffer, uint32_t size);
bool      FAT_RemoveFileFromDE      (FAT_Disk* disk, FAT_DirEntry* entry, uint32_t sector, uint32_t offset);
uint32_t  FAT_CountBadClusters      (FAT_Disk* disk);
uint32_t  FAT_CountFreeClusters     (FAT_Disk* disk);
uint32_t  FAT_FindFreeCluster       (FAT_Disk* disk);
bool      FAT_WriteSectorToDisk     (FAT_Disk* disk, uint32_t sector, const uint8_t* data);
bool      FAT_UpdateDirectoryEntry  (FAT_Disk* disk, FAT_DirEntry* entry, uint32_t sector, uint32_t offset);

static inline uint32_t FAT_Sector2Cluster (FAT_Disk* disk, uint32_t sector)
{
	return (sector - disk->firstDataSector) / disk->bootSector->sectorsPerCluster + 2;
}

static inline uint32_t FAT_Cluster2Sector (FAT_Disk* disk, uint32_t cluster)
{
	if (cluster < 2 || cluster >= FAT_RESERVED)
		return FAT_LAST;
	return disk->firstDataSector + (cluster - 2) * disk->bootSector->sectorsPerCluster;
}

static inline uint32_t FAT_FAT12Value(uint32_t cluster)
{
	return cluster >= 0xFF8 ? FAT_LAST :
		   cluster == 0xFF7 ? FAT_BAD :
		   cluster >= 0xFF0 ? FAT_RESERVED : cluster;
}

static inline uint32_t FAT_FAT16Value(uint32_t cluster)
{
	return cluster >= 0xFFF8 ? FAT_LAST :
		   cluster == 0xFFF7 ? FAT_BAD :
		   cluster >= 0xFFF0 ? FAT_RESERVED : cluster;
}

static inline uint32_t FAT_FAT32Value(uint32_t cluster)
{
	return cluster >= 0xFFFFFFF8 ? FAT_LAST :
		   cluster == 0xFFFFFFF7 ? FAT_BAD :
		   cluster >= 0xFFFFFFF0 ? FAT_RESERVED : cluster;
}

static uint32_t FAT_SetClusterFATValue(FAT_Disk* disk, uint32_t cluster, uint32_t value)
{
	int offset;
	
	switch (disk->fatType)
	{
		case FAT_FAT12:
			offset = (cluster >> 1) * 3;
			if (cluster & 1)
			{
				disk->fat[offset + 1] = (disk->fat[offset + 1] & 0x0F) | ((value & 0x0F) << 4);
				disk->fat[offset + 2] = (value >> 4) & 0xFF;
			}
			else
			{
				disk->fat[offset] = value & 0xFF;
				disk->fat[offset + 1] = (disk->fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
			}
			break;
		case FAT_FAT16:
			offset = cluster * 2;
			disk->fat[offset] = value & 0xFF;
			disk->fat[offset + 1] = (value >> 8) & 0xFF;
			break;
		case FAT_FAT32:
			offset = cluster * 4;
			disk->fat[offset] = value & 0xFF;
			disk->fat[offset + 1] = (value >> 8) & 0xFF;
			disk->fat[offset + 2] = (value >> 16) & 0xFF;
			disk->fat[offset + 3] = (value >> 24) & 0xFF;
			break;
	}

	disk->fatModified = true;
	return value;
}

static uint32_t FAT_GetClusterFATValue(FAT_Disk* disk, uint32_t cluster)
{
	int offset;
	
	switch (disk->fatType)
	{
		case FAT_FAT12:
			offset = (cluster >> 1) * 3;
			if (cluster & 1)
				return FAT_FAT12Value( (disk->fat[offset + 2] << 4) | (disk->fat[offset + 1] >> 4) );
			else
				return FAT_FAT12Value(disk->fat[offset] | ((disk->fat[offset + 1] << 8) & 0xF00) );
		case FAT_FAT16:
			offset = cluster * 2;
			return FAT_FAT16Value( disk->fat[offset] | (disk->fat[offset + 1] << 8) );
		case FAT_FAT32:
			offset = cluster * 4;
			return FAT_FAT32Value( disk->fat[offset] | (disk->fat[offset + 1] << 8) | (disk->fat[offset + 2] << 16) | (disk->fat[offset + 3] << 24) );
	}

	return 0;
}

static inline int FAT_NextSector (FAT_Disk* disk, uint32_t sector)
{
	if (sector < disk->firstDataSector)
	{
		if (sector == disk->firstDataSector - 1)
			return FAT_LAST;
		return sector + 1;
	}

	int sectorIndexInsideCluster = (sector - disk->firstDataSector) % disk->bootSector->sectorsPerCluster;
	if (sectorIndexInsideCluster < disk->bootSector->sectorsPerCluster - 1)
		return sector + 1;
	
	int cluster = FAT_Sector2Cluster(disk, sector);
	int nextCluster = FAT_GetClusterFATValue(disk, cluster);
	if (nextCluster < 2 || nextCluster >= FAT_RESERVED)
		return FAT_LAST;

	return FAT_Cluster2Sector(disk, (uint32_t)nextCluster);
}

bool FAT_MatchPattern (const FAT_DirEntry* entry, const char* pattern)
{
	size_t len = strlen(pattern);
	const char* dot = strrchr(pattern, '.');
	if (dot == NULL)
	{
		return DIM_MatchWildcards(entry->filename, 8, pattern, (int)len);
	}
	else
	{
		if (!DIM_MatchWildcards(entry->filename, 8, pattern, (int)(dot - pattern)))
			return false;
		return DIM_MatchWildcards(entry->extension, 3, dot + 1, (int)strlen(dot + 1));
	}
}

FAT_Disk* FAT_OpenDisk (const char* filename)
{
	bool ok = false;

	FAT_Disk* disk = Allocate<FAT_Disk>("FAT Disk");
	if (!disk)
	{
		DIM_SetError(DIMError_OutOfMemory);
		return NULL;
	}

	disk->file = File_Open(filename, ReadWrite);
	if (!disk->file)
	{
		DIM_SetError(DIMError_FileNotFound);
		Free(disk);
		return NULL;
	}

	DIM_SetError(DIMError_InvalidFile);

	disk->size = (uint32_t)File_GetSize(disk->file);
	disk->numSectors = disk->size / FAT_SECTOR_SIZE;
	disk->bootSector = (FAT_BootSector*)Allocate<uint8_t>("FAT Disk boot", FAT_SECTOR_SIZE);
	if (!disk->bootSector)
	{
		DIM_SetError(DIMError_OutOfMemory);
		File_Close(disk->file);
		Free(disk);
		return NULL;
	}

	FAT_BootSector* boot = disk->bootSector;
	if (File_Read(disk->file, disk->bootSector, FAT_SECTOR_SIZE) != FAT_SECTOR_SIZE)
	{
		DIM_SetError(DIMError_ReadError);
	}
	else
	{
		if (boot->bytesPerSector != FAT_SECTOR_SIZE ||
			boot->sectorsPerCluster == 0 ||
			boot->sectorsPerCluster > 64 ||
			boot->sectorsPerFAT == 0 ||
			boot->sectorsPerFAT * FAT_SECTOR_SIZE >= disk->size ||
			boot->numFATs < 1 ||
			boot->numFATs > 4)
		{
			DIM_SetError(DIMError_InvalidFile);
		}
		else if (
			boot->totalSectorsFAT16 > 0 &&
			boot->totalSectorsFAT16 < 4085 &&
			(unsigned)(boot->totalSectorsFAT16 * FAT_SECTOR_SIZE) <= disk->size &&
			(unsigned)boot->totalSectorsFAT16 * FAT_SECTOR_SIZE <= disk->size)
		{
			disk->numSectors = boot->totalSectorsFAT16;
			disk->fatType = FAT_FAT12;
			ok = true;
		}
		else if (
			boot->totalSectorsFAT16 > 0 &&
			boot->totalSectorsFAT16 < 65525 &&
			boot->totalSectorsFAT16 * FAT_SECTOR_SIZE <= disk->size &&
			boot->totalSectorsFAT16 * FAT_SECTOR_SIZE <= disk->size)
		{
			disk->numSectors = boot->totalSectorsFAT16;
			disk->fatType = FAT_FAT16;
			ok = true;
		}
		else if (
			boot->totalSectorsFAT32 > 0 &&
			boot->totalSectorsFAT32 * FAT_SECTOR_SIZE <= disk->size &&
			boot->totalSectorsFAT32 * FAT_SECTOR_SIZE <= disk->size)
		{
			disk->numSectors = boot->totalSectorsFAT32;
			disk->fatType = FAT_FAT32;
			ok = true;
		}
		else
		{
			DIM_SetError(DIMError_InvalidFile);
		}
	}

	if (ok)
	{
		uint64_t fatSize = boot->sectorsPerFAT * FAT_SECTOR_SIZE * boot->numFATs;
		disk->fat = Allocate<uint8_t>("FAT Disk fat", fatSize);
		if (disk->fat == NULL)
		{
			DIM_SetError(DIMError_OutOfMemory);
			ok = false;
		}
		else
		{
			disk->rootFolderSector = 1 + boot->sectorsPerFAT * boot->numFATs;
			disk->firstDataSector  = disk->rootFolderSector + boot->maxRootEntries * FAT_DIRECTORY_ENTRY_SIZE / FAT_SECTOR_SIZE;
			disk->numClusters      = (disk->numSectors - disk->firstDataSector) / boot->sectorsPerCluster;
			disk->cwdFirstSector   = disk->rootFolderSector;
			disk->cwdDepth         = 0;

			if (File_Read(disk->file, disk->fat, fatSize) != fatSize)
			{
				DIM_SetError(DIMError_ReadError);
				ok = false;
			}

			// TODO: Check copies for consistency
		}
	}

	if (!ok)
	{
		File_Close(disk->file);
		if (disk->fat)
			Free(disk->fat);
		Free(disk->bootSector);
		Free(disk);
		return NULL;
	}

	return disk;
}

FAT_Disk* FAT_CreateDisk (const char* filename, uint32_t size)
{
	FAT_Disk* disk = Allocate<FAT_Disk>("FAT Disk");
	if (!disk)
	{
		DIM_SetError(DIMError_OutOfMemory);
		return NULL;
	}
    if (size == 0)
        size = DISK_SIZE_1440KB;

	disk->file = File_Create(filename);
	if (!disk->file)
	{
		DIM_SetError(DIMError_CreatingFile);
		Free(disk);
		return NULL;
	}

	disk->size = size;
	disk->numSectors = size / FAT_SECTOR_SIZE;
	disk->bootSector = (FAT_BootSector*)Allocate<uint8_t>("FAT Disk boot", FAT_SECTOR_SIZE);
	if (!disk->bootSector)
	{
		DIM_SetError(DIMError_OutOfMemory);
		File_Close(disk->file);
		Free(disk);
		return NULL;
	}
	memset(disk->bootSector, 0, FAT_SECTOR_SIZE);

	// Calculate sectors per cluster in order to fit the disk in 12 bits
	uint8_t sectorsPerCluster = 1;
	while (disk->numSectors / sectorsPerCluster > 4085)
	{
		if (sectorsPerCluster >= 128)
		{
			DIM_SetError(DIMError_InvalidFile);
			File_Close(disk->file);
			Free(disk->bootSector);
			Free(disk);
			return NULL;
		}
		sectorsPerCluster *= 2;
	}

	// Calculate FAT size
	uint32_t totalClusters = disk->numSectors / sectorsPerCluster;
	uint32_t bytesPerFat = (totalClusters * 3 + 1) / 2;
	uint32_t sectorsPerFAT = (bytesPerFat + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;

	FAT_BootSector* boot = disk->bootSector;
	memcpy(boot->fileSystemType, "FAT12   ", 8);
	boot->bytesPerSector = FAT_SECTOR_SIZE;
	boot->sectorsPerCluster = sectorsPerCluster;
	boot->sectorsPerFAT = (uint16_t)sectorsPerFAT; 
	boot->totalSectorsFAT16 = (uint16_t)(disk->numSectors / 2);
	boot->numFATs = 2;
	boot->maxRootEntries = 112;

	disk->rootFolderSector = 1 + boot->sectorsPerFAT * boot->numFATs;
	disk->firstDataSector  = disk->rootFolderSector + boot->maxRootEntries * FAT_DIRECTORY_ENTRY_SIZE / FAT_SECTOR_SIZE;
	disk->numClusters	   = (disk->numSectors - disk->firstDataSector) / boot->sectorsPerCluster;
	disk->cwdFirstSector   = disk->rootFolderSector;
	disk->cwdDepth         = 0;
	
	disk->fat = Allocate<uint8_t>("FAT Disk fat", boot->sectorsPerFAT * FAT_SECTOR_SIZE * boot->numFATs);
	if (disk->fat == NULL)
	{
		DIM_SetError(DIMError_OutOfMemory);
		File_Close(disk->file);
		Free(disk->bootSector);
		Free(disk);
		return NULL;
	}

	uint32_t fatSize = boot->sectorsPerFAT * FAT_SECTOR_SIZE * boot->numFATs;
	bool ok = File_Write(disk->file, disk->bootSector, FAT_SECTOR_SIZE) == FAT_SECTOR_SIZE;
	ok = ok && File_Write(disk->file, disk->fat, fatSize) == fatSize;

	currentDisk = NULL;
	memset(sectorData, 0, FAT_SECTOR_SIZE);
	for (uint32_t n = disk->rootFolderSector; n < disk->numSectors && ok ; n++)
		ok = ok && File_Write(disk->file, sectorData, FAT_SECTOR_SIZE) == FAT_SECTOR_SIZE;

	if (!ok)
	{
		DIM_SetError(DIMError_CreatingFile);
		File_Close(disk->file);
		Free(disk->fat);
		Free(disk->bootSector);
		Free(disk);
		return NULL;
	}

	return disk;
}

uint32_t FAT_GetVolumeLabel (FAT_Disk* disk, char* buffer, uint32_t bufferSize)
{
	if (disk->bootSector->bootSignature != FAT_BOOT_SIGNATURE)
		return 0;

	char* out = buffer;
	char* end = buffer + bufferSize - 1;
	char* ptr = (char *)disk->bootSector->volumeLabel;
	while (out < end && *ptr && *ptr != ' ')
		*out++ = *ptr++;
	return (uint32_t)(out - buffer);
}

void FAT_DumpInfo (FAT_Disk* disk)
{
	FAT_BootSector* boot = disk->bootSector;

	printf("Disk size:           %7dK\n", (disk->size + 1023) / 1024);
	printf("Disk format:         %8s\n", disk->fatType == FAT_FAT12 ? "FAT12" : disk->fatType == FAT_FAT16 ? "FAT16" : "FAT32");
	if (disk->bootSector->bootSignature == FAT_BOOT_SIGNATURE &&
		boot->volumeLabel[0] != 0 && boot->volumeLabel[0] != ' ')
		printf("Volume label:     %.*s\n", 11, boot->volumeLabel);

	printf("-----------------------------\n");
	printf("Bytes per sector:    %8d\n", boot->bytesPerSector);
	printf("Bytes per cluster:   %8d\n", boot->sectorsPerCluster * boot->bytesPerSector);
	printf("FAT copies:          %8d\n", boot->numFATs);
	printf("Sectors per FAT:     %8d\n", boot->sectorsPerFAT);
	printf("Max root entries:    %8d\n", boot->maxRootEntries);
	printf("Total sectors:       %8d\n", disk->numSectors);
	printf("Total clusters:      %8d\n", disk->numClusters);
	printf("First data sector:   %8d\n", disk->firstDataSector);
	printf("Free clusters:       %8d\n", FAT_CountFreeClusters(disk));
	printf("Bad clusters:        %8d\n", FAT_CountBadClusters(disk));
}

bool FAT_ReadSectorToBuffer (FAT_Disk* disk, uint32_t sector, uint8_t* data)
{
	if (sector < 0 || sector >= disk->numSectors)
		return false;
	File_Seek(disk->file, sector * FAT_SECTOR_SIZE);
	if (File_Read(disk->file, data, FAT_SECTOR_SIZE) == FAT_SECTOR_SIZE)
		return true;
	DIM_SetError(DIMError_ReadError);
	return false;
}

bool FAT_ReadSector (FAT_Disk* disk, uint32_t sector)
{
	if (currentDisk == disk && currentSector == sector)
		return true;

	if (sector < 0 || sector >= disk->numSectors)
		return false;
	File_Seek(disk->file, sector * FAT_SECTOR_SIZE);
	if (File_Read(disk->file, sectorData, FAT_SECTOR_SIZE) == FAT_SECTOR_SIZE)
	{
		currentDisk = disk;
		currentSector = sector;
		return true;
	}

	DIM_SetError(DIMError_ReadError);
	return false;
}

bool FAT_WriteSectorToDisk (FAT_Disk* disk, uint32_t sector, const uint8_t* data)
{
	if (sector < 0 || sector >= disk->numSectors)
		return false;

	File_Seek(disk->file, sector * FAT_SECTOR_SIZE);
	if (File_Write(disk->file, data, FAT_SECTOR_SIZE) == FAT_SECTOR_SIZE)
		return true;

	DIM_SetError(DIMError_WriteError);
	return false;
}

bool FAT_UpdateDirectoryEntry (FAT_Disk* disk, FAT_DirEntry* entry, uint32_t sector, uint32_t offset)
{
	if (sector < 0 || sector >= disk->numSectors)
		return false;
	if (!FAT_ReadSector(disk, sector))
		return false;

	memcpy(sectorData + offset, entry, FAT_DIRECTORY_ENTRY_SIZE);
	return FAT_WriteSectorToDisk(disk, sector, sectorData);
}

bool FAT_RemoveFileFromDE (FAT_Disk* disk, FAT_DirEntry* entry, uint32_t sector, uint32_t offset)
{
	if (sector < 0 || sector >= disk->numSectors)
		return false;

	uint32_t cluster = entry->startingCluster;
	while (cluster != FAT_LAST)
	{
		uint32_t nextCluster = FAT_GetClusterFATValue(disk, cluster);
		FAT_SetClusterFATValue(disk, cluster, 0);
		cluster = nextCluster;
	}

	entry->filename[0] = (int8_t)(uint8_t)0xE5;
	return FAT_UpdateDirectoryEntry(disk, entry, sector, offset);
}

bool FAT_RemoveFile (FAT_Disk* disk, const char* path)
{
	FAT_FindResults result;
	if (!FAT_FindFile(disk, &result, path))
		return false;

	FAT_DirEntry* entry = &result.entry;
	uint32_t sector = result.sector;
	uint32_t offset = result.offset;

	if ((entry->attributes & FAT_DIRECTORY) ||
		(uint8_t)entry->filename[0] == 0xE5 ||
		(uint8_t)entry->filename[0] == 0)
	{
		DIM_SetError(DIMError_InvalidFile);
		return false;
	}

	return FAT_RemoveFileFromDE(disk, entry, sector, offset);
}

bool FAT_RemoveDirectory (FAT_Disk* disk, const char* path)
{
	FAT_FindResults result;

	if (!FAT_ChangeDirectory(disk, path))
		return false;
	if (FAT_FindFirstFile(disk, &result, "*"))
	{
		DIM_SetError(DIMError_DirectoryNotEmpty);
		return false;
	}
	FAT_ChangeDirectory(disk, "..");

	if (!FAT_FindFile(disk, &result, path))
		return false;
	FAT_DirEntry* entry = &result.entry;
	uint32_t sector = result.sector;
	uint32_t offset = result.offset;
	FAT_RemoveFileFromDE(disk, entry, sector, offset);
	return true;
}

uint32_t FAT_FindFreeCluster (FAT_Disk* disk)
{
	for (uint32_t i = 2; i < disk->numClusters; i++)
	{
		if (FAT_GetClusterFATValue(disk, i) == 0)
			return i;
	}
	return FAT_LAST;
}

uint32_t FAT_CountFreeClusters(FAT_Disk* disk)
{
	uint32_t FreeClusters = 0;
	for (uint32_t i = 2; i < disk->numClusters; i++)
	{
		if (FAT_GetClusterFATValue(disk, i) == 0)
			FreeClusters++;
	}
	return FreeClusters;
}

uint32_t FAT_CountBadClusters(FAT_Disk* disk)
{
	uint32_t FreeClusters = 0;
	for (uint32_t i = 2; i < disk->numClusters; i++)
	{
		if (FAT_GetClusterFATValue(disk, i) == FAT_BAD)
			FreeClusters++;
	}
	return FreeClusters;
}

uint64_t FAT_GetFreeSpace(FAT_Disk* disk)
{
	return FAT_CountFreeClusters(disk) * disk->bootSector->sectorsPerCluster * FAT_SECTOR_SIZE;
}

bool FAT_FindFile (FAT_Disk* disk, FAT_FindResults* result, const char* pattern)
{
	const char* ptr = pattern;
	while (*ptr != 0)
	{
		if (*ptr == '?')
			return false;
		if (*ptr == '*')
			return false;
		ptr++;
	}
	return FAT_FindFirstFile(disk, result, pattern);
}

bool FAT_FindFirstFile (FAT_Disk* disk, FAT_FindResults* result, const char* pattern)
{
	result->disk = disk;
	result->entry.filename[0] = 0;
	result->sector = disk->cwdFirstSector;
	result->offset = (uint16_t) -FAT_DIRECTORY_ENTRY_SIZE;

	DIM_CopyPatternTo8D3(pattern, result->matchFilename, result->matchExtension);

	return FAT_FindNextFile(disk, result);
}

bool FAT_FindNextFile (FAT_Disk* disk, FAT_FindResults* result)
{
	if ((int16_t)result->offset > FAT_SECTOR_SIZE - FAT_DIRECTORY_ENTRY_SIZE)
	{
		if (!FAT_ReadSector(disk, result->sector))
		{
			DIM_SetError(DIMError_ReadError);
			return false;
		}
	}

	for (;;)
	{
		result->offset += FAT_DIRECTORY_ENTRY_SIZE;
		if (result->offset >= FAT_SECTOR_SIZE)
		{
			result->sector = FAT_NextSector(disk, result->sector);
			result->offset = 0;
			if (result->sector == FAT_LAST)
			{
				DIM_SetError(DIMError_FileNotFound);
				return false;
			}
		}
		if (currentSector != result->sector)
		{
			if (!FAT_ReadSector(disk, result->sector))
			{
				DIM_SetError(DIMError_ReadError);
				return false;
			}
		}

		FAT_DirEntry* entry = (FAT_DirEntry*) (sectorData + result->offset);
		if (entry->filename[0] == 0)
			continue;
		if ((uint8_t)entry->filename[0] == 0xE5)
			continue;
		if (entry->attributes & FAT_LONGNAME)
			continue;
		if (entry->filename[0] == '.')
			continue;
		if (entry->filename[0] == ' ')
			continue;
		if (!DIM_MatchWildcards(entry->filename, 8, result->matchFilename, 8))
			continue;
		if (!DIM_MatchWildcards(entry->extension, 3, result->matchExtension, 3))
			continue;

		result->entry = *entry;
		return true;
	}
}

uint32_t FAT_ReadFileFromDE (FAT_Disk* disk, FAT_DirEntry* entry, uint8_t* buffer, uint32_t size)
{
	uint32_t total = 0;
	uint32_t remaining = entry->fileSize;
	if (remaining > size)
		remaining = size;
	uint32_t cluster = entry->startingCluster;
	if (cluster >= FAT_RESERVED || cluster < 2)
	{
		DIM_SetError(DIMError_ReadError);
		return 0;
	}
	while (cluster != FAT_LAST)
	{
		int sector = FAT_Cluster2Sector(disk, cluster);
		for (int n = 0; n < disk->bootSector->sectorsPerCluster; n++, sector++)
		{
			if (remaining > FAT_SECTOR_SIZE)
			{
				if (!FAT_ReadSectorToBuffer(disk, sector, buffer))
					return total;
				buffer += FAT_SECTOR_SIZE;
				remaining -= FAT_SECTOR_SIZE;
				total += FAT_SECTOR_SIZE;
			}
			else
			{
				if (!FAT_ReadSector(disk, sector))
					return total;
				memcpy(buffer, sectorData, remaining);
				total += remaining;
				return total;
			}
		}
		cluster = FAT_GetClusterFATValue(disk, cluster);
		if (cluster >= FAT_RESERVED || cluster < 2)
		{
			DIM_SetError(DIMError_ReadError);
			return remaining;
		}
	}
	return total;
}

uint32_t FAT_ReadFile (FAT_Disk* disk, const char* path, uint8_t* buffer, uint32_t size)
{
	FAT_FindResults result;
	if (!FAT_FindFile(disk, &result, path))
		return 0;

	return FAT_ReadFileFromDE(disk, &result.entry, buffer, size);
}

void FAT_CloseDisk (FAT_Disk* disk)
{
	if (disk)
	{
		if (disk->fatModified)
		{
			File_Seek(disk->file, FAT_SECTOR_SIZE);
			for (int n = 0; n < disk->bootSector->numFATs; n++)
				File_Write(disk->file, disk->fat, disk->bootSector->sectorsPerFAT * FAT_SECTOR_SIZE);
		}
		File_Close(disk->file);

		Free(disk->bootSector);
		Free(disk->fat);
		Free(disk);
	}
}

uint32_t FAT_GetFileName (FAT_DirEntry* entry, char* buffer, uint32_t bufferSize)
{
	char* ptr = buffer;
	char* end = buffer + bufferSize - 1;
	for (int i = 0; i < 8 && entry->filename[i] != ' ' && ptr < end; i++)
		*ptr++ = entry->filename[i];
	if (entry->extension[0] != ' ' && ptr < end)
	{
		*ptr++ = '.';
		for (int i = 0; i < 3 && entry->extension[i] != ' ' && ptr < end; i++)
			*ptr++ = entry->extension[i];
	}
	*ptr = 0;
	return (uint32_t)(ptr - buffer);
}

bool FAT_ChangeDirectory (FAT_Disk* disk, const char* name)
{
	if (name[0] == '\\')
	{
		disk->cwdDepth = 0;
		disk->cwdFirstSector = disk->rootFolderSector;
		name++;
	}

	const char* separator = strchr(name, '\\');
	if (separator)
	{
		char buffer[13];
		size_t len = separator - name;
		if (len > 12) len = 12;
		memcpy(buffer, name, len);
		buffer[len] = 0;
		if (!FAT_ChangeDirectory(disk, buffer))
			return false;
		return FAT_ChangeDirectory(disk, separator + 1);
	}

	if (name[0] == 0 || strcmp(name, ".") == 0)
		return true;

	if (strcmp(name, "..") == 0)
	{
		if (disk->cwdDepth > 0)
		{
			disk->cwdDepth--;
			if (disk->cwdDepth == 0)
				disk->cwdFirstSector = disk->rootFolderSector;
			else
				disk->cwdFirstSector = FAT_Cluster2Sector(disk, disk->cwd[disk->cwdDepth-1].startingCluster);
			return true;
		}
		return false;
	}

	FAT_FindResults results;
	if (!FAT_FindFile(disk, &results, name))
	{
		DIM_SetError(DIMError_DirectoryNotFound);
		return false;
	}

	if (results.entry.attributes & FAT_DIRECTORY)
	{
		if (disk->cwdDepth < FAT_MAX_DIRECTORY_DEPTH)
		{
			disk->cwd[disk->cwdDepth] = results.entry;
			disk->cwdDepth++;
			disk->cwdFirstSector = FAT_Cluster2Sector(disk, results.entry.startingCluster);
			return true;
		}
	}
	DIM_SetError(DIMError_NotADirectory);
	return false;
}

uint32_t FAT_GetCWD (FAT_Disk* disk, char* buffer, uint32_t bufferSize)
{
	char* ptr = buffer;
	char* end = buffer + bufferSize - 1;

	if (disk->cwdDepth == 0 && ptr < end)
		*ptr++ = '\\';
	for (int i = 0; i < disk->cwdDepth && ptr < end; i++)
	{
		if (ptr < end)
			*ptr++ = '\\';
		ptr += FAT_GetFileName(&disk->cwd[i], ptr, (uint32_t)(end - ptr));
	}
	*ptr = 0;
	return (uint32_t)(ptr - buffer);
}

bool FAT_FindFreeDirectoryEntry(FAT_Disk* disk, FAT_FindResults* result, bool deletedOk, bool makeDirectoryBigger)
{
	result->disk = disk;
	result->entry.filename[0] = 0;
	result->sector = disk->cwdFirstSector;
	result->offset = 0;

	if (result->sector == FAT_LAST)
	{
		DIM_SetError(DIMError_InvalidFile);
		return false;
	}

	if (!FAT_ReadSector(disk, result->sector))
	{
		DIM_SetError(DIMError_ReadError);
		return false;
	}

	for (;;)
	{
		FAT_DirEntry* entry = (FAT_DirEntry*)(sectorData + result->offset);
		if (entry->filename[0] == 0 || (deletedOk && (uint8_t)entry->filename[0] == 0xE5))
		{
			result->entry = *entry;
			return true;
		}

		result->offset += FAT_DIRECTORY_ENTRY_SIZE;
		if (result->offset >= FAT_SECTOR_SIZE)
		{
			uint32_t nextSector = FAT_NextSector(disk, result->sector);
			if (nextSector == FAT_LAST)
				break;

			result->sector = nextSector;
			result->offset = 0;
			if (!FAT_ReadSector(disk, result->sector))
			{
				DIM_SetError(DIMError_ReadError);
				return false;
			}
		}
	}

	if (!makeDirectoryBigger)
	{
		DIM_SetError(DIMError_DirectoryFull);
		return false;
	}

	if (disk->cwdDepth == 0 && disk->fatType != FAT_FAT32)
	{
		DIM_SetError(DIMError_DirectoryFull);
		return false;
	}

	uint32_t nextCluster = FAT_FindFreeCluster(disk);
	if (nextCluster == FAT_LAST)
	{
		DIM_SetError(DIMError_DiskFull);
		return false;
	}

	uint32_t currentCluster = FAT_Sector2Cluster(disk, result->sector);
	if (currentCluster == FAT_LAST)
	{
		DIM_SetError(DIMError_InvalidFile);
		return false;
	}

	FAT_SetClusterFATValue(disk, currentCluster, nextCluster);
	FAT_SetClusterFATValue(disk, nextCluster, (unsigned)FAT_LAST);

	result->sector = FAT_Cluster2Sector(disk, nextCluster);
	result->offset = 0;

	memset(sectorData, 0, FAT_SECTOR_SIZE);
	currentDisk = disk;
	currentSector = result->sector;
	return FAT_WriteSectorToDisk(disk, result->sector, sectorData);
}

bool FAT_MakeDirectory (FAT_Disk* disk, const char* filename)
{
	FAT_FindResults results;
	if (FAT_FindFile(disk, &results, filename))
	{
		DIM_SetError(DIMError_FileExists);
		return false;
	}

	if (!FAT_FindFreeDirectoryEntry(disk, &results, false, true))
		return false;

	memset(&results.entry, 0, sizeof(FAT_DirEntry));
	results.entry.attributes = FAT_DIRECTORY;

	const char* ptr = filename;
	for (int n = 0; n < 8; n++)
	{
		if (*ptr == 0 || *ptr == '.')
			results.entry.filename[n] = ' ';
		else
			results.entry.filename[n] = (char)toupper(*ptr++);
	}
	if (*ptr == '.')
		ptr++;
	for (int n = 0; n < 3; n++)
	{
		if (*ptr == 0)
			results.entry.extension[n] = ' ';
		else
			results.entry.extension[n] = (char)toupper(*ptr++);
	}

	return FAT_UpdateDirectoryEntry(disk, &results.entry, results.sector, results.offset);
}

uint32_t FAT_WriteFile (FAT_Disk* disk, const char* filename, const uint8_t* buffer, uint32_t size)
{
	FAT_FindResults results;
	bool existingEntry = false;
	uint8_t preservedAttributes = 0;
	if (FAT_FindFile(disk, &results, filename))
	{
		existingEntry = true;
		preservedAttributes = results.entry.attributes;
		if (!FAT_RemoveFileFromDE(disk, &results.entry, results.sector, results.offset))
			return 0;
	}
	else if (!FAT_FindFreeDirectoryEntry(disk, &results, false, false) &&
			 !FAT_FindFreeDirectoryEntry(disk, &results, true, true))
		return 0;

	uint32_t FreeClusters = FAT_CountFreeClusters(disk);
	uint32_t clusterSize = disk->bootSector->sectorsPerCluster * FAT_SECTOR_SIZE;
	uint32_t clustersNeeded = (size + clusterSize - 1) / clusterSize;
	if (clustersNeeded > FreeClusters)
	{
		DIM_SetError(DIMError_OutOfMemory);
		return 0;
	}

	uint32_t previousCluster = FAT_LAST;
	results.entry.fileSize = 0;
	results.entry.fat32ClusterHigh = (uint16_t)(FAT_LAST >> 16);
	results.entry.startingCluster = 0;
	results.entry.attributes = existingEntry ? preservedAttributes : 0;
	results.entry.modifyTime = 0;
	results.entry.modifyDate = 0;
	
	const char* ptr = filename;
	for (int n = 0; n < 8; n++)
	{
		if (*ptr == 0 || *ptr == '.')
			results.entry.filename[n] = ' ';
		else
			results.entry.filename[n] = (char)toupper(*ptr++);
	}
	if (*ptr == '.')
		ptr++;
	for (int n = 0; n < 3; n++)
	{
		if (*ptr == 0)
			results.entry.extension[n] = ' ';
		else
			results.entry.extension[n] = (char)toupper(*ptr++);
	}

	while (size > 0)
	{
		uint32_t cluster = FAT_FindFreeCluster(disk);
		if (cluster == FAT_LAST)
		{
			DIM_SetError(DIMError_DiskFull);
			return 0;
		}
		if (previousCluster != FAT_LAST)
			FAT_SetClusterFATValue(disk, previousCluster, cluster);
		else
		{
			results.entry.fat32ClusterHigh = (uint16_t)(cluster >> 16);
			results.entry.startingCluster = (uint16_t)cluster;
		}

		FAT_SetClusterFATValue(disk, cluster, FAT_LAST);
		previousCluster = cluster;

		uint32_t sector = FAT_Cluster2Sector(disk, cluster);
		for (int n = 0; n < disk->bootSector->sectorsPerCluster; n++, sector++)
		{
			if (size > FAT_SECTOR_SIZE)
			{
				if (!FAT_WriteSectorToDisk(disk, sector, buffer))
				{
					DIM_SetError(DIMError_WriteError);
					FAT_UpdateDirectoryEntry(disk, &results.entry, results.sector, results.offset);
					return results.entry.fileSize;
				}
				buffer += FAT_SECTOR_SIZE;
				size -= FAT_SECTOR_SIZE;
				results.entry.fileSize += FAT_SECTOR_SIZE;
			}
			else
			{
				memcpy(sectorData, buffer, size);
				memset(sectorData + size, 0, FAT_SECTOR_SIZE - size);
				currentDisk = disk;
				currentSector = sector;
				if (!FAT_WriteSectorToDisk(disk, sector, sectorData))
				{
					DIM_SetError(DIMError_WriteError);
					FAT_UpdateDirectoryEntry(disk, &results.entry, results.sector, results.offset);
					return results.entry.fileSize;
				}
				results.entry.fileSize += size;
				size = 0;
				break;
			}
		}
	}

	FAT_UpdateDirectoryEntry(disk, &results.entry, results.sector, results.offset);
	return results.entry.fileSize;
}

#endif