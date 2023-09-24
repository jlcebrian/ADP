#pragma once

#include <os_types.h>

#define FILE_MAX_PATH 256

struct File;

enum FileOpenMode
{
	ReadOnly	 = 0x01,
	ReadWrite 	 = 0x02,
};

enum FileError
{
	FileError_None,
	FileError_OutOfMemory,
	FileError_FileNotFound,
	FileError_NotWritable,
	FileError_NotReadable,
	FileError_NotSupported,
	FileError_ReadError,
	FileError_WriteError,
	FileError_OutOfBounds,
};

enum FileAttributes
{
	FileAttribute_Directory = 0x01,
	FileAttribute_Hidden    = 0x02,
	FileAttribute_System    = 0x04,
	FileAttribute_Archive   = 0x08,
	FileAttribute_ReadOnly  = 0x10,
};

struct FindFileResults
{
	char     fileName[FILE_MAX_PATH];
	char     description[80];
	uint32_t fileSize;
	int      attributes;

	uint8_t  internalData[300];
};

extern File*       File_Open           (const char* file, FileOpenMode mode = ReadOnly);
extern File*       File_Create         (const char* file);
extern uint64_t    File_GetSize        (File* file);
extern uint64_t    File_Read           (File* file, void* buffer, uint64_t bytes);
extern uint64_t    File_Write          (File* file, const void* buffer, uint64_t bytes);
extern bool        File_Seek           (File* file, uint64_t position);
extern uint64_t    File_GetPosition    (File* file);
extern bool        File_Truncate       (File* file, uint64_t size);
extern void        File_Close          (File* file);

extern bool        File_FindFirst 	   (const char* pattern, FindFileResults* results);
extern bool        File_FindNext 	   (FindFileResults* results);

extern FileError   File_GetError       ();
extern const char* File_GetErrorString ();
