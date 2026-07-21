#ifdef _DOS

#include <ddb.h>
#include <ddb_vid.h>
#if defined(DOS_TEST_BUILD)
#include <ddb_scr.h>
#include <ddb_test.h>
#include <stdlib.h>
#endif
#include <os_lib.h>
#include <os_mem.h>
#include <os_file.h>
#include "setup_config.h"
#include "game_modes.h"
#include "sb.h"

#include <conio.h>
#include <string.h>

extern void DOS_InitStackWatermark();
extern void DOS_GetStackWatermark(uint32_t* usedBytes, uint32_t* totalBytes);

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
		return ScreenMode_VGA;
	if (StrIComp(arg, "vga16") == 0)
		return ScreenMode_VGA16;
	if (StrIComp(arg, "svga") == 0 || StrIComp(arg, "sga") == 0)
		return ScreenMode_SHiRes;
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
	#if defined(DOS_TEST_BUILD)
	const char* inputFileName = 0;
	const char* transcriptFileName = 0;
	int partSelection = 0;
	#endif
	DebugPrintf("PLAYER: startup build=%s %s argc=%d\n", __DATE__, __TIME__, argc);
	DOS_InitStackWatermark();
	DDB_SetStartupVideoModePolicy(DDB_StartupVideoModePolicy_OverrideOrHighest);
	DDB_ClearStartupScreenModeOverride();
	bool modeInCommandLine = false;
	for (int i = 1; i < argc; i++)
	{
		#if defined(DOS_TEST_BUILD)
		if (StrIComp(argv[i], "-i") == 0 && i + 1 < argc)
		{
			inputFileName = argv[++i];
			continue;
		}
		if (StrIComp(argv[i], "-o") == 0 && i + 1 < argc)
		{
			transcriptFileName = argv[++i];
			continue;
		}
		if ((StrIComp(argv[i], "--part") == 0 || StrIComp(argv[i], "-p") == 0) && i + 1 < argc)
		{
			partSelection = atoi(argv[++i]);
			continue;
		}
		#endif
		DebugPrintf("PLAYER: argv[%d]=%s\n", i, argv[i] != 0 ? argv[i] : "(null)");
		DDB_ScreenMode mode = ParseScreenModeArgument(argv[i]);
		if (mode != ScreenMode_Default)
		{
			DebugPrintf("PLAYER: startup video override=%u\n", (unsigned)mode);
			DDB_SetStartupScreenModeOverride(mode);
			modeInCommandLine = true;
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

	ADPSetupConfig setup;
	if (ADPSetup_Load(ADP_SETUP_CONFIG_FILE, &setup))
	{
		if (!modeInCommandLine)
			DDB_SetStartupScreenModeOverride(setup.videoMode);
		if (setup.card != SoundCard_None)
		{
			SB_SetMaxVersion(ADPSetup_CardDSPCap(setup.card));
			SB_Configure(setup.sbPort, setup.sbIrq, setup.sbDMA);
			SB_ConfigureOutput(0, setup.sampleRate);   // 8-bit mono
		}
		else SB_Disable();
	}
	else
	{
		// Without ADPSETUP.CFG, the game can still run if the video mode
		// choice is already settled: either a mode was given in the
		// command line, or the game data supports a single kind of
		// graphics. Only ask for SETUP when there is an actual choice.
		if (!modeInCommandLine)
		{
			uint32_t gameModes = Setup_DetectGameVideoModes() & SETUP_SELECTABLE_VIDEO_MODES;
			if ((gameModes & (gameModes - 1)) != 0)
			{
				cputs("This game supports several graphic modes.\r\n"
				      "Please run SETUP.EXE before starting ADP.\r\n");
				return 1;
			}
		}
		SB_UseBlasterAutodetect();
	}

	#if defined(DOS_TEST_BUILD)
	if (transcriptFileName != 0)
		DDB_UseTranscriptFile(transcriptFileName);
	if (partSelection > 0)
		DDB_TestSetPartSelection(partSelection, 0);
	if (inputFileName != 0)
		SCR_UseInputFile(inputFileName);
	#endif

	DebugPrintf("PLAYER: invoking DDB_RunPlayer\n");
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	DebugPrintf("PLAYER: DDB_RunPlayer returned success\n");

	// Stack-overflow detection (debug builds): report peak usage and flag any
	// overshoot of the budget. A hog here is a bug — see scripts/dos-stack-report.py.
	uint32_t stackUsed = 0, stackTotal = 0;
	DOS_GetStackWatermark(&stackUsed, &stackTotal);
	if (stackTotal != 0)
	{
		DebugPrintf("PLAYER: peak stack usage %lu / %lu bytes\n",
			(unsigned long)stackUsed, (unsigned long)stackTotal);
		if (stackUsed > 12288)
			DebugPrintf("PLAYER: *** WARNING: stack usage %lu exceeds the 12K budget "
				"(hog: scripts/dos-stack-report.py) ***\n", (unsigned long)stackUsed);
	}
	return 1;
}

#endif
