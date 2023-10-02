#include <os_lib.h>
#include <os_mem.h>
#include <os_file.h>

#ifdef HAS_VIRTUALFILESYSTEM

#include <dim.h>

#ifdef _UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ----- Native files

uint64_t Native_GetPosition(File *file)
{
	return ftell((FILE*)file->data);
}

bool Native_Truncate(File *file, uint64_t size)
{
#ifdef _UNIX
	return ftruncate(fileno((FILE*)file->data), size) == 0;
#elif defined(_WINDOWS)
	return _chsize(fileno((FILE*)file->data), size) == 0;
#else
	return false;
#endif
}

void Native_Close(File *file)
{
	fclose((FILE*)file->data);
}

uint64_t Native_GetSize(File *file)
{
	FILE* f = (FILE*)file->data;
	long pos = ftell(f);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, pos, SEEK_SET);
	return (uint64_t)size;
}

uint64_t Native_Read(File *file, void *buffer, uint64_t bytes)
{
	return fread(buffer, 1, bytes, (FILE*)file->data);
}

uint64_t Native_Write(File *file, const void *buffer, uint64_t bytes)
{
	return fwrite(buffer, 1, bytes, (FILE*)file->data);
}

bool Native_Seek(File *file, uint64_t position)
{
	return fseek((FILE*)file->data, position, SEEK_SET) == 0;
}

File *Native_Open(const char *name, FileOpenMode mode)
{
	void* nativeFile = (void*) fopen(name, mode == ReadOnly ? "rb" : "rb+");
	if (nativeFile == 0)
		return 0;

	File* file = Allocate<File>("File", 1);
	if (file == 0)
		return 0;
	file->close       = Native_Close;
	file->seek        = Native_Seek;
	file->getPosition = Native_GetPosition;
	file->truncate    = Native_Truncate;
	file->getSize     = Native_GetSize;
	file->read        = Native_Read;
	file->write       = Native_Write;
	file->data        = nativeFile;
	return file;
}

File *Native_Create(const char *name)
{
	FILE* nativeFile = fopen(name, "wb");
	if (nativeFile == 0)
		return 0;

	File* file = Allocate<File>("File", 1);
	if (file == 0)
		return 0;
	file->close       = Native_Close;
	file->seek        = Native_Seek;
	file->getPosition = Native_GetPosition;
	file->truncate    = Native_Truncate;
	file->getSize     = Native_GetSize;
	file->read        = Native_Read;
	file->write       = Native_Write;
	file->data        = nativeFile;
	return file;
}

// ----- Memory files

uint64_t Memory_GetPosition(File *file)
{
	return (uint64_t)file->pos;
}

bool Memory_Truncate(File *file, uint64_t size)
{
	if (size > file->getSize(file))
		return false;
	file->size = size;
	return true;
}

void Memory_Close(File *file)
{
	Free(file->data);
	Free(file);
}

uint64_t Memory_GetSize(File *file)
{
	return (uint64_t)file->size;
}

uint64_t Memory_Read(File *file, void *buffer, uint64_t bytes)
{
	if (file->pos + bytes > file->size)
		bytes = file->size - file->pos;
	memcpy(buffer, (uint8_t*)file->data + file->pos, bytes);
	file->pos += bytes;
	return bytes;
}

uint64_t Memory_Write(File *file, const void *buffer, uint64_t bytes)
{
	if (file->pos + bytes > file->size)
		bytes = file->size - file->pos;
	MemCopy((uint8_t*)file->data + file->pos, buffer, bytes);
	file->pos += bytes;
	return bytes;
}

bool Memory_Seek(File *file, uint64_t position)
{
	if (position > file->size)
		return false;
	file->pos = position;
	return true;
}

File *Memory_Open(void *data, uint64_t dataSize)
{
	File* file = Allocate<File>("File", 1);
	if (file == 0)
		return 0;

	file->close       = Memory_Close;
	file->seek        = Memory_Seek;
	file->getPosition = Memory_GetPosition;
	file->truncate    = Memory_Truncate;
	file->getSize     = Memory_GetSize;
	file->read        = Memory_Read;
	file->write       = Memory_Write;
	file->data        = data;
	file->size        = dataSize;
	file->pos         = 0;
	return file;
}

// ----- Interface

static DIM_Disk* mountedDisk = 0;

bool File_MountDisk (const char* file)
{
	if (mountedDisk)
		return false;
	mountedDisk = DIM_OpenDisk(file);
	return mountedDisk != 0;
}

void File_UnmountDisk ()
{
	if (mountedDisk)
	{
		DIM_CloseDisk(mountedDisk);
		mountedDisk = 0;
	}
}

File *File_Open(const char *fileName, FileOpenMode mode)
{
	if (mountedDisk)
	{
		FindFileResults result;
		if (DIM_FindFile(mountedDisk, &result, fileName))
		{
			uint8_t* data = Allocate<uint8_t>("File", result.fileSize);
			if (data == 0)
				return 0;

			data[result.fileSize] = 0;
			uint32_t size = DIM_ReadFile(mountedDisk, result.fileName, data, result.fileSize);
			if (size == 0)
			{
				Free(data);
				return 0;
			}
			File* file = Memory_Open(data, size);
			if (file == 0)
				Free(data);
			return file;
		}
	}
	return Native_Open(fileName, mode);
}

File* File_Create(const char* fileName)
{
	// TODO: Support additional providers
	return Native_Create(fileName);
}

bool File_FindFirst (const char* pattern, FindFileResults* results)  
{ 
	if (mountedDisk)
		return DIM_FindFirstFile(mountedDisk, results, pattern);
	else
		return OS_FindFirstFile(pattern, results); 
}

bool File_FindNext (FindFileResults* results)                       
{ 
	if (mountedDisk)
		return DIM_FindNextFile(mountedDisk, results);
	else
		return OS_FindNextFile(results); 
}

#elif _STDCLIB

#ifdef _UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

File *File_Open(const char *file, FileOpenMode mode)
{
	return (File*) fopen(file, mode == ReadOnly ? "rb" : "rb+");
}

File *File_Create(const char *file)
{
	FILE* f = fopen(file, "wb");
	return (File*) f;
}

uint64_t File_GetPosition(File *file)
{
	return ftell((FILE*)file);
}

bool File_Truncate(File *file, uint64_t size)
{
#ifdef _UNIX
	return ftruncate(fileno((FILE*)file), size) == 0;
#elif defined(_WINDOWS)
	return _chsize(fileno((FILE*)file), size) == 0;
#else
	return false;
#endif
}

void File_Close(File *file)
{
	fclose((FILE*)file);
}

uint64_t File_GetSize(File *file)
{
	FILE* f = (FILE*)file;
	long pos = ftell(f);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, pos, SEEK_SET);
	return (uint64_t)size;
}

uint64_t File_Read(File *file, void *buffer, uint64_t bytes)
{
	return fread(buffer, 1, bytes, (FILE*)file);
}

uint64_t File_Write(File *file, const void *buffer, uint64_t bytes)
{
	return fwrite(buffer, 1, bytes, (FILE*)file);
}

bool File_Seek(File *file, uint64_t position)
{
	return fseek((FILE*)file, position, SEEK_SET) == 0;
}

#endif
