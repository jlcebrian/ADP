
#include <ddb.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <dmg.h>
#include <os_char.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#ifdef _ATARIST
#include <mint/cookie.h>
#endif

#define MAX_FILES          64
#define NAME_BUFFER_SIZE 2048

static int         ddbCount = 0;
static int         scrCount = 0;
static int         ddbSelected = 0;
static char*       files[MAX_FILES];
#ifdef HAS_VIRTUALFILESYSTEM
static int         ddbFiles[MAX_FILES];
static int         ddbFileCount = -1;
#endif
static int         fileCount = 0;
static char*       nameBuffer = 0;
static char        ddbFileName[FILE_MAX_PATH];
static char        introScreenName[FILE_MAX_PATH];
static char        startupPattern[FILE_MAX_PATH];

struct StartupConfig
{
	int  parts;
	int  disks;
	int  boxX;
	int  boxY;
	int  ink;
	char windowTitle[128];
	char windowIcon[FILE_MAX_PATH];
	bool hasBoxX;
	bool hasBoxY;
	bool hasInk;
	bool hasWindowTitle;
	bool hasWindowIcon;
};

enum LoaderPromptMode
{
	LoaderPrompt_None,
	LoaderPrompt_Part,
	LoaderPrompt_Disk,
};

static StartupConfig    startupConfig;
static LoaderPromptMode loaderPromptMode = LoaderPrompt_None;
static int              diskPromptNumber = 0;
static bool             diskPromptConfirmed = false;
static char             diskImages[MAX_FILES][FILE_MAX_PATH];
static int              diskImageCount = 0;
static int              currentDiskNumber = 0;

#ifdef HAS_VIRTUALFILESYSTEM
static void IgnoreDDBWarning(const char* message)
{
	(void)message;
}
#endif

static void CloseEnum();
static void EnumFiles(const char* pattern = "*");
static int CountFiles(const char* extension);
static int CountDDBFiles();
static const char* GetDDBFile(int index);
static bool GetDDBMetadata(const char* fileName, DDB_Machine* machine, DDB_Language* language, DDB_Version* version);
static void LoaderScreenUpdate(int elapsed);
static void WaitForKeyUpdate(int elapsed);

static void ResetStartupConfig();
static void LoadStartupConfig();
static void CollectDiskImages();
static int GetConfiguredPartCount();
static void ShowLoaderPrompt(int parts, DDB_Language language);
static void ShowDiskPrompt(int diskNumber, DDB_Language language);
static const char* ResolveSelectedDDBFile(int partIndex);
static bool EnsureSelectedPartMediaSync(int partIndex, DDB_Language language);

#ifdef _WEB
static bool EnsureSelectedPartMediaAsync(int partIndex, DDB_Language language);
#endif

#ifdef _AMIGA
extern bool VID_IsAGAAvailable();
extern void OpenKeyboard();
extern bool OpenAudio();
#endif

static bool FileExistsByName(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static void SetStartupPattern(const char* pattern)
{
	StrCopy(startupPattern, sizeof(startupPattern), pattern);
}

static bool StartupIsDiskMounted()
{
#ifdef HAS_VIRTUALFILESYSTEM
	return File_IsDiskMounted();
#else
	return false;
#endif
}

static bool StartupMountDisk(const char* fileName)
{
#ifdef HAS_VIRTUALFILESYSTEM
	return File_MountDisk(fileName);
#else
	(void)fileName;
	return false;
#endif
}

static void StartupUnmountDisk()
{
#ifdef HAS_VIRTUALFILESYSTEM
	File_UnmountDisk();
#endif
}

static const char* GetLastPathSeparator(const char* path)
{
	const char* slash = StrRChr(path, '/');
	const char* backslash = StrRChr(path, '\\');
	if (slash == 0)
		return backslash;
	if (backslash == 0)
		return slash;
	return slash > backslash ? slash : backslash;
}

static void CopyConfigString(char* output, size_t outputSize, const char* value)
{
	while (IsSpace(*value))
		value++;

	const char* end = value + StrLen(value);
	while (end > value && IsSpace((uint8_t)end[-1]))
		end--;

	if (end > value + 1 && ((value[0] == '"' && end[-1] == '"') || (value[0] == '\'' && end[-1] == '\'')))
	{
		value++;
		end--;
	}

	size_t length = (size_t)(end - value);
	if (length >= outputSize)
		length = outputSize - 1;
	if (length > 0)
		MemCopy(output, value, length);
	output[length] = 0;
}

static void ResolveConfigAdjacentPath(const char* cfgFile, const char* value, char* output, size_t outputSize)
{
	if (outputSize == 0)
		return;

	CopyConfigString(output, outputSize, value);
	if (output[0] == 0)
		return;

	if (GetLastPathSeparator(output) != 0)
		return;

	const char* separator = GetLastPathSeparator(cfgFile);
	if (separator == 0)
		return;

	char resolved[FILE_MAX_PATH];
	size_t prefixLength = (size_t)(separator - cfgFile + 1);
	if (prefixLength >= sizeof(resolved))
		prefixLength = sizeof(resolved) - 1;
	MemCopy(resolved, cfgFile, prefixLength);
	resolved[prefixLength] = 0;
	StrCat(resolved, sizeof(resolved), output);
	StrCopy(output, outputSize, resolved);
}

static void RescanCurrentFiles()
{
	if (StartupIsDiskMounted())
		EnumFiles();
	else
		EnumFiles(startupPattern[0] == 0 ? "*" : startupPattern);
}

static bool ParseConfigInt(const char* text, int* value)
{
	int parsed = 0;
	bool hasDigits = false;
	while (IsSpace(*text))
		text++;
	if (*text == 0)
		return false;
	while (*text >= '0' && *text <= '9')
	{
		hasDigits = true;
		parsed = parsed * 10 + (*text - '0');
		text++;
	}
	while (IsSpace(*text))
		text++;
	if (!hasDigits || *text != 0)
		return false;
	*value = parsed;
	return true;
}

static void ResetStartupConfig()
{
	MemClear(&startupConfig, sizeof(startupConfig));
	startupConfig.parts = 0;
	startupConfig.disks = 0;
	startupConfig.ink = 0x0F;
	VID_SetWindowTitle(0);
	VID_SetWindowIcon(0);
	loaderPromptMode = LoaderPrompt_None;
	diskPromptNumber = 0;
	diskPromptConfirmed = false;
	diskImageCount = 0;
	currentDiskNumber = 0;
	startupPattern[0] = 0;
	for (int i = 0; i < MAX_FILES; i++)
		diskImages[i][0] = 0;
}

static void ApplyConfigEntry(const char* cfgFile, const char* key, const char* value)
{
	int parsed = 0;
	if (StrIComp(key, "TITLE") == 0 || StrIComp(key, "WINDOWTITLE") == 0)
	{
		CopyConfigString(startupConfig.windowTitle, sizeof(startupConfig.windowTitle), value);
		startupConfig.hasWindowTitle = startupConfig.windowTitle[0] != 0;
		return;
	}
	if (StrIComp(key, "ICON") == 0 || StrIComp(key, "WINDOWICON") == 0)
	{
		ResolveConfigAdjacentPath(cfgFile, value, startupConfig.windowIcon, sizeof(startupConfig.windowIcon));
		startupConfig.hasWindowIcon = startupConfig.windowIcon[0] != 0;
		return;
	}

	if (!ParseConfigInt(value, &parsed))
		return;

	if (StrIComp(key, "PARTS") == 0 && parsed > 0)
		startupConfig.parts = parsed;
	else if (StrIComp(key, "DISKS") == 0 && parsed > 0)
		startupConfig.disks = parsed;
	else if (StrIComp(key, "SCRBOXX") == 0)
	{
		startupConfig.boxX = parsed;
		startupConfig.hasBoxX = true;
	}
	else if (StrIComp(key, "SCRBOXY") == 0)
	{
		startupConfig.boxY = parsed;
		startupConfig.hasBoxY = true;
	}
	else if (StrIComp(key, "SCRINK") == 0)
	{
		startupConfig.ink = parsed & 0xFF;
		startupConfig.hasInk = true;
	}
}

static void LoadStartupConfig()
{
	const char* cfgFile = 0;
	for (int i = 0; i < fileCount; i++)
	{
		const char* dot = StrRChr(files[i], '.');
		if (dot != 0 && StrIComp(dot, ".cfg") == 0)
		{
			cfgFile = files[i];
			break;
		}
	}
	if (cfgFile == 0)
		return;

	File* file = File_Open(cfgFile, ReadOnly);
	if (file == 0)
		return;

	uint64_t size = File_GetSize(file);
	if (size == 0 || size > 8192)
	{
		File_Close(file);
		return;
	}

	char* buffer = Allocate<char>("startup cfg", (uint32_t)size + 1);
	if (buffer == 0)
	{
		File_Close(file);
		return;
	}

	if (File_Read(file, buffer, size) != size)
	{
		Free(buffer);
		File_Close(file);
		return;
	}
	File_Close(file);
	buffer[size] = 0;

	char* line = buffer;
	while (*line != 0)
	{
		char* next = line;
		while (*next != 0 && *next != '\n' && *next != '\r')
			next++;
		char saved = *next;
		*next = 0;

		char* start = line;
		while (IsSpace(*start))
			start++;
		if (*start != 0 && *start != ';' && *start != '#' && *start != '[')
		{
			char* end = start + StrLen(start);
			while (end > start && IsSpace((uint8_t)end[-1]))
				*--end = 0;
			char* equals = (char*)StrRChr(start, '=');
			if (equals != 0)
			{
				char* value = equals + 1;
				*equals = 0;
				char* keyEnd = equals;
				while (keyEnd > start && IsSpace((uint8_t)keyEnd[-1]))
					*--keyEnd = 0;
				while (IsSpace(*value))
					value++;
				ApplyConfigEntry(cfgFile, start, value);
			}
		}

		*next = saved;
		line = next;
		while (*line == '\n' || *line == '\r')
			line++;
	}

	VID_SetWindowTitle(startupConfig.hasWindowTitle ? startupConfig.windowTitle : 0);
	VID_SetWindowIcon(startupConfig.hasWindowIcon ? startupConfig.windowIcon : 0);

	DebugPrintf("Loaded startup CFG %s (parts=%d disks=%d boxX=%d%s boxY=%d%s ink=%d%s title=%s icon=%s)\n",
		cfgFile,
		startupConfig.parts,
		startupConfig.disks,
		startupConfig.boxX,
		startupConfig.hasBoxX ? "" : " default",
		startupConfig.boxY,
		startupConfig.hasBoxY ? "" : " default",
		startupConfig.ink,
		startupConfig.hasInk ? "" : " default",
		startupConfig.hasWindowTitle ? startupConfig.windowTitle : "default",
		startupConfig.hasWindowIcon ? startupConfig.windowIcon : "default");

	Free(buffer);
}

static void CollectDiskImages()
{
	diskImageCount = 0;
	for (int i = 0; i < fileCount && diskImageCount < MAX_FILES; i++)
	{
		const char* dot = StrRChr(files[i], '.');
		if (dot == 0)
			continue;
		if (StrIComp(dot, ".adf") != 0 && StrIComp(dot, ".dsk") != 0 && StrIComp(dot, ".st") != 0)
			continue;
		StrCopy(diskImages[diskImageCount], FILE_MAX_PATH, files[i]);
		diskImageCount++;
	}
	DebugPrintf("Collected %d disk image candidate(s)\n", diskImageCount);
}

static int GetConfiguredPartCount()
{
	return startupConfig.parts > 0 ? startupConfig.parts : ddbCount;
}

static int GetTargetDiskForPart(int partIndex)
{
	if (startupConfig.disks <= 0)
		return 0;
	int disk = partIndex + 1;
	return disk > 0 && disk <= startupConfig.disks ? disk : 0;
}

#ifdef HAS_VIRTUALFILESYSTEM
static int GetDiskNumberForImage(const char* path)
{
	for (int i = 0; i < diskImageCount; i++)
	{
		if (StrIComp(diskImages[i], path) == 0)
			return i + 1;
	}
	return 0;
}
#endif

static bool MountDiskByNumber(int diskNumber)
{
	if (diskNumber <= 0 || diskNumber > diskImageCount)
		return false;
	if (StartupIsDiskMounted() && currentDiskNumber == diskNumber)
		return true;

	if (StartupIsDiskMounted())
		StartupUnmountDisk();

	if (!StartupMountDisk(diskImages[diskNumber - 1]))
	{
		currentDiskNumber = 0;
		RescanCurrentFiles();
		return false;
	}

	currentDiskNumber = diskNumber;
	EnumFiles();
	return true;
}

static bool HasLocalDataForDDB(const char* ddbFile, DDB_Machine machine, DDB_Version version)
{
	if (ddbFile == 0 || ddbFile[0] == 0)
		return false;
	if (!DDB_SupportsDataFile(version, machine))
		return true;
	return FileExistsByName(ChangeExtension(ddbFile, ".dat")) ||
		FileExistsByName(ChangeExtension(ddbFile, ".DAT")) ||
		FileExistsByName(ChangeExtension(ddbFile, ".ega")) ||
		FileExistsByName(ChangeExtension(ddbFile, ".cga"));
}

static const char* ResolveSelectedDDBFile(int partIndex)
{
	if (partIndex < 0)
		return "";
	if (startupConfig.disks > 0 && StartupIsDiskMounted())
	{
		int targetDisk = GetTargetDiskForPart(partIndex);
		if (targetDisk == 0 || currentDiskNumber != targetDisk || CountDDBFiles() == 0)
			return "";
		return GetDDBFile(0);
	}
	if (partIndex >= CountDDBFiles())
		return "";
	return GetDDBFile(partIndex);
}

static bool SelectedPartNeedsDisk(int partIndex, DDB_Machine machine, DDB_Version version)
{
	if (startupConfig.disks <= 0)
		return false;
	const char* ddbFile = ResolveSelectedDDBFile(partIndex);
	if (ddbFile[0] == 0)
		return true;
	return !HasLocalDataForDDB(ddbFile, machine, version);
}

static bool ShowAndProcessDiskPrompt(int diskNumber, DDB_Language language)
{
	ShowDiskPrompt(diskNumber, language);
	VID_MainLoop(0, WaitForKeyUpdate);
	if (diskImageCount > 0 && diskNumber <= diskImageCount)
		return MountDiskByNumber(diskNumber);
	RescanCurrentFiles();
	return true;
}

static bool EnsureSelectedPartMediaSync(int partIndex, DDB_Language language)
{
	DDB_Machine machine = DDB_MACHINE_AMIGA;
	DDB_Language unusedLanguage = language;
	DDB_Version version = DDB_VERSION_2;
	const char* ddbFile = ResolveSelectedDDBFile(partIndex);
	if (ddbFile[0] != 0)
		GetDDBMetadata(ddbFile, &machine, &unusedLanguage, &version);
	if (!SelectedPartNeedsDisk(partIndex, machine, version))
		return true;

	int targetDisk = GetTargetDiskForPart(partIndex);
	if (targetDisk == 0)
		return false;
	if (!ShowAndProcessDiskPrompt(targetDisk, language))
		return false;

	ddbCount = CountDDBFiles();
	ddbFile = ResolveSelectedDDBFile(partIndex);
	if (ddbFile[0] == 0)
		return false;
	GetDDBMetadata(ddbFile, &machine, &unusedLanguage, &version);
	return HasLocalDataForDDB(ddbFile, machine, version);
}

#ifdef _WEB
static bool EnsureSelectedPartMediaAsync(int partIndex, DDB_Language language)
{
	DDB_Machine machine = DDB_MACHINE_AMIGA;
	DDB_Language unusedLanguage = language;
	DDB_Version version = DDB_VERSION_2;
	const char* ddbFile = ResolveSelectedDDBFile(partIndex);
	if (ddbFile[0] != 0)
		GetDDBMetadata(ddbFile, &machine, &unusedLanguage, &version);
	if (!SelectedPartNeedsDisk(partIndex, machine, version))
		return true;

	int targetDisk = GetTargetDiskForPart(partIndex);
	if (targetDisk == 0)
		return false;
	if (!diskPromptConfirmed)
	{
		loaderPromptMode = LoaderPrompt_Disk;
		diskPromptNumber = targetDisk;
		ShowDiskPrompt(targetDisk, language);
		VID_SetTextInputMode(true);
		VID_MainLoopAsync(0, LoaderScreenUpdate);
		return false;
	}

	diskPromptConfirmed = false;
	loaderPromptMode = LoaderPrompt_None;
	if (diskImageCount > 0 && targetDisk <= diskImageCount)
	{
		if (!MountDiskByNumber(targetDisk))
			return false;
	}
	else
	{
		RescanCurrentFiles();
	}

	ddbCount = CountDDBFiles();
	ddbFile = ResolveSelectedDDBFile(partIndex);
	if (ddbFile[0] == 0)
		return false;
	GetDDBMetadata(ddbFile, &machine, &unusedLanguage, &version);
	return HasLocalDataForDDB(ddbFile, machine, version);
}
#endif

static uint16_t Read16BE(const uint8_t* ptr)
{
	return (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
}

static bool ProbeDAT5Header(const char* fileName, uint16_t* width, uint16_t* height, uint8_t* colorMode)
{
	File* dat = File_Open(ChangeExtension(fileName, ".dat"), ReadOnly);
	if (dat == 0)
		return false;

	uint8_t header[16];
	bool ok = File_Read(dat, header, sizeof(header)) == sizeof(header);
	File_Close(dat);
	if (!ok)
		return false;

	if (!(header[0] == 'D' && header[1] == 'A' && header[2] == 'T' && header[3] == 0 &&
		header[4] == 0 && header[5] == 5))
		return false;

	if (width) *width = Read16BE(header + 0x06);
	if (height) *height = Read16BE(header + 0x08);
	if (colorMode) *colorMode = header[0x0E];
	return true;
}

static bool ValidateResolvedVideoConfig(const char* fileName, DDB_Machine machine, DDB_ScreenMode screenMode, uint8_t planes)
{
#ifdef _AMIGA
	if (planes >= 8 && !VID_IsAGAAvailable())
	{
		DebugPrintf("Rejecting DAT5 Planar8 during initial probe on non-AGA machine\n");
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
#endif

#ifdef _ATARIST
	if (machine == DDB_MACHINE_ATARIST)
	{
		long cookieValue = 0;
		bool hasFalconVideo = Getcookie(C__VDO, &cookieValue) == C_FOUND && cookieValue >= 0x00030000L;
		uint16_t width = 0;
		uint16_t height = 0;
		uint8_t colorMode = 0;
		if (ProbeDAT5Header(fileName, &width, &height, &colorMode))
		{
			bool supported = false;
			if (width == 320 && height == 200)
			{
				if (colorMode == DMG_DAT5_COLORMODE_PLANAR4ST && planes == 4 && screenMode == ScreenMode_VGA16)
					supported = true;
				else if (colorMode == DMG_DAT5_COLORMODE_PLANAR8ST && hasFalconVideo && planes == 8 && screenMode == ScreenMode_VGA)
					supported = true;
			}

			if (!supported)
			{
				DebugPrintf("Rejecting unsupported DAT5 during initial ST probe (mode=%u size=%ux%u planes=%u screen=%u)\n",
					(unsigned)colorMode,
					(unsigned)width,
					(unsigned)height,
					(unsigned)planes,
					(unsigned)screenMode);
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
		}
	}
#endif

	(void)fileName;
	(void)machine;
	(void)screenMode;
	(void)planes;
	return true;
}

#if HAS_PCX
static uint16_t Read16LE(const uint8_t* ptr)
{
	return (uint16_t)(((uint16_t)ptr[1] << 8) | ptr[0]);
}

static void BuildFileNameWithExtension(const char* fileName, const char* extension, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, fileName);
	char* dot = (char*)StrRChr(output, '.');
	if (dot == 0)
		dot = output + StrLen(output);
	StrCopy(dot, output + outputSize - dot, extension);
}

static bool HasPCXHeader(const char* fileName, DDB_ScreenMode* screenMode)
{
	uint8_t header[128];
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;

	bool ok = File_Read(file, header, sizeof(header)) == sizeof(header) &&
		header[0] == 0x0A && header[2] == 1 && header[3] == 8 && header[65] == 1;
	File_Close(file);
	if (!ok)
		return false;

	if (screenMode != 0)
	{
		int width = (int)(Read16LE(header + 8) - Read16LE(header + 4) + 1);
		int height = (int)(Read16LE(header + 10) - Read16LE(header + 6) + 1);
		if (width == 640 && height == 400)
			*screenMode = ScreenMode_SHiRes;
		else if (width == 640 && height == 200)
			*screenMode = ScreenMode_HiRes;
		else
			*screenMode = ScreenMode_VGA;
	}

	return true;
}

static const char* FindPCXIntroScreen(const char* fileName, DDB_Machine machine, DDB_Version version, DDB_ScreenMode* screenMode)
{
	static char introScreen[FILE_MAX_PATH];
	if (machine != DDB_MACHINE_IBMPC || version < DDB_VERSION_2)
		return 0;

	BuildFileNameWithExtension(fileName, ".VGA", introScreen, sizeof(introScreen));
	if (!FileExistsByName(introScreen))
	{
		BuildFileNameWithExtension(fileName, ".vga", introScreen, sizeof(introScreen));
		if (!FileExistsByName(introScreen))
		{
			BuildFileNameWithExtension(fileName, ".PCX", introScreen, sizeof(introScreen));
			if (!FileExistsByName(introScreen))
			{
				BuildFileNameWithExtension(fileName, ".pcx", introScreen, sizeof(introScreen));
				if (!FileExistsByName(introScreen))
					return 0;
			}
		}
	}

	if (HasPCXHeader(introScreen, screenMode))
		return introScreen;

	*screenMode = ScreenMode_VGA;
	return introScreen;
}
#endif

static bool HasPI1Header(const char* fileName)
{
	uint8_t header[2];
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;

	uint64_t size = File_GetSize(file);
	bool ok = size == 32034 && File_Read(file, header, sizeof(header)) == sizeof(header);
	File_Close(file);
	return ok && header[0] == 0x00 && header[1] == 0x00;
}

static bool ShouldPreferAmigaSCR(const char* fileName, DDB_Machine machine, DDB_ScreenMode* screenMode)
{
	if (fileName == 0 || fileName[0] == 0)
		return false;
	if (machine != DDB_MACHINE_ATARIST)
		return false;
	if (CountFiles(".ch0") > 0 || CountFiles(".chr") > 0)
		return false;
	if (HasPI1Header(fileName))
		return false;
	#if HAS_PCX
	if (HasPCXHeader(fileName, screenMode))
		return false;
	#endif
	return true;
}

static bool ResolveDDBVideoConfig(const char* fileName, DDB_Machine* machine, DDB_Language* language, DDB_Version* version, DDB_ScreenMode* screenMode, uint8_t* displayPlanes)
{
	DDB_Machine resolvedMachine = DDB_MACHINE_AMIGA;
	DDB_Language resolvedLanguage = DDB_SPANISH;
	DDB_Version resolvedVersion = DDB_VERSION_2;
	if (!GetDDBMetadata(fileName, &resolvedMachine, &resolvedLanguage, &resolvedVersion))
		return false;

	if (machine)
		*machine = resolvedMachine;
	if (language)
		*language = resolvedLanguage;
	if (version)
		*version = resolvedVersion;

	DDB_ScreenMode resolvedScreenMode = DDB_GetDefaultScreenMode(resolvedMachine);
	uint8_t resolvedPlanes = 4;

	DDB_CheckDataFileConfig(fileName, resolvedMachine, &resolvedScreenMode, &resolvedPlanes);
	if (!ValidateResolvedVideoConfig(fileName, resolvedMachine, resolvedScreenMode, resolvedPlanes))
		return false;

	if (screenMode)
		*screenMode = resolvedScreenMode;
	if (displayPlanes)
		*displayPlanes = resolvedPlanes;

	return true;
}

static void EnumFiles(const char* pattern)
{
	FindFileResults r;

	CloseEnum();
	fileCount = 0;
#ifdef HAS_VIRTUALFILESYSTEM
	ddbFileCount = -1;
#endif
	nameBuffer = Allocate<char>("EnumFiles", NAME_BUFFER_SIZE);
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

#ifdef HAS_VIRTUALFILESYSTEM
static void DetectDDBFiles()
{
	if (ddbFileCount >= 0)
		return;

	ddbFileCount = 0;
	int namedDDBCount = CountFiles(".ddb");
	DDB_SetWarningHandler(IgnoreDDBWarning);
	for (int n = 0; n < fileCount && ddbFileCount < MAX_FILES; n++)
	{
		if (namedDDBCount > 0)
		{
			const char* dot = StrRChr(files[n], '.');
			if (dot == 0 || StrIComp(dot, ".ddb") != 0)
				continue;
		}
		if (DDB_Check(files[n], 0, 0, 0))
		{
			ddbFiles[ddbFileCount++] = n;
		}
	}
	DDB_SetWarningHandler(0);
	DebugPrintf("Detected %d DDB candidates by content\n", ddbFileCount);
}
#endif

static void CloseEnum()
{
	if (nameBuffer)
	{
		Free(nameBuffer);
		nameBuffer = 0;
	}
	#ifdef HAS_VIRTUALFILESYSTEM
	ddbFileCount = -1;
	#endif
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

static bool TryMountDiskImageWithDDBs()
{
#ifdef HAS_VIRTUALFILESYSTEM
	char* candidates = Allocate<char>("Mount candidates", MAX_FILES * FILE_MAX_PATH);
	if (candidates == 0)
		return false;
	int candidateCount = fileCount;
	if (candidateCount > MAX_FILES)
		candidateCount = MAX_FILES;

	for (int n = 0; n < candidateCount; n++)
		StrCopy(candidates + n * FILE_MAX_PATH, FILE_MAX_PATH, files[n]);

	for (int n = 0; n < candidateCount; n++)
	{
		if (!File_MountDisk(candidates + n * FILE_MAX_PATH))
			continue;

		currentDiskNumber = GetDiskNumberForImage(candidates + n * FILE_MAX_PATH);

		EnumFiles();
		if (CountDDBFiles() > 0)
		{
			Free(candidates);
			return true;
		}

		CloseEnum();
		File_UnmountDisk();
		currentDiskNumber = 0;
	}
	Free(candidates);
#endif
	return false;
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

static int CountDDBFiles()
{
	#ifdef HAS_VIRTUALFILESYSTEM
	DetectDDBFiles();
	return ddbFileCount;
	#else
	return CountFiles(".ddb");
	#endif
}

static const char* GetDDBFile(int index)
{
	#ifdef HAS_VIRTUALFILESYSTEM
	DetectDDBFiles();
	if (index < 0 || index >= ddbFileCount)
		return "";
	return files[ddbFiles[index]];
	#else
	return GetFile(".ddb", index);
	#endif
}

static bool GetDDBMetadata(const char* fileName, DDB_Machine* machine, DDB_Language* language, DDB_Version* version)
{
	return DDB_Check(fileName, machine, language, version);
}

static void DrawPromptBox(const char* const* lines, int lineCount, uint8_t ink)
{
	int maxChars = 0;
	for (int y = 0; y < lineCount; y++)
	{
		int chars = (int)StrLen(lines[y]);
		if (chars > maxChars)
			maxChars = chars;
	}

	int promptWidth = maxChars * columnWidth;
	int promptHeight = lineCount * lineHeight;
	int promptX = startupConfig.hasBoxX ? startupConfig.boxX : (screenWidth - promptWidth) / 2;
	int promptY = startupConfig.hasBoxY ? startupConfig.boxY : (screenHeight - promptHeight) / 2;
	VID_Clear(promptX - columnWidth, promptY - lineHeight,
		promptWidth + columnWidth * 2, promptHeight + lineHeight * 2, 0);

	for (int y = 0; y < lineCount; y++)
	{
		for (int x = 0; lines[y][x] != 0; x++)
		{
			VID_DrawCharacter(promptX + x * columnWidth, promptY + y * lineHeight, lines[y][x], ink, 0);
		}
	}
}

static void BuildPromptLine(char* output, size_t outputSize, const char* templateText, int value)
{
	char digits[16];
	LongToChar(value, digits, 10);
	output[0] = 0;
	for (int i = 0; templateText[i] != 0; i++)
	{
		if (templateText[i] == '*')
			StrCat(output, outputSize, digits);
		else
		{
			char text[2] = { templateText[i], 0 };
			StrCat(output, outputSize, text);
		}
	}
}

static void ShowLoaderPrompt(int parts, DDB_Language language)
{
	char line0[64];
	char line1[64];
	const char* lines[2];
	if (language == DDB_SPANISH)
	{
		StrCopy(line0, sizeof(line0), "\x12Qu\x16 parte quieres");
		BuildPromptLine(line1, sizeof(line1), "   cargar (1-*)?  ", parts);
	}
	else
	{
		StrCopy(line0, sizeof(line0), " Which part do you ");
		BuildPromptLine(line1, sizeof(line1), "want to load (1-*)?", parts);
	}
	lines[0] = line0;
	lines[1] = line1;
	DrawPromptBox(lines, 2, (uint8_t)startupConfig.ink);
}

static void ShowDiskPrompt(int diskNumber, DDB_Language language)
{
	char line0[64];
	char line1[64];
	const char* lines[2];
	if (language == DDB_SPANISH)
	{
		BuildPromptLine(line0, sizeof(line0), " Inserta el disco * ", diskNumber);
		StrCopy(line1, sizeof(line1), " y pulsa una tecla  ");
	}
	else
	{
		BuildPromptLine(line0, sizeof(line0), "   Insert disk *   ", diskNumber);
		StrCopy(line1, sizeof(line1), " and press any key ");
	}
	lines[0] = line0;
	lines[1] = line1;
	DrawPromptBox(lines, 2, (uint8_t)startupConfig.ink);
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
		else if (loaderPromptMode == LoaderPrompt_Disk)
		{
			diskPromptConfirmed = true;
			VID_Quit();
		}
		else if (key >= '1' && key <= '0' + GetConfiguredPartCount())
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

static void ScaleFadePalette(const uint32_t* sourcePalette, uint32_t* fadedPalette, uint16_t count, uint16_t scale)
{
	for (uint16_t n = 0; n < count; n++)
	{
		uint32_t v0 = ((sourcePalette[n] & 0xFF00FFUL) * scale) >> 8;
		uint32_t v1 = ((sourcePalette[n] & 0x00FF00UL) * scale) >> 8;
		fadedPalette[n] = (v0 & 0xFF00FFUL) | (v1 & 0x00FF00UL);
	}
}

#if _WEB

static int frame;
static const int fadeSteps = 8;
static const uint16_t fadeScale[fadeSteps] = { 256, 219, 183, 146, 110, 73, 37, 0 };
static uint32_t fadeSourcePalette[256];
static uint32_t fadeWorkingPalette[256];

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
	(void)elapsed;
	uint16_t fadePaletteSize = VID_GetPaletteSize();
	uint16_t scale = fadeScale[frame];
	ScaleFadePalette(fadeSourcePalette, fadeWorkingPalette, fadePaletteSize, scale);
	VID_SetPaletteEntries(fadeWorkingPalette, fadePaletteSize, 0, false, true);
	frame++;
	
	if (frame >= fadeSteps)
	{
		VID_Quit();
	}
}

static void FadeOut()
{
	uint16_t fadePaletteSize = VID_GetPaletteSize();
	if (fadePaletteSize > 256)
		fadePaletteSize = 256;
	for (uint16_t i = 0; i < fadePaletteSize; i++)
	{
		uint8_t r, g, b;
		VID_GetPaletteColor(i, &r, &g, &b);
		fadeSourcePalette[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}
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
	static uint8_t displayPlanes = 4;
	static DDB_Machine initializedMachine = DDB_MACHINE_AMIGA;
	static DDB_Version initializedVersion = DDB_VERSION_2;

	if (state == Player_Finished || state == Player_Error)
		return state;

	if (state == Player_FadingOut)
	{
		VID_ClearBuffer(true);
		VID_ClearBuffer(false);
		VID_SetPaletteRange(DefaultPalette, 16, 0, true, true);

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
		if (startupPattern[0] == 0)
			ResetStartupConfig();
		DDB_Machine machine = DDB_MACHINE_AMIGA;
		DDB_Language language = DDB_SPANISH;
		DDB_Version version = DDB_VERSION_2;
		screenMode = DDB_GetDefaultScreenMode(machine);

		char path[FILE_MAX_PATH];
		StrCopy(path, FILE_MAX_PATH, location);
		StrCat(path, FILE_MAX_PATH, "*");
		SetStartupPattern(path);
		
		EnumFiles(path);
		LoadStartupConfig();
		CollectDiskImages();
		snapshotCount = CountSnapshots();
		ddbCount = CountDDBFiles();
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
			if (!TryMountDiskImageWithDDBs())
			{
				CloseEnum();
				DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
				return state = Player_Error;
			}

			snapshotCount = CountSnapshots();
			ddbCount = CountDDBFiles();
			scrCount = CountFiles(".scr");
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
			LogBackBufferAnalysisForDDB(ddbFileName, ddb->machine, ddb);
			VID_Initialize(ddb->machine, ddb->version, screenMode);
		}
		else
		{
			StrCopy(ddbFileName, FILE_MAX_PATH, GetDDBFile(0));
			if (!ResolveDDBVideoConfig(ddbFileName, &machine, &language, &version, &screenMode, &displayPlanes))
			{
				DebugPrintf("Rejected invalid DDB %s before initialization\n", ddbFileName);
				if (DDB_GetError() == DDB_ERROR_NONE)
					DDB_SetError(DDB_ERROR_INVALID_FILE);
				return state = Player_Error;
			}
			if (scrCount > 0 && StrIComp(scrExtension, ".scr") == 0)
			{
				const char* screenFile = GetFile(".scr", 0);
				if (ShouldPreferAmigaSCR(screenFile, machine, &screenMode))
					machine = DDB_MACHINE_AMIGA;
				#if HAS_PCX
				else if (HasPCXHeader(screenFile, &screenMode))
					introScreen = screenFile;
				#endif
			}
			VID_SetDisplayPlanesHint(displayPlanes);
			#if HAS_PCX
			if (introScreen == 0)
				introScreen = FindPCXIntroScreen(ddbFileName, machine, version, &screenMode);
			if (introScreen != 0)
				scrCount = 1;
			#endif
			DebugPrintf("Checked %s\n", ddbFileName);
			VID_Initialize(machine, version, screenMode);
				initializedMachine = machine;
				initializedVersion = version;
		    if (DDB_SupportsDataFile(version, machine) && !VID_LoadDataFile(ddbFileName))
			{
				DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
				VID_ShowError(DDB_GetErrorString());
				CloseEnum();
				VID_Finish();
				return state = Player_Error;
			}
		}
		
		if (scrCount > 0)
		{
			#if HAS_PCX
			const char* screenFile = introScreen != 0 ? introScreen : GetFile(scrExtension, 0);
			#else
			const char* screenFile = GetFile(scrExtension, 0);
			#endif
			if (ShouldPreferAmigaSCR(screenFile, machine, &screenMode))
				machine = DDB_MACHINE_AMIGA;
			if (!VID_DisplaySCRFile(screenFile, machine, true))
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
		if (GetConfiguredPartCount() > 1)
		{
			DebugPrintf("Showing part selector\n");
			ddbSelected = -1;
			loaderPromptMode = LoaderPrompt_Part;
			ShowLoaderPrompt(GetConfiguredPartCount(), language);
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
	if (!EnsureSelectedPartMediaAsync(ddbSelected, selectedLanguage))
	{
		if (loaderPromptMode == LoaderPrompt_Disk)
			return state = Player_SelectingPart;
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return state = Player_Error;
	}
	StrCopy(ddbFileName, FILE_MAX_PATH, ResolveSelectedDDBFile(ddbSelected));

	DDB_Machine selectedMachine = DDB_MACHINE_AMIGA;
	DDB_Language selectedLanguage = DDB_SPANISH;
	DDB_Version selectedVersion = DDB_VERSION_2;
	DDB_ScreenMode selectedScreenMode = ScreenMode_VGA16;
	uint8_t selectedDisplayPlanes = 4;
	if (!ResolveDDBVideoConfig(ddbFileName, &selectedMachine, &selectedLanguage, &selectedVersion, &selectedScreenMode, &selectedDisplayPlanes))
	{
		CloseEnum();
		if (DDB_GetError() == DDB_ERROR_NONE)
			DDB_SetError(DDB_ERROR_INVALID_FILE);
		return state = Player_Error;
	}
	if (selectedMachine != initializedMachine || selectedVersion != initializedVersion || selectedScreenMode != screenMode || selectedDisplayPlanes != displayPlanes)
	{
		VID_Finish();
		VID_SetDisplayPlanesHint(selectedDisplayPlanes);
		VID_Initialize(selectedMachine, selectedVersion, selectedScreenMode);
		initializedMachine = selectedMachine;
		initializedVersion = selectedVersion;
		screenMode = selectedScreenMode;
		displayPlanes = selectedDisplayPlanes;
	}
	CloseEnum();
	
	DebugPrintf("Loading %s\n", ddbFileName);

	ddb = DDB_Load(ddbFileName);
	if (!ddb)
	{
		DebugPrintf("Error loading %s", ddbFileName);
		DebugPrintf(": %s\n", DDB_GetErrorString());
		return state = Player_Error;
	}
	DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == 0)
	{
		DebugPrintf("Error creating interpreter: %s\n", DDB_GetErrorString());
		DDB_Close(ddb);
		VID_Finish();
		return state = Player_Error;
	}
	if (DDB_SupportsDataFile(ddb->version, ddb->target) && !VID_LoadDataFile(ddbFileName))
	{
		DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
		VID_ShowError(DDB_GetErrorString());
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		VID_Finish();
		return state = Player_Error;
	}
	#if HAS_PCX
	if (VID_HasExternalPictures())
		screenMode = ScreenMode_VGA;
	#endif
	FadeOut();
	return state = Player_FadingOut;
}

#else

static const int fadeSteps = 8;
static const uint16_t fadeScale[fadeSteps] = { 256, 219, 183, 146, 110, 73, 37, 0 };

static void FadeOut()
{
	uint32_t sourcePalette[256];
	uint32_t palette[256];
	uint16_t fadePaletteSize = VID_GetPaletteSize();
	if (fadePaletteSize > 256)
		fadePaletteSize = 256;
	for (uint16_t i = 0; i < fadePaletteSize; i++)
	{
		uint8_t r, g, b;
		VID_GetPaletteColor(i, &r, &g, &b);
		sourcePalette[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}
	for (int frame = 0; frame < fadeSteps; frame++)
	{
		uint16_t scale = fadeScale[frame];
		ScaleFadePalette(sourcePalette, palette, fadePaletteSize, scale);
		VID_SetPaletteEntries(palette, fadePaletteSize, 0, false, true);
	}
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
		if (ShouldPreferAmigaSCR(GetFile(".scr", 0), *machine, screenMode))
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
	ResetStartupConfig();
	DDB_Machine machine = DDB_MACHINE_AMIGA;
	DDB_Language language = DDB_SPANISH;
	DDB_ScreenMode screenMode = DDB_GetDefaultScreenMode(machine);
	DDB_Version version = DDB_VERSION_2;
	const char* introScreen = 0;
	introScreenName[0] = 0;
	SetStartupPattern("*");

	EnumFiles();
	LoadStartupConfig();
	CollectDiskImages();
	
	ddbCount = CountDDBFiles();
	if (ddbCount == 0)
	{
		if (!TryMountDiskImageWithDDBs())
		{
			CloseEnum();
			DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
			return false;
		}

		ddbCount = CountDDBFiles();
		if (ddbCount == 0)
		{
			CloseEnum();
			DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
			return false;
		}
	}

	StrCopy(ddbFileName, FILE_MAX_PATH, GetDDBFile(0));
	uint8_t displayPlanes = 4;

	if (!ResolveDDBVideoConfig(ddbFileName, &machine, &language, &version, &screenMode, &displayPlanes))
	{
		CloseEnum();
		if (DDB_GetError() == DDB_ERROR_NONE)
			DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}
	VID_SetDisplayPlanesHint(displayPlanes);

	#if HAS_PCX
	CheckIntroScreenFiles(ddbFileName, &introScreen, &machine, version, &screenMode);
	#else
	CheckIntroScreenFiles(&introScreen, &machine, &screenMode);
	#endif
	if (introScreen != 0 && introScreen[0] != 0)
	{
		StrCopy(introScreenName, sizeof(introScreenName), introScreen);
		introScreen = introScreenName;
	}

	#ifdef _AMIGA
	OpenKeyboard();
	OpenAudio();
	#endif

	VID_Initialize(machine, version, screenMode);
	if (introScreen != 0)
	{
		if (!VID_DisplaySCRFile(introScreen, machine, true))
			VID_ShowError(DDB_GetErrorString());
	}

	if (GetConfiguredPartCount() > 1)
	{
		DebugPrintf("Showing part selector\n");
		ddbSelected = -1;
		loaderPromptMode = LoaderPrompt_Part;
		ShowLoaderPrompt(GetConfiguredPartCount(), language);
		VID_MainLoop(0, LoaderScreenUpdate);
		if (exitGame)
			return true;
		if (ddbSelected == -1)
		{
			CloseEnum();
			VID_Finish();
			return true;
		}
		DebugPrintf("Selected part %ld\n", (long)(ddbSelected + 1));
		if (!EnsureSelectedPartMediaSync(ddbSelected, language))
		{
			CloseEnum();
			DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
			return false;
		}
		StrCopy(ddbFileName, FILE_MAX_PATH, ResolveSelectedDDBFile(ddbSelected));

		DDB_Machine selectedMachine = DDB_MACHINE_AMIGA;
		DDB_Language selectedLanguage = DDB_SPANISH;
		DDB_Version selectedVersion = DDB_VERSION_2;
		DDB_ScreenMode selectedScreenMode = ScreenMode_VGA16;
		uint8_t selectedDisplayPlanes = 4;
		if (!ResolveDDBVideoConfig(ddbFileName, &selectedMachine, &selectedLanguage, &selectedVersion, &selectedScreenMode, &selectedDisplayPlanes))
		{
			CloseEnum();
			if (DDB_GetError() == DDB_ERROR_NONE)
				DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		#if HAS_PCX
		CheckIntroScreenFiles(ddbFileName, &introScreen, &selectedMachine, selectedVersion, &selectedScreenMode);
		#else
		CheckIntroScreenFiles(&introScreen, &selectedMachine, &selectedScreenMode);
		#endif
		if (introScreen != 0 && introScreen[0] != 0)
		{
			StrCopy(introScreenName, sizeof(introScreenName), introScreen);
			introScreen = introScreenName;
		}

		if (selectedMachine != machine || selectedVersion != version || selectedScreenMode != screenMode || selectedDisplayPlanes != displayPlanes)
		{
			VID_Finish();
			VID_SetDisplayPlanesHint(selectedDisplayPlanes);
			VID_Initialize(selectedMachine, selectedVersion, selectedScreenMode);
			machine = selectedMachine;
			version = selectedVersion;
			screenMode = selectedScreenMode;
			displayPlanes = selectedDisplayPlanes;
			if (introScreen != 0)
				VID_DisplaySCRFile(introScreen, machine, false);
		}
		else if (introScreen != 0)
		{
			VID_DisplaySCRFile(introScreen, machine, false);
		}
	}

	CloseEnum();
	
	DebugPrintf("Loading %s\n", ddbFileName);

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
	if (DDB_RequiresBackBuffer(ddb))
		VID_EnableBackBuffer();

	uint32_t tAfterDDBLoad = 0;
	VID_GetMilliseconds(&tAfterDDBLoad);
	DebugPrintf("DDB_Load completed in %lu ms\n", (unsigned long)(tAfterDDBLoad - tLoadStart));
	VID_ShowProgressBar(64);

	DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == 0)
	{
		DebugPrintf("Error creating interpreter: %s\n", DDB_GetErrorString());
		DDB_Close(ddb);
		VID_Finish();
		return false;
	}

	if (DDB_SupportsDataFile(ddb->version, ddb->target) && !VID_LoadDataFile(ddbFileName)) 
	{
		DebugPrintf("VID_LoadDataFile(%s) failed: %s\n", ddbFileName, DDB_GetErrorString());
		VID_ShowError(DDB_GetErrorString());
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		VID_Finish();
		return false;
	}

	#if HAS_PCX
	if (VID_HasExternalPictures())
		screenMode = ScreenMode_VGA;
	#endif
	VID_ShowProgressBar(255);

	if (scrCount > 0 && ddbCount == 1)
	{
		VID_DisplaySCRFile(introScreen, machine, false);
		DDB_Interpreter* i = interpreter;
		VID_MainLoop(0, WaitForKeyUpdate);
		interpreter = i;
		if (exitGame)
		{
			DDB_CloseInterpreter(interpreter);
			DDB_Close(ddb);
			VID_Finish();
			return true;
		}
	}
	if (scrCount > 0)
		FadeOut();

	VID_ClearBuffer(true);
	VID_ClearBuffer(false);
	VID_SetPaletteRange(DefaultPalette, 16, 0, true, true);

	DebugPrintf("Starting interpreter\n");
	DDB_Run(interpreter);
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	VID_Finish();

	return true;
}

#endif
