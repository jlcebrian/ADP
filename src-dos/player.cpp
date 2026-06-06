#ifdef _DOS

#include <ddb.h>
#include <ddb_vid.h>
#include <os_lib.h>
#include <os_mem.h>

#include <conio.h>
#include <direct.h>
#include <string.h>

extern void DOS_InitStackWatermark();

static void error(const char* message)
{
	VID_Finish();

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

extern "C" int main (int argc, char**argv)
{
	DOS_InitStackWatermark();
	DDB_SetStartupVideoModePolicy(DDB_StartupVideoModePolicy_Configurable);
	DDB_ClearStartupScreenModeOverride();
	for (int i = 1; i < argc; i++)
	{
		DDB_ScreenMode mode = ParseScreenModeArgument(argv[i]);
		if (mode != ScreenMode_Default)
		{
			DDB_SetStartupScreenModeOverride(mode);
			break;
		}
	}
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	return 1;
}

#endif
