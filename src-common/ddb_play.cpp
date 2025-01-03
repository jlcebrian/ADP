
#include <ddb.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#define MAX_FILES          64
#define NAME_BUFFER_SIZE 2048

static int         ddbCount = 0;
static int         scrCount = 0;
static int         ddbSelected = 0;
static char*       files[MAX_FILES];
static int         fileCount = 0;
static char*       nameBuffer = 0;
static char        ddbFileName[FILE_MAX_PATH];

static void EnumFiles(const char* pattern = "*")
{
	FindFileResults r;

	fileCount = 0;
	nameBuffer = Allocate<char>("Temporary filenames", NAME_BUFFER_SIZE);
	
	if (File_FindFirst(pattern, &r))
	{
		char* ptr = nameBuffer;
		char* end = nameBuffer + NAME_BUFFER_SIZE;
		do
		{
			if (r.attributes & FileAttribute_Directory)
				continue;
			if (fileCount >= MAX_FILES)
				break;
			files[fileCount] = ptr;
			ptr += StrCopy(files[fileCount], end - ptr, r.fileName);
			fileCount++;
			if (fileCount == MAX_FILES || ptr >= end-1)
				break;
		} while (File_FindNext(&r));
	}

	// Sort files
	for (int n = 0; n < fileCount; n++)
	{
		for (int m = n + 1; m < fileCount; m++)
		{
			if (StrIComp(files[n], files[m]) > 0)
			{
				char* temp = files[n];
				files[n] = files[m];
				files[m] = temp;
			}
		}
	}
}

static void CloseEnum()
{
	if (nameBuffer)
	{
		Free(nameBuffer);
		nameBuffer = 0;
	}
}

static int CountFiles(const char* extension)
{
	int count = 0;
	for (int n = 0; n < fileCount; n++)
	{
		const char* dot = StrRChr(files[n], '.');
		if (dot == 0)
			continue;
		if (StrIComp(dot, extension) == 0)
			count++;
	}
	return count;
}

static const char* GetFile(const char* extension, int index)
{
	int count = 0;
	for (int n = 0; n < fileCount; n++)
	{
		const char* dot = StrRChr(files[n], '.');
		if (dot == 0)
			continue;
		if (StrIComp(dot, extension) == 0)
		{
			if (count == index)
				return files[n];
			count++;
		}
	}
	return "";
}

static void ShowLoaderPrompt(int parts, DDB_Language language)
{
	VID_Clear(92, 80, 136, 32, 0);

	static const char* messageSP[2] = {
	                                  "\x12Qu\x16 parte quieres",
									  "   cargar (1-*)?  ",
	};
	static const char* messageEN[2] = {
	                                  " Which part do you ",
									  "want to load (1-*)?",
	};

	const char** message = (language == DDB_SPANISH) ? messageSP : messageEN;
	for (int y = 0; y < 2; y++)
	{
		for (int x = 0; message[y][x] != 0; x++)
		{
			char c = message[y][x];
			if (c == '*') c = '0' + parts;
			VID_DrawCharacter(106 + x * 6, 88 + y * 8, c, 0x0F, 0);
		}
	}
}

static void LoaderScreenUpdate(int elapsed)
{
	if (VID_AnyKey())
	{
		uint8_t key = 0, ext = 0;
		VID_GetKey(&key, &ext, 0);
		if (key == 27)
			VID_Quit();
		else if (key >= '1' && key <= '0' + ddbCount)
		{
			ddbSelected = key - '1';
			VID_Quit();
		}			
	}
}

static void WaitForKeyUpdate(int elapsed)
{
	if (VID_AnyKey())
	{
		uint8_t key = 0, ext = 0;
		VID_GetKey(&key, &ext, 0);
		VID_Quit();
	}
}

#if _WEB

static int frame;
static uint8_t r[16], g[16], b[16];

static PlayerState state = Player_Starting;
static DDB*        ddb;

static const char* snapshotExtensions[] = {
	".z80",
	".sna",
	".tzx",
	".sta",
	".vsf",
	".tap",
	".cas",
	".bin",
	".rom",
	".raw",
	0
};

static int         snapshotCount = 0;

static int CountSnapshots()
{
	int count = 0;
	for (int n = 0; snapshotExtensions[n]; n++)
		count += CountFiles(snapshotExtensions[n]);
	return count;
}

static const char* GetSnapshot(int index)
{
	int count = 0;
	for (int m = 0; m < fileCount; m++)
	{
		const char* dot = StrRChr(files[m], '.');
		if (dot == 0)
			continue;

		for (int n = 0; snapshotExtensions[n]; n++)
		{
			if (StrIComp(dot, snapshotExtensions[n]) == 0)
			{
				if (count == index)
					return files[m];
				count++;
				break;
			}
		}
	}
	return "";
}

static void FadeOutStep(int elapsed)
{
	for (int i = 0; i < 16; i++)
	{
		uint8_t r2 = r[i] * (15 - frame) / 15;
		uint8_t g2 = g[i] * (15 - frame) / 15;
		uint8_t b2 = b[i] * (15 - frame) / 15;
		VID_SetPaletteColor(i, r2, g2, b2);
	}
	frame++;
	VID_ActivatePalette();
	
	if (frame >= 16)
	{
		VID_ClearBuffer(true);
		VID_ClearBuffer(false);	
		VID_SetDefaultPalette();
		VID_ActivatePalette();
		VID_Quit();
	}
}

static void FadeOut()
{
	for (int i = 0; i < 16; i++)
		VID_GetPaletteColor(i, &r[i], &g[i], &b[i]);
	frame = 0;
	FadeOutStep(0);

	VID_MainLoopAsync(0, FadeOutStep);
}

static const char* PlayerStateToString(PlayerState state)
{
	switch (state)
	{
		case Player_Starting: return "Starting";
		case Player_SelectingPart: return "SelectingPart";
		case Player_ShowingScreen: return "ShowingScreen";
		case Player_FadingOut: return "FadingOut";
		case Player_InGame: return "InGame";
		case Player_Finished: return "Finished";
		case Player_Error: return "Error";
	}
	return "Unknown";
}

void DDB_RestartAsyncPlayer()
{
	if (state == Player_Finished || state == Player_Error)
		state = Player_Starting;
}

PlayerState DDB_RunPlayerAsync(const char* location)
{
	static DDB_ScreenMode screenMode = ScreenMode_VGA16;

	if (state == Player_Finished || state == Player_Error)
		return state;

	if (state == Player_FadingOut)
	{
		VID_ClearBuffer(true);
		VID_ClearBuffer(false);

		DebugPrintf("Starting interpreter\n");
		DDB_Run(interpreter);
		return state = Player_InGame;
	}

	if (state == Player_InGame)
	{
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		VID_Finish();
		return state = Player_Finished;
	}

	if (state == Player_Starting)
	{
		DDB_Machine machine = DDB_MACHINE_AMIGA;
		DDB_Language language = DDB_SPANISH;
		DDB_Version version = DDB_VERSION_2;

		char path[FILE_MAX_PATH];
		StrCopy(path, FILE_MAX_PATH, location);
		StrCat(path, FILE_MAX_PATH, "*");
		
		EnumFiles(path);
		snapshotCount = CountSnapshots();
		ddbCount = CountFiles(".ddb");
		scrCount = CountFiles(".scr");
		const char* scrExtension = ".scr";
		if (scrCount == 0)
		{
			if ((scrCount = CountFiles(".egs")) > 0)
			{
				scrExtension = ".egs";
				screenMode = ScreenMode_EGA;
			}
			else if ((scrCount = CountFiles(".cgs")) > 0)
			{
				scrExtension = ".cgs";
				screenMode = ScreenMode_CGA;
			}
			else if ((scrCount = CountFiles(".vgs")) > 0)
			{
				scrExtension = ".vgs";
				screenMode = ScreenMode_VGA16;
			}
		}
		DebugPrintf("Found %d DDBs, %d snapshots and %d screens\n", ddbCount, snapshotCount, scrCount);
		if (ddbCount == 0 && snapshotCount == 0)
		{
			CloseEnum();
			DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
			return state = Player_Error;
		}

		if (snapshotCount > 0)
		{
			StrCopy(ddbFileName, FILE_MAX_PATH, GetSnapshot(0));
			ddb = DDB_Load(ddbFileName);
			if (!ddb) 
			{
				DebugPrintf("Error loading snapshot from %s: %s\n", ddbFileName, DDB_GetErrorString());
				VID_ShowError(DDB_GetErrorString());
				return state = Player_Error;
			}
			DebugPrintf("Loaded snapshot from %s.\nVersion %s, machine %s\n", ddbFileName, DDB_DescribeVersion(ddb->version), DDB_DescribeMachine(ddb->machine));
			VID_Initialize(ddb->machine, ddb->version);
		}
		else
		{
			StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", 0));
			DDB_Check(ddbFileName, &machine, &language, &version);
			DebugPrintf("Checked %s\n", ddbFileName);
			VID_Initialize(machine, version);
		}
		
		if (scrCount > 0)
		{
			if (machine == DDB_MACHINE_ATARIST && CountFiles(".ch0") == 0)
				machine = DDB_MACHINE_AMIGA;
			if (!VID_DisplaySCRFile(GetFile(scrExtension, 0), machine))
			{
				VID_ShowError(DDB_GetErrorString());
				return state = Player_Error;
			}
			if (ddbCount == 1)
			{
				VID_MainLoopAsync(0, WaitForKeyUpdate);
				return state = Player_ShowingScreen;
			}
		}
		if (ddbCount > 1)
		{
			DebugPrintf("Showing part selector\n");
			ddbSelected = -1;
			VID_SaveScreen();
			ShowLoaderPrompt(ddbCount, language);
			VID_SetTextInputMode(true);
			VID_MainLoopAsync(0, LoaderScreenUpdate);
			return state = Player_SelectingPart;
		}
	}

	VID_SetTextInputMode(false);

	// Start the game

	if (ddbSelected == -1)
	{
		CloseEnum();
		VID_Finish();
		return state = Player_Finished;
	}

	DebugPrintf("Selected part %ld\n", (long)(ddbSelected + 1));
	if (ddbSelected != 0)
		StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", ddbSelected));
	CloseEnum();
	
	DebugPrintf("Loading %s\n", ddbFileName);

	ddb = DDB_Load(ddbFileName);
	if (!ddb)
	{
		DebugPrintf("Error loading %s", ddbFileName);
		DebugPrintf(": %s\n", DDB_GetErrorString());
		return state = Player_Error;
	}
	if (DDB_SupportsDataFile(ddb))
		VID_LoadDataFile(ddbFileName);
	DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == 0)
	{
		DebugPrintf("Error creating interpreter: %s\n", DDB_GetErrorString());
		DDB_Close(ddb);
		VID_Finish();
		return state = Player_Error;
	}
	FadeOut();
	return state = Player_FadingOut;
}

#else

static void FadeOut()
{

	uint8_t r[16], g[16], b[16];
	for (int i = 0; i < 16; i++)
		VID_GetPaletteColor(i, &r[i], &g[i], &b[i]);
	for (int frame = 0; frame < 16; frame++)
	{
		for (int i = 0; i < 16; i++)
		{
			uint8_t r2 = r[i] * (15 - frame) / 15;
			uint8_t g2 = g[i] * (15 - frame) / 15;
			uint8_t b2 = b[i] * (15 - frame) / 15;
			VID_SetPaletteColor(i, r2, g2, b2);
		}
		VID_VSync();
	}
	VID_SetDefaultPalette();
}

bool DDB_RunPlayer()
{
	DDB_Machine machine = DDB_MACHINE_AMIGA;
	DDB_Language language = DDB_SPANISH;
	DDB_ScreenMode screenMode = ScreenMode_VGA16;
	DDB_Version version = DDB_VERSION_2;

	EnumFiles();
	
	ddbCount = CountFiles(".ddb");
	if (ddbCount == 0)
	{
		CloseEnum();
		DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
		return false;
	}

	DebugPrintf("Initializing video\n");

	StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", 0));
	DDB_Check(ddbFileName, &machine, &language, &version);
	VID_Initialize(machine, version);

	scrCount = CountFiles(".scr");
	if (scrCount > 0)
	{
		if (machine == DDB_MACHINE_ATARIST && CountFiles(".ch0") == 0)
			machine = DDB_MACHINE_AMIGA;
		if (!VID_DisplaySCRFile(GetFile(".scr", 0), machine))
			VID_ShowError(DDB_GetErrorString());
	}
	else if ((scrCount = CountFiles(".egs")) > 0)
	{
		screenMode = ScreenMode_EGA;
		if (!VID_DisplaySCRFile(GetFile(".egs", 0), machine))
			VID_ShowError(DDB_GetErrorString());
	}
	else if ((scrCount = CountFiles(".cgs")) > 0)
	{
		screenMode = ScreenMode_CGA;
		if (!VID_DisplaySCRFile(GetFile(".cgs", 0), machine))
			VID_ShowError(DDB_GetErrorString());
	}
	else if ((scrCount = CountFiles(".vgs")) > 0)
	{
		screenMode = ScreenMode_VGA16;
		if (!VID_DisplaySCRFile(GetFile(".vgs", 0), machine))
			VID_ShowError(DDB_GetErrorString());
	}

	if (ddbCount > 1)
	{
		DebugPrintf("Showing part selector\n");
		ddbSelected = -1;
		VID_SaveScreen();
		ShowLoaderPrompt(ddbCount, language);
		VID_MainLoop(0, LoaderScreenUpdate);
		if (exitGame)
			return true;
		VID_RestoreScreen();
		VID_ClearBuffer(false);
		if (ddbSelected == -1)
		{
			CloseEnum();
			VID_Finish();
			return true;
		}
		DebugPrintf("Selected part %ld\n", (long)(ddbSelected + 1));
		if (ddbSelected != 0)
			StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", ddbSelected));
	}

	CloseEnum();
	
	DebugPrintf("Loading %s\n", ddbFileName);

	VID_SaveScreen();

	VID_ShowProgressBar(0);
	DDB* ddb = DDB_Load(ddbFileName);
	if (!ddb)
	{
		DebugPrintf("Error loading %s", ddbFileName);
		DebugPrintf(": %s\n", DDB_GetErrorString());
		return false;
	}
	VID_ShowProgressBar(64);

	if (DDB_SupportsDataFile(ddb))
		VID_LoadDataFile(ddbFileName);

	DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == 0)
	{
		DebugPrintf("Error creating interpreter: %s\n", DDB_GetErrorString());
		DDB_Close(ddb);
		VID_Finish();
		return false;
	}
	VID_ShowProgressBar(255);
	
	if (scrCount > 0)
	{
		VID_RestoreScreen();

		DDB_Interpreter* i = interpreter;
		uint8_t key, ext;
		VID_MainLoop(0, WaitForKeyUpdate);
		VID_GetKey(&key, &ext, 0);
		interpreter = i;
	}
	if (scrCount > 0)
		FadeOut();

	VID_ClearBuffer(true);
	VID_ClearBuffer(false);

	DebugPrintf("Starting interpreter\n");
	DDB_Run(interpreter);
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	VID_Finish();

	return true;
}

#endif