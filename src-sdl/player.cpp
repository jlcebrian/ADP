#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>

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

int main (int argc, char *argv[])
{
	OSInit();
	emscripten_request_animation_frame_loop(EM_InnerLoop, 0);
	DDB_RunPlayerAsync("/data/");
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