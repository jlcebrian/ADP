#ifndef __DIM_CPC_H__
#define __DIM_CPC_H__

#include <dim.h>
#include <os_file.h>
#include <os_types.h>

#include <stdio.h>

struct CPC_Disk;

struct CPC_FindResults
{
	uint16_t offset;
	uint16_t entries;
	uint16_t fileSize;
	char     fileName[13];
	char     description[32];

	char     matchName[8];
	char     matchExtension[3];
};

extern CPC_Disk* CPC_OpenDisk      (const char* fileName);
extern bool      CPC_FindFile      (CPC_Disk* disk, CPC_FindResults* results, const char* name);
extern bool      CPC_FindFirstFile (CPC_Disk* disk, CPC_FindResults* results, const char* pattern);
extern bool      CPC_FindNextFile  (CPC_Disk* disk, CPC_FindResults* results);
extern uint32_t  CPC_ReadFile      (CPC_Disk* disk, const char* name, uint8_t* buffer, uint32_t bufferSize);
extern uint64_t  CPC_GetFreeSpace  (CPC_Disk* disk);
extern void      CPC_DumpInfo      (CPC_Disk* disk);
extern void      CPC_CloseDisk     (CPC_Disk* disk);

#endif