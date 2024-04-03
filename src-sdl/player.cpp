#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>
#include <os_file.h>
#include <os_lib.h>

#if _OSX
#include <unistd.h>
#endif

#include <SDL.h>

#if _WEB

#include <emscripten.h>
#include <emscripten/html5.h>

void TracePrintf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

extern bool quit;

void VID_InnerLoop(void (*callback)(int elapsed));

static char fileBuffer[2048];
static uint32_t fileSize;
static char* filePtr = 0;
static const char* dataPath = "/data/";

static EM_BOOL EM_InnerLoop(double time, void* userData)
{
	if (!quit)
		VID_InnerLoop();
	if (quit)
		DDB_RunPlayerAsync(dataPath);
	return !quit;
}

static void getNextFile();

static void run()
{
	DebugPrintf("Running game from \"%s\"\n", dataPath);

	OSInit();
	emscripten_request_animation_frame_loop(EM_InnerLoop, 0);
	DDB_RunPlayerAsync(dataPath);
}

static void fileLoadFailed(const char* lastFile)
{
	DebugPrintf("Failed to load file %s\n", lastFile);
	getNextFile();
}

static void fileLoaded(const char* lastFile)
{
	DebugPrintf("Loaded %s\n", lastFile);
	getNextFile();
}

static void getNextFile()
{
	if (filePtr == 0 || *filePtr == 0)
	{
		run();
	}
	else
	{
		static char fileNameBuffer[256];
		const char* fileName = filePtr;
		while (*filePtr != '\n' && *filePtr != '\r' && *filePtr != 0)
			filePtr++;
		while (*filePtr == '\n' || *filePtr == '\r')
			*filePtr++ = 0;

		if (filePtr - fileName > 250)
		{
			DebugPrintf("File name %s too long", fileName);
			return;
		}

		DebugPrintf("Loading \'%s\'\n", fileName);

		StrCopy(fileNameBuffer, 256, dataPath);
		StrCat(fileNameBuffer, 256, fileName);
		emscripten_async_wget(fileName, fileNameBuffer, fileLoaded, fileLoadFailed);
	}
}

static void fileListFound(const char*)
{
	File* file = File_Open("files.txt", ReadOnly);
	fileSize = File_Read(file, fileBuffer, 2047);
	fileBuffer[fileSize] = 0;
	File_Close(file);
	filePtr = fileBuffer;
	getNextFile();
}

static void fileListNotFound(const char*)
{
	DebugPrintf("File list not found\n");
	run();
}

static bool CheckExtension(const char* filename, const char* ext)
{
	const char* p = StrRChr(filename, '.');
	if (!p)
		return false;
	return StrIComp(p + 1, ext) == 0;
}

extern "C" 
{
	void RunGameFromTXT()
	{
		emscripten_async_wget("files.txt", "files.txt", fileListFound, fileListNotFound);
	}
	void RunGameFrom(const char* path)
	{
		size_t size = StrLen(path) + 1;
		dataPath = Allocate<char>("dataPath", size);
		StrCopy((char*)dataPath, size, path);
		run();
	}
	void RunGame()
	{
		run();
	}
	bool SaveFile(const char* name, void* buffer, size_t length)
	{
		File* file = File_Create(name);
		if (file == NULL)
		{
			DebugPrintf("Failed to create file %s\n", name);
			return false;
		}
		File_Write(file, buffer, length);
		File_Close(file);
		DebugPrintf("Saved file %s (%u bytes)\n", name, (unsigned)length);
		OSSyncFS();

		if (CheckExtension(name, "adf") ||
			CheckExtension(name, "dsk") ||
			CheckExtension(name, "st"))
		{
			if (File_MountDisk(name))
			{
				DebugPrintf("Mounted disk %s\n", name);
				dataPath = "";
				run();
				return true;
			}
			else
			{
				DebugPrintf("Failed to mount disk %s\n", name);
			}
		}
		return false;
	}
	void QuitGame()
	{
		VID_Quit();
	}
}

int main (int argc, char *argv[])
{
	return 0;
}

#else

#include <stdarg.h>
#include <stdio.h>

void TracePrintf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

int main (int argc, char *argv[])
{
	#if _OSX
	chdir(SDL_GetBasePath());
	#endif

	OSInit();
	if (!DDB_RunPlayer())
		OSError(DDB_GetErrorString());
	return 1;
}
#endif