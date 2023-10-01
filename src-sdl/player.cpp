#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>
#include <os_file.h>
#include <os_lib.h>

#include <SDL.h>

#if _WEB

#include <emscripten.h>
#include <emscripten/html5.h>

extern bool quit;

void VID_InnerLoop(void (*callback)(int elapsed));

static EM_BOOL EM_InnerLoop(double time, void* userData)
{
	if (!quit)
		VID_InnerLoop();
	if (quit)
		DDB_RunPlayerAsync("/data/");

	return !quit;
}

static char fileBuffer[2048];
static uint32_t fileSize;
static char* filePtr = 0;

static void getNextFile();

static void run()
{
	OSInit();
	emscripten_request_animation_frame_loop(EM_InnerLoop, 0);
	DDB_RunPlayerAsync("/data/");
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

		StrCopy(fileNameBuffer, 256, "/data/");
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

int main (int argc, char *argv[])
{
	emscripten_async_wget("files.txt", "files.txt", fileListFound, fileListNotFound);
	return 0;
}

#else

void TracePrintf(const char* format, ...)
{
}

int main (int argc, char *argv[])
{
	OSInit();
	if (!DDB_RunPlayer())
		OSError(DDB_GetErrorString());
	return 1;
}
#endif