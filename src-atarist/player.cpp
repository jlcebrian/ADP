#include <dmg.h>
#include <ddb.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_mem.h>

#ifdef _ATARIST

#include <mint/sysvars.h>
#include <osbind.h>

#include <stdio.h>
#include <sys/dirent.h>
#include <string.h>
#include <gem.h>

static uint32_t ret;
static uint16_t defaultPalette[16];

static void init()
{
	ret = Super(0L);
	for (int n = 0; n < 16; n++)
		defaultPalette[n] = Setcolor(n, -1);

#ifndef NO_SAMPLES
	// Disable key click, keep key repeat
	*conterm = 2;
#endif
}

static void quit()
{
	Vsync();
	memset(Physbase(), 0, 32000);
	for (int n = 0; n < 16; n++)
		Setcolor(n, defaultPalette[n]);

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

	if (Getrez())
	{
		appl_init();
		form_alert(1, "[4][Solo modo grafico][Ok]");
		appl_exit();
		Pterm(0);
	}

	init();

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

	if (!VID_Initialize(ddb->target, ddb->version))
		error(DDB_GetErrorString());

	if (DDB_SupportsDataFile(ddb))
		VID_LoadDataFile(file);

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, ScreenMode_VGA16);
	if (interpreter == 0)
		error(DDB_GetErrorString());

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