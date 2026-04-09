#include <dmg.h>
#include <ddb.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_mem.h>

#ifdef _ATARIST

#include <mint/cookie.h>
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

	uint32_t freeRam = Malloc(-1);
	DebugPrintf("Free RAM: %d\n", freeRam);
	if (freeRam > 16384)
	{
		size_t reservedArena = OSReserveArena(freeRam - 2048);
		if (reservedArena != 0)
			DebugPrintf("Reserved OS arena: %lu bytes\n", (unsigned long)reservedArena);
		else
			DebugPrintf("Reserved OS arena: failed\n");
	}

	DMG_GetTemporaryBuffer(ImageMode_PlanarST);

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

	if (!VID_Initialize(ddb->target, ddb->version, ScreenMode_VGA16))
		error(DDB_GetErrorString());

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, ScreenMode_VGA16);
	if (interpreter == 0)
		error(DDB_GetErrorString());

	if (DDB_SupportsDataFile(ddb->version, ddb->target))
		VID_LoadDataFile(file);

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