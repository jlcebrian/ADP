#include <os_file.h>
#include <os_lib.h>
#include <os_char.h>
#include <os_mem.h>

#ifdef _AMIGA

#include "video.h"
#include "timer.h"

#include <proto/dos.h>

#ifndef DEBUG_FILE_IO
#define DEBUG_FILE_IO 0
#endif

struct File
{
	BPTR handle;
};


struct FindFileInternal
{
	const char*   pattern;
	BPTR          lock;
	FileInfoBlock info;
};

typedef char FindFileInternalFitsInResults[
	sizeof(FindFileInternal) <= sizeof(((FindFileResults*)0)->internalData) ? 1 : -1
];

static int filesOpen = 0;

File* File_Open(const char *file, FileOpenMode mode)
{
#if DEBUG_FILE_IO
	uint32_t t0 = GetMilliseconds();
#endif
	CallingDOS();
	File* result = 0;
	BPTR handle = Open(file, mode == ReadOnly ? MODE_OLDFILE : MODE_READWRITE);
	if (handle != 0)
	{
		result = Allocate<File>("File", 1, true);
		if (result != 0)
			result->handle = handle;
		else
			Close(handle);
	}
	AfterCallingDOS();
#if DEBUG_FILE_IO
	DebugPrintf("File_Open(%s,%s) => %p in %lu ms\n",
		file,
		mode == ReadOnly ? "r" : "rw",
		result,
		(unsigned long)(GetMilliseconds() - t0));
#endif

	if (result != 0)
		filesOpen++;
	return result;
}

File* File_Create(const char *file)
{
	CallingDOS();
	File* result = 0;
	BPTR handle = Open(file, MODE_NEWFILE);
	if (handle != 0)
	{
		result = Allocate<File>("File", 1, true);
		if (result != 0)
			result->handle = handle;
		else
			Close(handle);
	}
	AfterCallingDOS();
	return result;
}

static FileInfoBlock* GetStaticFileInfoBlock()
{
	static uint8_t fib[sizeof(FileInfoBlock) + 16];

	// Ensure proper alignment to 8 bytes
	uint8_t* addr = (uint8_t*)fib;
	if ((((uint32_t)addr) & 7) != 0)
		addr += 8 - ((uint32_t)addr & 7);

	return (FileInfoBlock*)addr;
}

uint64_t File_GetSizeByName(const char* file)
{
	CallingDOS();
	BPTR lock = Lock(file, ACCESS_READ);
	if (lock == 0)
	{
		AfterCallingDOS();
		return 0;
	}

	uint64_t size = 0;

	FileInfoBlock* fib = GetStaticFileInfoBlock();
	if (fib != 0)
	{
		if (Examine(lock, fib))
			size = fib->fib_Size;
	}

	UnLock(lock);
	AfterCallingDOS();
	return size;
}

uint64_t File_Read(File* file, void* buffer, uint64_t size)
{
#if DEBUG_FILE_IO
	uint32_t t0 = GetMilliseconds();
#endif
	CallingDOS();
	uint64_t result = Read(file->handle, buffer, size);
	AfterCallingDOS();
#if DEBUG_FILE_IO
	DebugPrintf("File_Read(%p,%lu) => %lu in %lu ms\n",
		file,
		(unsigned long)size,
		(unsigned long)result,
		(unsigned long)(GetMilliseconds() - t0));
#endif
	return result;
}

uint64_t File_Write(File* file, const void* buffer, uint64_t size)
{
	CallingDOS();
	uint64_t result = Write(file->handle, (APTR)buffer, size);
	AfterCallingDOS();
	return result;
}

bool File_Seek(File* file, uint64_t offset)
{
#if DEBUG_FILE_IO
	uint32_t t0 = GetMilliseconds();
#endif
	CallingDOS();
	LONG result = Seek(file->handle, offset, OFFSET_BEGINNING);
	AfterCallingDOS();
#if DEBUG_FILE_IO
	DebugPrintf("File_Seek(%p,%lu) => %ld in %lu ms\n",
		file,
		(unsigned long)offset,
		(long)result,
		(unsigned long)(GetMilliseconds() - t0));
#endif
	return result != -1;
}

uint64_t File_GetSize(File* file)
{
#if DEBUG_FILE_IO
	uint32_t t0 = GetMilliseconds();
#endif
	CallingDOS();
	LONG size = -1;
	// ExamineFH() is not available on Kickstart 1.3 / DOS v34 (A500-era ROMs).
	// Use it only when the DOS library is new enough, otherwise fall back to Seek().
	if (DOSBase != 0 && DOSBase->dl_lib.lib_Version >= 36)
	{
		FileInfoBlock* fib = GetStaticFileInfoBlock();
		MemClear(fib, sizeof(*fib));
		if (ExamineFH(file->handle, fib))
			size = fib->fib_Size;
	}

	if (size < 0)
	{
		LONG pos = Seek(file->handle, 0, OFFSET_CURRENT);
		Seek(file->handle, 0, OFFSET_END);
		size = Seek(file->handle, 0, OFFSET_CURRENT);
		Seek(file->handle, pos, OFFSET_BEGINNING);
	}
	AfterCallingDOS();
#if DEBUG_FILE_IO
	DebugPrintf("File_GetSize(%p) => %lu in %lu ms\n",
		file,
		(unsigned long)size,
		(unsigned long)(GetMilliseconds() - t0));
#endif
	return size;
}

uint64_t File_GetPosition(File* file)
{
	return Seek(file->handle, 0, OFFSET_CURRENT);
}

void File_Close(File* file)
{
	if (file != 0)
	{
		Close(file->handle);
		Free(file);
		filesOpen--;
	}
}

static void FillFindResults(FindFileResults* info)
{
	FindFileInternal* i = (FindFileInternal*)(info->internalData);

	if (i->info.fib_DirEntryType >= 0)
		info->attributes = FileAttribute_Directory;
	else
		info->attributes = 0;

	StrCopy(info->fileName, sizeof(info->fileName), i->info.fib_FileName);
	StrCopy(info->description, sizeof(info->description), i->info.fib_Comment);
	info->fileSize = i->info.fib_Size;
}

static bool PatternMatches(const char* p, const char* f)
{
	while (*p != 0)
	{
		if (*p == '?')
		{
			if (*f == 0)
				return false;
			p++;
			f++;
		}
		else if (*p == '*')
		{
			p++;
			if (*p == 0)
				return true;
			while (*f != 0)
			{
				if (PatternMatches(p, f))
					return true;
				f++;
			}
			return false;
		}
		else if (ToUpper((uint8_t)*p) == ToUpper((uint8_t)*f))
		{
			p++;
			f++;
		}
		else
			return false;
	}
	return *p == 0 && *f == 0;
}

bool OS_FindFirstFile(const char* pattern, FindFileResults* info)
{
	FindFileInternal* i = (FindFileInternal*)(info->internalData);

	i->pattern = pattern;
	i->lock = Lock("", ACCESS_READ);
	if (i->lock == 0)
		return false;
	if (!Examine(i->lock, &i->info))
	{
		UnLock(i->lock);
		return false;
	}
	FillFindResults(info);
	if (!PatternMatches(pattern, i->info.fib_FileName))
		return OS_FindNextFile(info);
	return true;
}

bool OS_FindNextFile(FindFileResults* info)
{
	FindFileInternal* i = (FindFileInternal*)(info->internalData);

	if (!ExNext(i->lock, &i->info))
		return false;
	FillFindResults(info);
	if (!PatternMatches(i->pattern, i->info.fib_FileName))
		return OS_FindNextFile(info);
	return true;
}

bool OS_GetCurrentDirectory(char* buffer, size_t bufferSize)
{
	if (buffer == 0 || bufferSize == 0)
		return false;
	CallingDOS();
	LONG result = GetCurrentDirName(buffer, (LONG)bufferSize);
	AfterCallingDOS();
	return result != 0;
}

bool OS_ChangeDirectory(const char* path)
{
	if (path == 0)
		return false;
	CallingDOS();
	BOOL ok = SetCurrentDirName((STRPTR)path);
	AfterCallingDOS();
	return ok != 0;
}

#endif
