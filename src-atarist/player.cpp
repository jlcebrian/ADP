#include <dmg.h>
#include <ddb.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_file.h>
#include <os_mem.h>

#ifdef _ATARIST

#include <mint/cookie.h>
#include <mint/basepage.h>
#include <mint/ostruct.h>
#include <mint/sysvars.h>
#include <osbind.h>

#include <stdio.h>
#include <sys/dirent.h>
#include <string.h>
#include <gem.h>

static uint32_t ret;
static uint16_t defaultPalette[16];
static uint16_t savedPalette[256];
static uint16_t savedConterm;
static void* savedLogbase;
static void* savedPhysbase;
static int16_t savedRez;
static int16_t savedShift;
static int16_t savedFalconMode;
static bool savedFalconModeValid;

static bool GetExecutablePathFromBasepage(char* buffer, size_t bufferSize)
{
	if (buffer == 0 || bufferSize == 0 || _base == 0 || _base->p_env == 0)
		return false;

	const char* ptr = _base->p_env;
	while (*ptr != 0)
	{
		while (*ptr++ != 0)
		{
		}
	}
	ptr++;
	if (*ptr == 0)
		return false;

	strncpy(buffer, ptr, bufferSize - 1);
	buffer[bufferSize - 1] = 0;
	return true;
}

static bool ChangeToExecutableDirectory(int argc, char* argv[])
{
	char executablePath[FILE_MAX_PATH];
	executablePath[0] = 0;

	if (!GetExecutablePathFromBasepage(executablePath, sizeof(executablePath)))
	{
		if (argc <= 0 || argv == 0 || argv[0] == 0)
			return false;

		strncpy(executablePath, argv[0], sizeof(executablePath) - 1);
		executablePath[sizeof(executablePath) - 1] = 0;
	}

	char* lastSlash = strrchr(executablePath, '\\');
	char* lastAltSlash = strrchr(executablePath, '/');
	char* lastSeparator = lastSlash;
	if (lastAltSlash != 0 && (lastSeparator == 0 || lastAltSlash > lastSeparator))
		lastSeparator = lastAltSlash;

	if (lastSeparator == 0)
		return false;

	if (lastSeparator == executablePath || (lastSeparator == executablePath + 2 && executablePath[1] == ':'))
		lastSeparator[1] = 0;
	else
		*lastSeparator = 0;

	return OS_ChangeDirectory(executablePath);
}

enum
{
	FalconModeInquire = -1,
	FalconModeSTLow = 0x0082,
};

static bool HasFalconVideo()
{
	long cookieValue;
	return Getcookie(C__VDO, &cookieValue) == C_FOUND && cookieValue >= 0x00030000L;
}

static int16_t FalconSetMode(int16_t mode)
{
	return (int16_t)trap_14_ww((short)88, (short)mode);
}

static void FalconSetScreen(void* logbase, void* physbase, int16_t mode)
{
	(void)trap_14_wllww((short)0x05, (long)logbase, (long)physbase,
		(short)SCR_MODECODE, (short)mode);
}

static void init()
{
	bool hasFalconVideo = HasFalconVideo();
	int16_t initialFalconMode = hasFalconVideo ? FalconSetMode(FalconModeInquire) : -1;

	ret = Super(0L);
	savedLogbase = Logbase();
	savedPhysbase = Physbase();
	savedRez = Getrez();
	savedShift = hasFalconVideo ? -1 : EgetShift();
	savedFalconModeValid = false;
	if (savedRez != ST_LOW)
	{
		EgetPalette(0, 256, savedPalette);
		if (hasFalconVideo)
		{
			savedFalconMode = initialFalconMode;
			FalconSetScreen(0, 0, FalconModeSTLow);
			savedFalconModeValid = true;
		}
		else
		{
			EsetShift(ST_LOW);
			Setscreen(-1, -1, ST_LOW);
		}
	}

	for (int n = 0; n < 16; n++)
		defaultPalette[n] = Setcolor(n, -1);

	// Disable the TOS key click but preserve repeat and other console flags.
	savedConterm = *conterm;
	*conterm = (uint16_t)(savedConterm & ~1u);
}

static void quit()
{
	Vsync();
	memset(Physbase(), 0, 32000);
	for (int n = 0; n < 16; n++)
		Setcolor(n, defaultPalette[n]);
	*conterm = savedConterm;
	if (savedRez != ST_LOW)
	{
		if (savedFalconModeValid)
			FalconSetScreen(savedLogbase, savedPhysbase, savedFalconMode);
		else
		{
			Setscreen(savedLogbase, savedPhysbase, -1);
			EsetShift(savedShift);
		}
		EsetPalette(0, 256, savedPalette);
	}

	OSReleaseArena();

	Super(ret);
	Pterm(0);
}

static void error(const char* message)
{
	VID_Finish();

	char text[256];
	strcpy(text, "[4][");
	strcat(text, message);
	strcat(text, "][Ok]");

	appl_init();
	form_alert(1, text);
	appl_exit();

	quit();
	exit(1);
}

int main (int argc, char *argv[])
{
	char file[32];

	init();
	ChangeToExecutableDirectory(argc, argv);

	#if _DEBUGPRINT
	uint32_t freeRam = Malloc(-1);
	DebugPrintf("Free RAM: %d\n", freeRam);
	#endif

	if (argc < 2)
	{
		if (!DDB_RunPlayer())
			error(DDB_GetErrorString());
		quit();
		return 1; 
	}

	strncpy(file, argv[1], 32);
	file[31] = 0;

	DDB* ddb = DDB_Load(file);
	if (!ddb)
		error(DDB_GetErrorString());

	DDB_ScreenMode screenMode = DDB_GetDefaultScreenMode(ddb->target);
	uint8_t displayPlanes = 4;
	DDB_CheckDataFileConfig(file, ddb->target, &screenMode, &displayPlanes);
	VID_SetDisplayPlanesHint(displayPlanes);

	if (!VID_Initialize(ddb->target, ddb->version, screenMode))
		error(DDB_GetErrorString());

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == 0)
		error(DDB_GetErrorString());

	if (DDB_SupportsDataFile(ddb->version, ddb->target) && !VID_LoadDataFile(file))
	{
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		error(DDB_GetErrorString());
	}

	DDB_Run(interpreter);
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	
	quit();
	return 1;
}

void OSSyncFS()
{
}

#endif