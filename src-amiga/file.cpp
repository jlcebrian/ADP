#include <os_file.h>
#include <os_lib.h>

#ifdef _AMIGA

#include "video.h"

#include <proto/dos.h>

struct FindFileInternal
{
	const char*   pattern;
	BPTR          lock;
	FileInfoBlock info;
};

static int filesOpen = 0;

File* File_Open(const char *file, FileOpenMode mode)
{
	CallingDOS();
	File* result = (File*)Open(file, mode == ReadOnly ? MODE_OLDFILE : MODE_READWRITE);
	AfterCallingDOS();

	if (result != 0)
		filesOpen++;
	return result;
}

File* File_Create(const char *file)
{
	CallingDOS();
	File* result = (File*)Open(file, MODE_NEWFILE);
	AfterCallingDOS();
	return result;
}

uint64_t File_Read(File* file, void* buffer, uint64_t size)
{
	CallingDOS();
	uint64_t result = Read((BPTR)file, buffer, size);
	AfterCallingDOS();
	return result;
}

uint64_t File_Write(File* file, const void* buffer, uint64_t size)
{
	CallingDOS();
	uint64_t result = Write((BPTR)file, (APTR)buffer, size);
	AfterCallingDOS();
	return result;
}

bool File_Seek(File* file, uint64_t offset)
{
	CallingDOS();
	bool result = Seek((BPTR)file, offset, OFFSET_BEGINNING) == (LONG)offset;
	AfterCallingDOS();
	return result;
}

uint64_t File_GetSize(File* file)
{
	CallingDOS();
	LONG pos  = Seek((BPTR)file, 0, OFFSET_CURRENT);
	Seek((BPTR)file, 0, OFFSET_END);
	LONG size  = Seek((BPTR)file, 0, OFFSET_CURRENT);
	Seek((BPTR)file, pos, OFFSET_BEGINNING);
	AfterCallingDOS();
	return size;
}

uint64_t File_GetPosition(File* file)
{
	return Seek((BPTR)file, 0, OFFSET_CURRENT);
}

void File_Close(File* file)
{
	Close((BPTR)file);
	if (file != 0)
		filesOpen--;
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
		else if (*p == *f)
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