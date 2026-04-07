
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
static uint16_t    fadePaletteSize = 16;

#if HAS_PCX
static bool FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static void BuildFileNameWithExtension(const char* fileName, const char* extension, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, fileName);
	char* dot = (char*)StrRChr(output, '.');
	if (dot == 0)
		dot = output + StrLen(output);
	StrCopy(dot, output + outputSize - dot, extension);
}

static const char* FindPCXIntroScreen(const char* fileName, DDB_Machine machine, DDB_Version version, DDB_ScreenMode* screenMode)
{
	static char introScreen[FILE_MAX_PATH];
	if (machine != DDB_MACHINE_IBMPC || version < DDB_VERSION_2)
		return 0;

	BuildFileNameWithExtension(fileName, ".VGA", introScreen, sizeof(introScreen));
	if (!FileExists(introScreen))
	{
		BuildFileNameWithExtension(fileName, ".vga", introScreen, sizeof(introScreen));
		if (!FileExists(introScreen))
		{
			BuildFileNameWithExtension(fileName, ".PCX", introScreen, sizeof(introScreen));
			if (!FileExists(introScreen))
			{
				BuildFileNameWithExtension(fileName, ".pcx", introScreen, sizeof(introScreen));
				if (!FileExists(introScreen))
					return 0;
			}
		}
	}

	*screenMode = ScreenMode_VGA;
	return introScreen;
}
#endif

static void EnumFiles(const char* pattern = "*")
{
	FindFileResults r;

	fileCount = 0;
	nameBuffer = Allocate<char>("Temporary filenames", NAME_BUFFER_SIZE);
	if (nameBuffer == 0)
		return;
	
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

	const char** message = (language == DDB_SPANISH ? messageSP : messageEN);
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
	(void)elapsed;
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
	(void)elapsed;
	if (VID_AnyKey())
	{
		uint8_t key = 0, ext = 0;
		VID_GetKey(&key, &ext, 0);
		VID_Quit();
	}
}

#if _WEB

static int frame;
static uint8_t r[256], g[256], b[256];

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
	for (uint16_t i = 0; i < fadePaletteSize; i++)
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
		VID_ResetDisplayToDefault();
		VID_Quit();
	}
}

static void FadeOut()
{
	fadePaletteSize = VID_GetPaletteSize();
	if (fadePaletteSize > 256)
		fadePaletteSize = 256;
	for (uint16_t i = 0; i < fadePaletteSize; i++)
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
		#if HAS_PCX
		const char* introScreen = 0;
		#endif
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
			VID_Initialize(ddb->machine, ddb->version, screenMode);
		}
		else
		{
			StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", 0));
			DDB_Check(ddbFileName, &machine, &language, &version);
			uint8_t displayPlanes = 4;
			DDB_CheckDataFileConfig(ddbFileName, &screenMode, &displayPlanes);
			VID_SetDisplayPlanesHint(displayPlanes);
			#if HAS_PCX
			introScreen = FindPCXIntroScreen(ddbFileName, machine, version, &screenMode);
			if (introScreen != 0)
				scrCount = 1;
			#endif
			DebugPrintf("Checked %s\n", ddbFileName);
			VID_Initialize(machine, version, screenMode);
            if (DDB_SupportsDataFile(version, machine) && !VID_LoadDataFile(ddbFileName))
				DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
		}
		
		if (scrCount > 0)
		{
			if (machine == DDB_MACHINE_ATARIST && CountFiles(".ch0") == 0)
				machine = DDB_MACHINE_AMIGA;
			#if HAS_PCX
			const char* screenFile = introScreen != 0 ? introScreen : GetFile(scrExtension, 0);
			#else
			const char* screenFile = GetFile(scrExtension, 0);
			#endif
			if (!VID_DisplaySCRFile(screenFile, machine))
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
	if (DDB_SupportsDataFile(ddb->version, ddb->target) && !VID_LoadDataFile(ddbFileName))
		DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
	#if HAS_PCX
	if (VID_HasExternalPictures())
		screenMode = ScreenMode_VGA;
	#endif
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
	uint16_t fadePaletteSize = VID_GetPaletteSize();
	if (fadePaletteSize > 256)
		fadePaletteSize = 256;
	uint8_t r[256], g[256], b[256];
	for (uint16_t i = 0; i < fadePaletteSize; i++)
		VID_GetPaletteColor(i, &r[i], &g[i], &b[i]);
	for (int frame = 0; frame < 16; frame++)
	{
		for (uint16_t i = 0; i < fadePaletteSize; i++)
		{
			uint8_t r2 = r[i] * (15 - frame) / 15;
			uint8_t g2 = g[i] * (15 - frame) / 15;
			uint8_t b2 = b[i] * (15 - frame) / 15;
			VID_SetPaletteColor(i, r2, g2, b2);
		}
		VID_VSync();
	}
	VID_ResetDisplayToDefault();
}

// Checks for intro screen files and, if found, updates machine and screenMode accordingly
#if HAS_PCX
static void CheckIntroScreenFiles(const char* ddbFileName, const char** introScreen, DDB_Machine* machine, DDB_Version version, DDB_ScreenMode* screenMode)
#else
static void CheckIntroScreenFiles(const char** introScreen, DDB_Machine* machine, DDB_ScreenMode* screenMode)
#endif
{
	scrCount = CountFiles(".scr");
	if (scrCount > 0)
	{
		if (*machine == DDB_MACHINE_ATARIST && CountFiles(".ch0") == 0)
			*machine = DDB_MACHINE_AMIGA;
        *introScreen = GetFile(".scr", 0);
	}
	else if ((scrCount = CountFiles(".egs")) > 0)
	{
		*screenMode = ScreenMode_EGA;
		*introScreen = GetFile(".egs", 0);
	}
	else if ((scrCount = CountFiles(".cgs")) > 0)
	{
		*screenMode = ScreenMode_CGA;
		*introScreen = GetFile(".cgs", 0);
	}
	#if HAS_PCX
	else if ((*introScreen = FindPCXIntroScreen(ddbFileName, *machine, version, screenMode)) != 0)
	{
		scrCount = 1;
	}
	#endif
	else if ((scrCount = CountFiles(".vgs")) > 0)
	{
		*screenMode = ScreenMode_VGA16;
		*introScreen = GetFile(".vgs", 0);
	}
}

bool DDB_RunPlayer()
{
	DDB_Machine machine = DDB_MACHINE_AMIGA;
	DDB_Language language = DDB_SPANISH;
	DDB_ScreenMode screenMode = ScreenMode_VGA16;
	DDB_Version version = DDB_VERSION_2;
    const char* introScreen = 0;

	EnumFiles();
	
	ddbCount = CountFiles(".ddb");
	if (ddbCount == 0)
	{
		CloseEnum();
		DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
		return false;
	}

	StrCopy(ddbFileName, FILE_MAX_PATH, GetFile(".ddb", 0));
	DDB_Check(ddbFileName, &machine, &language, &version);
	uint8_t displayPlanes = 4;
	DDB_CheckDataFileConfig(ddbFileName, &screenMode, &displayPlanes);
	VID_SetDisplayPlanesHint(displayPlanes);

	#if HAS_PCX
	CheckIntroScreenFiles(ddbFileName, &introScreen, &machine, version, &screenMode);
	#else
	CheckIntroScreenFiles(&introScreen, &machine, &screenMode);
	#endif
	VID_Initialize(machine, version, screenMode);
    if (introScreen != 0)
    {
        if (!VID_DisplaySCRFile(introScreen, machine))
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
	uint32_t tLoadStart = 0;
	VID_GetMilliseconds(&tLoadStart);
	DDB* ddb = DDB_Load(ddbFileName);
	if (!ddb)
	{
		DebugPrintf("Error loading %s", ddbFileName);
		DebugPrintf(": %s\n", DDB_GetErrorString());
		return false;
	}
	uint32_t tAfterDDBLoad = 0;
	VID_GetMilliseconds(&tAfterDDBLoad);
	DebugPrintf("DDB_Load completed in %lu ms\n", (unsigned long)(tAfterDDBLoad - tLoadStart));
	VID_ShowProgressBar(64);

	if (DDB_SupportsDataFile(ddb->version, ddb->target) && !VID_LoadDataFile(ddbFileName)) 
	{
		DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
	}

	#if HAS_PCX
	if (VID_HasExternalPictures())
		screenMode = ScreenMode_VGA;
	#endif

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
		VID_MainLoop(0, WaitForKeyUpdate);
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
