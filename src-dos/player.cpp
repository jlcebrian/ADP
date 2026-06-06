#ifdef _DOS

#include <ddb.h>
#include <ddb_vid.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_file.h>

#include <conio.h>
#include <string.h>

extern void DOS_InitStackWatermark();

static void error(const char* message)
{
	DDB_Error errorCode = DDB_GetError();
	DebugPrintf("PLAYER: fatal error code=%d screenMode=%u message=%s\n",
		(int)errorCode,
		(unsigned)screenMode,
		message != 0 ? message : "(null)");
	VID_Finish();

	if (errorCode == DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED || errorCode == DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED)
	{
		const char* videoMessage = VID_DescribeVideoModeError(errorCode, screenMode);
		if (videoMessage != 0)
		{
			cputs("Error: ");
			cputs(videoMessage);
			cputs("\r\n");
			exit(1);
		}

		cputs("Error: Unsupported video mode");
		cputs("\r\n");
		exit(1);
	}

	cputs("Error: ");
	cputs(message);
	cputs("\r\n");
	exit(1);
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

static bool ChangeToGamePath(const char* path)
{
	if (path == 0 || path[0] == 0)
		return false;
	if (OS_ChangeDirectory(path))
		return true;

	const char* slash = StrRChr(path, '/');
	const char* backslash = StrRChr(path, '\\');
	if (slash == 0 || (backslash != 0 && backslash > slash))
		slash = backslash;
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

extern "C" int main (int argc, char**argv)
{
	const char* gamePath = 0;
	DebugPrintf("PLAYER: startup build=%s %s argc=%d\n", __DATE__, __TIME__, argc);
	DOS_InitStackWatermark();
	DDB_SetStartupVideoModePolicy(DDB_StartupVideoModePolicy_Configurable);
	DDB_ClearStartupScreenModeOverride();
	for (int i = 1; i < argc; i++)
	{
		DebugPrintf("PLAYER: argv[%d]=%s\n", i, argv[i] != 0 ? argv[i] : "(null)");
		DDB_ScreenMode mode = ParseScreenModeArgument(argv[i]);
		if (mode != ScreenMode_Default)
		{
			DebugPrintf("PLAYER: startup video override=%u\n", (unsigned)mode);
			DDB_SetStartupScreenModeOverride(mode);
			continue;
		}
		if (gamePath == 0)
			gamePath = argv[i];
	}
	DebugPrintf("PLAYER: gamePath=%s\n", gamePath != 0 ? gamePath : "(none)");

	if (gamePath != 0 && !ChangeToGamePath(gamePath))
	{
		DebugPrintf("PLAYER: ChangeToGamePath failed for %s\n", gamePath);
		cputs("Warning: unable to change directory\r\n");
	}

	DebugPrintf("PLAYER: invoking DDB_RunPlayer\n");
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	DebugPrintf("PLAYER: DDB_RunPlayer returned success\n");
	return 1;
}

#endif
