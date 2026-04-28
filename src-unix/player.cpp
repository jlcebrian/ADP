#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#include <stdio.h>
#include <string.h>

void TracePrintf(const char* format, ...)
{
}

static void error(const char* message)
{
	VID_Finish();

	printf("Error: %s\n", message);
	exit(1);
}

static bool ChangeToGamePath(const char* path)
{
	if (path == 0 || path[0] == 0)
		return false;
	if (OS_ChangeDirectory(path))
		return true;

	const char* slash = StrRChr(path, '/');
	if (slash == 0)
		return false;

	char folder[FILE_MAX_PATH];
	size_t length = (size_t)(slash - path);
	if (length >= sizeof(folder))
		return false;
	MemCopy(folder, path, length);
	folder[length] = 0;
	return OS_ChangeDirectory(folder);
}

static DDB_ScreenMode ParseScreenModeArgument(const char* arg)
{
	if (arg == 0 || arg[0] == 0)
		return ScreenMode_Default;
	if (StrIComp(arg, "cga") == 0)
		return ScreenMode_CGA;
	if (StrIComp(arg, "ega") == 0)
		return ScreenMode_EGA;
	if (StrIComp(arg, "vga") == 0)
		return ScreenMode_VGA16;
	if (StrIComp(arg, "svga") == 0 || StrIComp(arg, "sga") == 0)
		return ScreenMode_VGA;
	if (StrIComp(arg, "hires") == 0)
		return ScreenMode_HiRes;
	if (StrIComp(arg, "shires") == 0 || StrIComp(arg, "superhires") == 0)
		return ScreenMode_SHiRes;
	return ScreenMode_Default;
}

int main (int argc, char**argv)
{
	const char* gamePath = 0;
	DDB_SetStartupVideoModePolicy(DDB_StartupVideoModePolicy_OverrideOrHighest);
	DDB_ClearStartupScreenModeOverride();
	for (int i = 1; i < argc; i++)
	{
		DDB_ScreenMode mode = ParseScreenModeArgument(argv[i]);
		if (mode != ScreenMode_Default)
		{
			DDB_SetStartupScreenModeOverride(mode);
			continue;
		}
		if (gamePath == 0)
			gamePath = argv[i];
	}

	if (gamePath != 0 && !ChangeToGamePath(gamePath))
		printf("Warning: unable to change directory to '%s'\n", gamePath);

	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	return 1;
}


