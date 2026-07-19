#include <os_file.h>
#include <os_lib.h>
#include <os_char.h>
#include <os_mem.h>

#ifdef _AMIGA

#include "video.h"
#include "timer.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>

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

bool File_Flush(File* file)
{
	CallingDOS();
	LONG result = Flush(file->handle);
	AfterCallingDOS();
	return result != 0;
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

// The directory scan must release the blitter around its DOS calls like
// every other file operation here: on OCS Kickstarts trackdisk decodes MFM
// with the blitter, so enumerating a disk while the player owns it
// deadlocks the filesystem handler (the post-swap rescan was the first
// enumeration to ever run with the system taken).

static bool FindNextInternal(FindFileResults* info)
{
	FindFileInternal* i = (FindFileInternal*)(info->internalData);

	if (i->lock == 0)
		return false;
	for (;;)
	{
		if (!ExNext(i->lock, &i->info))
		{
			// End of directory: release the lock here, as no explicit
			// close call exists in the enumeration interface. Leaking it
			// would keep the outgoing volume referenced across disk swaps.
			UnLock(i->lock);
			i->lock = 0;
			return false;
		}
		FillFindResults(info);
		if (PatternMatches(i->pattern, i->info.fib_FileName))
			return true;
	}
}

bool OS_FindFirstFile(const char* pattern, FindFileResults* info)
{
	FindFileInternal* i = (FindFileInternal*)(info->internalData);

	CallingDOS();
	bool result = false;
	i->pattern = pattern;
	i->lock = Lock("", ACCESS_READ);
	if (i->lock != 0)
	{
		if (!Examine(i->lock, &i->info))
			UnLock(i->lock);
		else
		{
			FillFindResults(info);
			result = PatternMatches(pattern, i->info.fib_FileName) ? true : FindNextInternal(info);
		}
	}
	AfterCallingDOS();
	return result;
}

bool OS_FindNextFile(FindFileResults* info)
{
	CallingDOS();
	bool result = FindNextInternal(info);
	AfterCallingDOS();
	return result;
}

// ---- Physical disk swap support -------------------------------------------
// The player normally runs with the boot floppy's root as its current
// directory. AmigaDOS locks are bound to a *volume*, so after the user swaps
// disks, any access through that directory would make DOS request the old
// volume back (invisibly, as the player owns the display). These helpers
// re-bind the current directory to a floppy *device*, so file access follows
// whatever disk is currently in a drive. The requested disk is accepted in
// any drive: the first drive holding a volume other than the one we were
// bound to wins, so both single-drive swaps and a second disk sitting in
// DF1: work. Uses only Kickstart 1.x-safe DOS calls and the classic
// Forbid'ed device-list walk.

#define MAX_SWAP_DEVICES 5

static char driveNames[MAX_SWAP_DEVICES][36];
static int  driveCount = 0;
static BPTR initialCurrentDir = 0;
static bool currentDirReplaced = false;
static bool bootMediaInfoCaptured = false;
static bool bootMediaSwappable = false;
static BPTR lastBoundVolume = 0;

static void CopyDosListName(struct DosList* node, char* output, size_t outputSize)
{
	const uint8_t* name = (const uint8_t*)BADDR(node->dol_Name);
	int length = name[0];
	if (length > (int)outputSize - 2)
		length = (int)outputSize - 2;
	for (int i = 0; i < length; i++)
		output[i] = (char)name[i + 1];
	output[length] = ':';
	output[length + 1] = 0;
}

static bool IsFloppyDeviceName(const char* name)
{
	return (name[0] == 'D' || name[0] == 'd') &&
	       (name[1] == 'F' || name[1] == 'f') &&
	        name[2] >= '0' && name[2] <= '3' && name[3] == ':';
}

void AmigaInitBootMedia()
{
	if (bootMediaInfoCaptured)
		return;
	bootMediaInfoCaptured = true;
	bootMediaSwappable = false;

	struct Process* self = (struct Process*)FindTask(0);
	BPTR cd = self->pr_CurrentDir;
	if (cd == 0)
	{
		// The boot shell runs with a zero current-directory lock, which
		// AmigaDOS treats as the root of the boot volume (the reason classic
		// startup-sequences began with "CD :"). Bind a real lock so the
		// volume and its handler can be inspected and later re-bound.
		BPTR rootLock = Lock((CONST_STRPTR)":", ACCESS_READ);
		if (rootLock == 0)
		{
			DebugPrintf("Swap: cannot lock the boot volume root\n");
			return;
		}
		CurrentDir(rootLock);
		initialCurrentDir = 0;
		currentDirReplaced = true;
		cd = rootLock;
	}
	else
	{
		// Only a volume root may be re-bound: games running from a
		// subdirectory (hard disk installs) must keep their working
		// directory untouched.
		BPTR parent = ParentDir(cd);
		if (parent != 0)
		{
			UnLock(parent);
			DebugPrintf("Swap: current dir is not a volume root\n");
			return;
		}
	}

	struct MsgPort* handler = ((struct FileLock*)BADDR(cd))->fl_Task;
	lastBoundVolume = ((struct FileLock*)BADDR(cd))->fl_Volume;

	// Candidate drives for disk swaps: the boot volume's own device first
	// when it is not a floppy (unusual boot devices), then DF0-DF3 in unit
	// order. The handler match alone is not relied upon: floppy drives are
	// always candidates, so a missing boot-device association cannot
	// disable swapping.
	char bootName[36];
	char floppyNames[4][36];
	bool floppyPresent[4] = { false, false, false, false };
	char nodeName[36];
	bootName[0] = 0;
	Forbid();
	struct DosInfo* info = (struct DosInfo*)BADDR(DOSBase->dl_Root->rn_Info);
	for (BPTR n = info->di_DevInfo; n != 0; )
	{
		struct DosList* node = (struct DosList*)BADDR(n);
		if (node->dol_Type == DLT_DEVICE)
		{
			CopyDosListName(node, nodeName, sizeof(nodeName));
			if (IsFloppyDeviceName(nodeName))
			{
				int unit = nodeName[2] - '0';
				StrCopy(floppyNames[unit], sizeof(floppyNames[unit]), nodeName);
				floppyPresent[unit] = true;
			}
			else if (handler != 0 && node->dol_Task == handler && bootName[0] == 0)
			{
				StrCopy(bootName, sizeof(bootName), nodeName);
			}
		}
		n = node->dol_Next;
	}
	Permit();

	driveCount = 0;
	if (bootName[0] != 0)
		StrCopy(driveNames[driveCount++], sizeof(driveNames[0]), bootName);
	for (int unit = 0; unit < 4 && driveCount < MAX_SWAP_DEVICES; unit++)
	{
		if (floppyPresent[unit])
			StrCopy(driveNames[driveCount++], sizeof(driveNames[0]), floppyNames[unit]);
	}
	bootMediaSwappable = driveCount > 0;

	if (bootMediaSwappable)
	{
		for (int i = 0; i < driveCount; i++)
			DebugPrintf("Swap candidate drive %d: %s\n", i, driveNames[i]);
	}
	else
	{
		DebugPrintf("Boot media is not swappable\n");
	}
}

bool OS_RemountBootMedia()
{
	CallingDOS();
	AmigaInitBootMedia();
	bool bound = false;
	if (bootMediaSwappable)
	{
		// Prefer the first drive whose volume is not the one we were bound
		// to (that is where the user put the new disk); with no new volume
		// anywhere, stay on the current one so a rescan can still run.
		BPTR chosen = 0;
		BPTR fallback = 0;
		for (int i = 0; i < driveCount && chosen == 0; i++)
		{
			BPTR lock = Lock((CONST_STRPTR)driveNames[i], ACCESS_READ);
			if (lock == 0)
			{
				DebugPrintf("Swap: no medium in %s\n", driveNames[i]);
				continue;
			}
			DebugPrintf("Swap: %s volume %08lx (bound to %08lx)\n", driveNames[i],
				(unsigned long)((struct FileLock*)BADDR(lock))->fl_Volume,
				(unsigned long)lastBoundVolume);
			if (((struct FileLock*)BADDR(lock))->fl_Volume != lastBoundVolume)
				chosen = lock;
			else if (fallback == 0)
				fallback = lock;
			else
				UnLock(lock);
		}
		if (chosen == 0)
		{
			chosen = fallback;
			fallback = 0;
		}
		if (fallback != 0)
			UnLock(fallback);
		if (chosen != 0)
		{
			lastBoundVolume = ((struct FileLock*)BADDR(chosen))->fl_Volume;
			BPTR previous = CurrentDir(chosen);
			if (!currentDirReplaced)
			{
				// The initial lock belongs to the caller's CLI; keep it so it
				// can be restored on exit instead of unlocking it.
				initialCurrentDir = previous;
				currentDirReplaced = true;
			}
			else if (previous != 0)
				UnLock(previous);
			bound = true;
		}
	}
	DebugPrintf("Swap: remount %s\n", bound ? "bound a volume" : "did not bind");
	AfterCallingDOS();
	return bound;
}

void AmigaRestoreCurrentDir()
{
	if (!currentDirReplaced)
		return;
	BPTR mine = CurrentDir(initialCurrentDir);
	if (mine != 0)
		UnLock(mine);
	currentDirReplaced = false;
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
