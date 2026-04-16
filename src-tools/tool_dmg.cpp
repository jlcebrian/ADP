#include <dmg.h>
#include <dmg_pack.h>
#include <img.h>
#include <cli_parser.h>
#include <ddb.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_file.h>
#include <session_commands.h>

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#if defined(_STDCLIB)
#if defined(_WIN32)
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif
#endif

#define LOAD_BUFFER_SIZE 1024*1024*3

static int ignoredWildcardExpansionCount = 0;
static bool debug = false;
static const uint32_t DAT5_MIN_ZX0_SAVINGS_BYTES = 32;
static const uint32_t DAT5_MIN_ZX0_SAVINGS_PERCENT = 1;
static const char* DMG_CACHE_EXTENSION = ".dmgcache";

typedef enum
{
    ExtractFormat_PNG,
    ExtractFormat_PI1,
    ExtractFormat_PL1,
    ExtractFormat_IFF,
}
ExtractFormat;

static const char* imageExtensions[] = { "png", "pcx", "vga", "pi1", "pl1", "iff", "ilbm" };
static const int count = sizeof(imageExtensions) / sizeof(imageExtensions[0]);

typedef enum
{
	OPTION_X,
	OPTION_Y,
	OPTION_FIRST_COLOR,
	OPTION_LAST_COLOR,
	OPTION_BUFFER,
	OPTION_FIXED,
	OPTION_CGA,
	OPTION_FREQ,
}
ColonOption;

static const char* colonOptions[] = {
	"x:",
	"y:",
	"first:",
	"last:",
	"buffer:",
	"fixed:",
	"cga:",
	"freq:",
};
static const int colonOptionCount = sizeof(colonOptions) / sizeof(colonOptions[0]);

static struct
{
	int value;
	DMG_KHZ freq;
}
frequencies[] =
{
	{ 5,     DMG_5KHZ },
	{ 7,     DMG_7KHZ },
	{ 9,     DMG_9_5KHZ },
	{ 15,    DMG_15KHZ },
	{ 20,    DMG_20KHZ },
	{ 30,    DMG_30KHZ },
    { 44,    DMG_44_1KHZ },
    { 48,    DMG_48KHZ },
	{ 5000,  DMG_5KHZ },
	{ 7000,  DMG_7KHZ },
	{ 95,    DMG_9_5KHZ },
	{ 9500,  DMG_9_5KHZ },
	{ 15000, DMG_15KHZ },
	{ 20000, DMG_20KHZ },
	{ 30000, DMG_30KHZ },
	{ 44100, DMG_44_1KHZ },
    { 48000, DMG_48KHZ },
	{ 0,     (DMG_KHZ)0 },
};

static uint8_t* buffer = NULL;
static uint32_t bufferSize = 0;
static DMG_ImageMode extractMode = ImageMode_Indexed;
static ExtractFormat extractFormat = ExtractFormat_PNG;
static char extractOutputDirectory[FILE_MAX_PATH];
static bool extractJsonSidecar = true;
static char filename[1024];
static char newfilename[1024];
static bool selected[256];
static bool verbose = false;
static bool listSortById = true;
static bool readOnly = true;
static bool createDAT5 = false;
static bool createFormatExplicit = false;
static DMG_Version createLegacyVersion = DMG_Version2;
static DMG_DAT5ColorMode createDAT5Mode = DMG_DAT5_COLORMODE_NONE;
static uint16_t createDAT5Width = 320;
static uint16_t createDAT5Height = 200;
static bool createImportWidthExplicit = false;
static bool createImportHeightExplicit = false;
static uint16_t createImportWidth = 0;
static uint16_t createImportHeight = 0;
static bool compressionEnabled = true;
static bool ignoreMissingWildcards = false;
static char commandError[512];

static void ClearCommandError()
{
    commandError[0] = 0;
}

static bool SetCommandError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(commandError, sizeof(commandError), format, args);
    va_end(args);
    return false;
}

static const char* GetCommandError()
{
    return commandError[0] != 0 ? commandError : "Invalid arguments";
}

typedef struct
{
    uint32_t fileSize;
    uint32_t modifyTime;
}
DMGSourceFileInfo;

static bool GetSourceFileInfo(const char* fileName, DMGSourceFileInfo* info)
{
    if (fileName == 0 || info == 0)
        return false;

#if defined(_STDCLIB) && defined(_WIN32)
    struct _stat64 st;
    if (_stat64(fileName, &st) != 0)
        return false;
    info->fileSize = st.st_size > 0xFFFFFFFFll ? 0xFFFFFFFFu : (uint32_t)st.st_size;
    info->modifyTime = (uint32_t)st.st_mtime;
    return true;
#elif defined(_STDCLIB)
    struct stat st;
    if (stat(fileName, &st) != 0)
        return false;
    info->fileSize = st.st_size > 0xFFFFFFFFll ? 0xFFFFFFFFu : (uint32_t)st.st_size;
    info->modifyTime = (uint32_t)st.st_mtime;
    return true;
#else
    FindFileResults results;
    if (!File_FindFirst(fileName, &results))
        return false;
    info->fileSize = results.fileSize;
    info->modifyTime = results.modifyTime;
    return true;
#endif
}
static void ResetCreateSettings()
{
    createDAT5 = false;
    createFormatExplicit = false;
    createLegacyVersion = DMG_Version2;
    createDAT5Mode = DMG_DAT5_COLORMODE_NONE;
    createDAT5Width = 320;
    createDAT5Height = 200;
    createImportWidthExplicit = false;
    createImportHeightExplicit = false;
    createImportWidth = 0;
    createImportHeight = 0;
}

typedef enum
{
    REMAP_NONE,
    REMAP_MIN,
    REMAP_RESERVE,
    REMAP_STD,
    REMAP_DARK,
    REMAP_RANGE,
}
RemapMode;

static RemapMode remapMode = REMAP_NONE;
static int remapRangeFirst = 0;
static int remapRangeLast = 0;
static uint8_t priorityEntries[256];
static int priorityEntryCount = 0;

typedef struct
{
    bool hasX;
    bool hasY;
    bool hasWidth;
    bool hasHeight;
    bool hasFirstColor;
    bool hasLastColor;
    bool hasBuffer;
    bool hasFixed;
    bool hasAmigaHack;
    bool hasClone;
    bool hasCompression;
    bool useSidecarJson;
    bool hasJsonFile;
    int x;
    int y;
    int width;
    int height;
    int firstColor;
    int lastColor;
    int cloneSource;
    bool buffer;
    bool fixed;
    bool amigaHack;
    bool compression;
    char jsonFile[FILE_MAX_PATH];
}
PendingInputOptions;

static PendingInputOptions pendingInputOptions;

static void ResetPendingInputOptions()
{
    MemClear(&pendingInputOptions, sizeof(pendingInputOptions));
    pendingInputOptions.useSidecarJson = true;
}

typedef enum
{
	ACTION_LIST,
	ACTION_EXTRACT,
	ACTION_EXTRACT_PALETTES,
	ACTION_TEST,
	ACTION_ADD,
	ACTION_DELETE,
	ACTION_NEW,
	ACTION_UPDATE,
	ACTION_SHELL,
	ACTION_HELP,
}
Action;

static Action action = ACTION_LIST;

typedef enum
{
    DMG_OPTION_VERBOSE = 1,
    DMG_OPTION_SORT_BY_ID,
    DMG_OPTION_FILE_ORDER,
    DMG_OPTION_HELP,
}
DmgOption;

static const CLI_ActionSpec actionSpecs[] =
{
    { "list", "list", ACTION_LIST },
    { "l", "list", ACTION_LIST },
    { "list-palettes", "list-palettes", ACTION_LIST },
    { "v", "list-palettes", ACTION_LIST },
    { "extract", "extract", ACTION_EXTRACT },
    { "x", "extract", ACTION_EXTRACT },
    { "extract-palettes", "extract-palettes", ACTION_EXTRACT_PALETTES },
    { "p", "extract-palettes", ACTION_EXTRACT_PALETTES },
    { "test", "test", ACTION_TEST },
    { "t", "test", ACTION_TEST },
    { "add", "add", ACTION_ADD },
    { "a", "add", ACTION_ADD },
    { "delete", "delete", ACTION_DELETE },
    { "d", "delete", ACTION_DELETE },
    { "create", "create", ACTION_NEW },
    { "n", "create", ACTION_NEW },
    { "update", "update", ACTION_UPDATE },
    { "u", "update", ACTION_UPDATE },
    { "shell", "shell", ACTION_SHELL },
    { "help", "help", ACTION_HELP },
    { "h", "help", ACTION_HELP },
    { 0, 0, 0 }
};

static const CLI_OptionSpec optionSpecs[] =
{
    { 'v', "verbose", DMG_OPTION_VERBOSE, CLI_OPTION_NONE },
    { 'n', "sort-by-id", DMG_OPTION_SORT_BY_ID, CLI_OPTION_NONE },
    { 0, "file-order", DMG_OPTION_FILE_ORDER, CLI_OPTION_NONE },
    { 'h', "help", DMG_OPTION_HELP, CLI_OPTION_NONE },
    { 0, 0, 0, CLI_OPTION_NONE }
};

#pragma pack(push, 1)
struct WAVHeader
{
	uint8_t		riff[4];
	uint32_t 	fileSize;
	uint8_t		wave[4];
	uint8_t		fmt0[4];
	uint32_t	fmtSize;
	uint16_t	format;			// 1 - PCM
	uint16_t	channels;		// 1 - mono, 2 - stereo
	uint32_t	samplesPerSec;	// 8000, 11025, 22050, 44100
	uint32_t	avgBytesPerSec;	// samplesPerSec * channels * bitsPerSample/8
	uint16_t	blockAlign;		// bitsPerSample * channels / 8
	uint16_t	bitsPerSample;	// 8, 16
	uint8_t		data[4];
	uint32_t	dataSize;
};
#pragma pack(pop)

void TracePrintf(const char* format, ...)
{
}

static void PrintHelp()
{
	printf("DMG file utility for DAAD " VERSION_STR "\n\n");
    printf("Usage: dmg [global options] <action> <file> [arguments]\n");
    printf("       dmg [global options] <file> [arguments]\n\n");
	printf("Actions:\n\n");
	printf("    list, l              List contents of a container (default)\n");
	printf("    list-palettes, v     List contents with palettes\n");
	printf("    extract, x           Extract image/audio entries\n");
	printf("    test, t              Decode and validate entries\n");
	printf("    extract-palettes, p  Extract image palettes\n");
	printf("    add, a               Add or replace entries\n");
	printf("    delete, d            Delete entries\n");
	printf("    create, n            Create a new container\n");
	printf("    update, u            Rebuild or modify a container\n");
	printf("    shell                Open an interactive shell\n");
	printf("    help, h              Show this help\n");
    printf("\n");
    printf("Global options:\n\n");
    printf("   -v, --verbose      Enable verbose/debug output\n");
    printf("   -n, --sort-by-id   List entries by id (default)\n");
    printf("       --file-order   List entries by file offset\n");
	printf("   -h, --help         Show this help\n");
    printf("\n");
    printf("Modern options accept both '-name value' and '-name=value'.\n");
    printf("Double-dash forms are accepted too.\n");
    printf("File metadata options bind to the adjacent file token and do not stay sticky.\n");
    printf("\n");
	printf("Container formats:\n\n");
    printf("   dat5       Modern DAT V5\n");
    printf("   dat2       DAT V2\n");
    printf("   dat1       DAT V1\n");
    printf("   ega        DOS EGA DAT\n");
    printf("   cga        DOS CGA DAT\n");
    printf("   pcw        Amstrad PCW DAT\n");
    printf("\n");
	printf("Input formats:\n\n");
	printf("   .png       Indexed PNG input\n");
    printf("   .pcx/.vga  Indexed PCX input\n");
    printf("   .pi1/.pl1  Atari ST Degas low-res input\n");
    printf("   .iff/.ilbm Amiga ILBM indexed input\n");
	printf("   .wav       Audio input\n");
    printf("\n");
	printf("Common add/create/update options:\n\n");
    printf("   -format <id>       Container format: dat5, dat2, dat1, ega, cga, pcw\n");
    printf("   -mode <id>         DAT5 mode: cga, ega, planar4, planar5, planar8,\n");
    printf("                      planar4st, planar8st\n");
    printf("   -type <id>         Alias for -mode\n");
    printf("   -screen <WxH>      DAT5 screen size: 320x200, 640x200, 640x400\n");
    printf("   -id <n>            Target entry id for the following file. Repeats increment automatically.\n");
    printf("   -fixed|-float      Set or clear fixed flag on the adjacent file, or on selected entries during update\n");
    printf("   -buffer|-nobuffer  Set or clear buffered flag on the adjacent file, or on selected entries during update\n");
    printf("   -compress|-nocompress Enable or disable image compression\n");
    printf("   -x <n> -y <n>      Set image coordinates on the adjacent file, or on selected entries during update\n");
    printf("   -width <n>         Crop or pad the adjacent image to this width before import\n");
    printf("   -height <n>        Crop or pad the adjacent image to this height before import\n");
    printf("   -pcs <n> -pce <n>  Set first and last palette color on the adjacent file, or on selected entries during update\n");
    printf("   -json auto|off|<file> Use adjacent JSON metadata, disable it, or force a file\n");
    printf("   -clone <n>         Clone entry data from another slot instead of reading a file\n");
    printf("   -amiga-hack|-noamiga-hack Toggle the classic DAT Amiga 4-bit palette marker on the adjacent file\n");
    printf("   -remap <mode>      Palette remap mode for DAT5, dat1, dat2: min, reserve, std, dark, A-B\n");
    printf("   -priority <list>   Rebuild order preference for update/create\n");
    printf("   -ignore-missing    Ignore unmatched wildcard file arguments\n");
    printf("\n");
    printf("Extract options:\n\n");
    printf("   -output <dir>      Destination directory\n");
    printf("   -as <fmt>          Image output format: png, pi1, pl1, iff\n");
    printf("   -json auto|off     Write sidecar JSON metadata next to extracted files (default: auto)\n");
    printf("   -nojson            Disable sidecar JSON metadata on extract\n");
    printf("   Selectors accept: 12, 12,14,18, 12-20, 12-\n");
    printf("\n");
    printf("Examples:\n\n");
    printf("   dmg create game.dat -format dat5 -mode planar4 -screen 320x200 title.png -id 0\n");
    printf("   dmg create intro.ega -format ega intro.pi1 -id 0\n");
    printf("   dmg add game.dat sprites/*.png -id 32 -buffer -json auto\n");
    printf("   dmg update game.dat -priority 0,1,2 -output game-repacked.dat\n");
    printf("   dmg extract game.dat 0-10 -as iff -output exports\n");
    printf("   dmg shell game.dat\n");
}

static void ShowWarning(const char* message)
{
	fprintf(stderr, "Warning: %s\n", message);
}

static int GetPaletteLimit(DMG_DAT5ColorMode mode)
{
    switch (mode)
    {
        case DMG_DAT5_COLORMODE_CGA: return 4;
        case DMG_DAT5_COLORMODE_EGA:
        case DMG_DAT5_COLORMODE_PLANAR4:
        case DMG_DAT5_COLORMODE_PLANAR4ST: return 16;
        case DMG_DAT5_COLORMODE_PLANAR5: return 32;
        case DMG_DAT5_COLORMODE_PLANAR8:
        case DMG_DAT5_COLORMODE_PLANAR8ST: return 256;
        default: return 16;
    }
}

static uint8_t GetBitDepthForMode(DMG_DAT5ColorMode mode)
{
    switch (mode)
    {
        case DMG_DAT5_COLORMODE_CGA: return 0;
        case DMG_DAT5_COLORMODE_EGA: return 0;
        default: return DMG_DAT5ModePlaneCount(mode);
    }
}

static bool ParseDAT5Mode(const char* value, DMG_DAT5ColorMode* mode)
{
    if (stricmp(value, "cga") == 0) *mode = DMG_DAT5_COLORMODE_CGA;
    else if (stricmp(value, "ega") == 0) *mode = DMG_DAT5_COLORMODE_EGA;
    else if (stricmp(value, "planar4") == 0 || stricmp(value, "i16") == 0) *mode = DMG_DAT5_COLORMODE_PLANAR4;
    else if (stricmp(value, "planar5") == 0 || stricmp(value, "i32") == 0 || stricmp(value, "ocs") == 0 || stricmp(value, "amiga") == 0) *mode = DMG_DAT5_COLORMODE_PLANAR5;
    else if (stricmp(value, "planar8") == 0 || stricmp(value, "i256") == 0 || stricmp(value, "vga") == 0 || stricmp(value, "aga") == 0) *mode = DMG_DAT5_COLORMODE_PLANAR8;
    else if (stricmp(value, "planar4st") == 0 || stricmp(value, "st") == 0) *mode = DMG_DAT5_COLORMODE_PLANAR4ST;
    else if (stricmp(value, "planar8st") == 0 || stricmp(value, "falcon") == 0) *mode = DMG_DAT5_COLORMODE_PLANAR8ST;
    else return false;
    return true;
}

static bool ParseDAT5Size(const char* value, uint16_t* width, uint16_t* height)
{
    const char* x = strchr(value, 'x');
    if (x == NULL)
        x = strchr(value, 'X');
    if (x == NULL)
        return false;
    int w = atoi(value);
    int h = atoi(x + 1);
    if (w <= 0 || h <= 0)
        return false;
    *width = (uint16_t)w;
    *height = (uint16_t)h;
    return true;
}

static bool ParseContainerFormat(const char* value, bool* isDAT5, DMG_Version* legacyVersion)
{
    *isDAT5 = false;
    *legacyVersion = DMG_Version2;
    if (stricmp(value, "dat1") == 0) *legacyVersion = DMG_Version1;
    else if (stricmp(value, "dat2") == 0) *legacyVersion = DMG_Version2;
    else if (stricmp(value, "ega") == 0) *legacyVersion = DMG_Version1_EGA;
    else if (stricmp(value, "cga") == 0) *legacyVersion = DMG_Version1_CGA;
    else if (stricmp(value, "pcw") == 0) *legacyVersion = DMG_Version1_PCW;
    else if (stricmp(value, "dat5") == 0) *isDAT5 = true;
    else return false;
    return true;
}

static void BuildSidecarPath(const char* fileName, const char* extension, char* output, size_t outputSize)
{
    StrCopy(output, outputSize, fileName);
    char* dot = strrchr(output, '.');
    if (dot != 0)
        *dot = 0;
    StrCat(output, outputSize, extension);
}

static bool ParseJsonIntField(const char* json, const char* key, int* outValue)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* ptr = strstr(json, pattern);
    if (ptr == 0)
        return false;
    ptr += strlen(pattern);
    while (*ptr != 0 && isspace((unsigned char)*ptr))
        ptr++;
    if (*ptr != ':')
        return false;
    ptr++;
    while (*ptr != 0 && isspace((unsigned char)*ptr))
        ptr++;
    if (*ptr == '-' || isdigit((unsigned char)*ptr))
    {
        *outValue = atoi(ptr);
        return true;
    }
    return false;
}

static bool LoadPendingOptionsFromJson(const char* jsonFile, PendingInputOptions* options)
{
    File* file = File_Open(jsonFile, ReadOnly);
    if (file == 0)
        return false;

    uint32_t size = (uint32_t)File_GetSize(file);
    char* json = Allocate<char>("Image JSON", size + 1);
    if (json == 0)
    {
        File_Close(file);
        return false;
    }
    bool ok = File_Read(file, json, size) == size;
    File_Close(file);
    if (!ok)
    {
        Free(json);
        return false;
    }
    json[size] = 0;

    int value = 0;
    if (ParseJsonIntField(json, "X", &value)) { options->hasX = true; options->x = value; }
    if (ParseJsonIntField(json, "Y", &value)) { options->hasY = true; options->y = value; }
    if (ParseJsonIntField(json, "width", &value)) { options->hasWidth = true; options->width = value; }
    if (ParseJsonIntField(json, "height", &value)) { options->hasHeight = true; options->height = value; }
    if (ParseJsonIntField(json, "PCS", &value)) { options->hasFirstColor = true; options->firstColor = value; }
    if (ParseJsonIntField(json, "PCE", &value)) { options->hasLastColor = true; options->lastColor = value; }
    if (ParseJsonIntField(json, "float", &value)) { options->hasFixed = true; options->fixed = value == 0; }
    if (ParseJsonIntField(json, "buffer", &value)) { options->hasBuffer = true; options->buffer = value != 0; }
    if (ParseJsonIntField(json, "clone", &value) && value != 0)
    {
        if (ParseJsonIntField(json, "location", &value))
        {
            options->hasClone = true;
            options->cloneSource = value;
        }
    }
    if (ParseJsonIntField(json, "compress", &value))
    {
        options->hasCompression = true;
        options->compression = value != 0;
    }

    Free(json);
    return true;
}

typedef struct
{
    uint32_t sourceSize;
    uint32_t sourceTime;
    uint32_t width;
    uint32_t height;
    uint32_t payloadSize;
    uint16_t format;
    uint16_t colorMode;
    uint8_t  compressed;
    uint8_t  remap;
    uint8_t  firstColor;
    uint8_t  lastColor;
}
DMGCacheHeader;

#define DMG_CACHE_MAGIC 0x444D4743u
#define DMG_CACHE_VERSION_MULTI 2u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sourceSize;
    uint32_t sourceTime;
}
DMGCacheFileHeader;

typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t payloadSize;
    uint16_t format;
    uint16_t colorMode;
    uint8_t  compressed;
    uint8_t  remap;
    uint8_t  firstColor;
    uint8_t  lastColor;
}
DMGCacheEntryHeader;

static bool DMGCacheMatches(const DMGCacheHeader* header, uint32_t sourceSize, uint32_t sourceTime, uint32_t format, uint32_t colorMode, uint8_t remap, uint8_t firstColor, uint8_t lastColor, uint32_t width, uint32_t height, uint8_t compressed)
{
    return header->sourceSize == sourceSize &&
        header->sourceTime == sourceTime &&
        header->format == format &&
        header->colorMode == colorMode &&
        header->remap == remap &&
        header->firstColor == firstColor &&
        header->lastColor == lastColor &&
        header->width == width &&
        header->height == height &&
        header->compressed == compressed;
}

static bool DMGCacheEntryMatches(const DMGCacheEntryHeader* entry, uint32_t format, uint32_t colorMode, uint8_t remap, uint8_t firstColor, uint8_t lastColor, uint32_t width, uint32_t height, uint8_t compressed)
{
    return entry->format == format &&
        entry->colorMode == colorMode &&
        entry->remap == remap &&
        entry->firstColor == firstColor &&
        entry->lastColor == lastColor &&
        entry->width == width &&
        entry->height == height &&
        entry->compressed == compressed;
}

static bool DMGCacheReadPayload(File* file, uint32_t payloadSize, uint8_t** payload, uint32_t* payloadSizeOut)
{
    uint8_t* data = Allocate<uint8_t>("DMG cache payload", payloadSize);
    if (data == 0)
        return false;
    if (File_Read(file, data, payloadSize) != payloadSize)
    {
        Free(data);
        return false;
    }
    *payload = data;
    *payloadSizeOut = payloadSize;
    return true;
}

static bool TryLoadCachedPayload(const char* sourceFile, const char* cacheFile, uint32_t format, uint32_t colorMode, uint8_t remap, uint8_t firstColor, uint8_t lastColor, uint32_t width, uint32_t height, uint8_t compressed, uint8_t** payload, uint32_t* payloadSize)
{
    *payload = 0;
    *payloadSize = 0;

    DMGSourceFileInfo sourceInfo;
    if (!GetSourceFileInfo(sourceFile, &sourceInfo))
        return false;

    File* file = File_Open(cacheFile, ReadOnly);
    if (file == 0)
        return false;

    DMGCacheFileHeader fileHeader;
    if (File_Read(file, &fileHeader, sizeof(fileHeader)) != sizeof(fileHeader))
    {
        File_Close(file);
        return false;
    }

    if (fileHeader.magic != DMG_CACHE_MAGIC)
    {
        File_Close(file);
        return false;
    }

    if (fileHeader.version != DMG_CACHE_VERSION_MULTI ||
        fileHeader.sourceSize != sourceInfo.fileSize ||
        fileHeader.sourceTime != sourceInfo.modifyTime)
    {
        File_Close(file);
        return false;
    }

    bool found = false;
    uint8_t* matchedPayload = 0;
    uint32_t matchedPayloadSize = 0;
    uint64_t fileSize = File_GetSize(file);
    while (File_GetPosition(file) + sizeof(DMGCacheEntryHeader) <= fileSize)
    {
        DMGCacheEntryHeader entry;
        if (File_Read(file, &entry, sizeof(entry)) != sizeof(entry))
            break;

        if (entry.payloadSize > fileSize || File_GetPosition(file) + entry.payloadSize > fileSize)
            break;

        if (DMGCacheEntryMatches(&entry, format, colorMode, remap, firstColor, lastColor, width, height, compressed))
        {
            uint8_t* data = 0;
            uint32_t dataSize = 0;
            if (!DMGCacheReadPayload(file, entry.payloadSize, &data, &dataSize))
                break;
            if (matchedPayload != 0)
                Free(matchedPayload);
            matchedPayload = data;
            matchedPayloadSize = dataSize;
            found = true;
        }
        else if (!File_Seek(file, File_GetPosition(file) + entry.payloadSize))
        {
            break;
        }
    }

    File_Close(file);
    if (!found)
        return false;

    *payload = matchedPayload;
    *payloadSize = matchedPayloadSize;
    return true;
}

static bool OpenCacheForAppend(const char* cacheFile, uint32_t sourceSize, uint32_t sourceTime, File** cacheHandle)
{
    *cacheHandle = 0;

    File* file = File_Open(cacheFile, ReadWrite);
    if (file != 0)
    {
        DMGCacheFileHeader header;
        bool valid = File_Read(file, &header, sizeof(header)) == sizeof(header) &&
            header.magic == DMG_CACHE_MAGIC &&
            header.version == DMG_CACHE_VERSION_MULTI &&
            header.sourceSize == sourceSize &&
            header.sourceTime == sourceTime;
        if (valid && File_Seek(file, File_GetSize(file)))
        {
            *cacheHandle = file;
            return true;
        }
        File_Close(file);
    }

    file = File_Create(cacheFile);
    if (file == 0)
        return false;

    DMGCacheFileHeader header;
    header.magic = DMG_CACHE_MAGIC;
    header.version = DMG_CACHE_VERSION_MULTI;
    header.sourceSize = sourceSize;
    header.sourceTime = sourceTime;
    if (File_Write(file, &header, sizeof(header)) != sizeof(header))
    {
        File_Close(file);
        return false;
    }

    *cacheHandle = file;
    return true;
}

static void SaveCachedPayload(const char* sourceFile, const char* cacheFile, uint32_t format, uint32_t colorMode, uint8_t remap, uint8_t firstColor, uint8_t lastColor, uint32_t width, uint32_t height, uint8_t compressed, const uint8_t* payload, uint32_t payloadSize)
{
    DMGSourceFileInfo sourceInfo;
    if (!GetSourceFileInfo(sourceFile, &sourceInfo))
        return;

    File* file = 0;
    if (!OpenCacheForAppend(cacheFile, sourceInfo.fileSize, sourceInfo.modifyTime, &file) || file == 0)
        return;

    DMGCacheEntryHeader header;
    header.width = width;
    header.height = height;
    header.payloadSize = payloadSize;
    header.format = (uint16_t)format;
    header.colorMode = (uint16_t)colorMode;
    header.compressed = compressed;
    header.remap = remap;
    header.firstColor = firstColor;
    header.lastColor = lastColor;

    File_Write(file, &header, sizeof(header));
    File_Write(file, payload, payloadSize);
    File_Close(file);
}

static bool EncodePCWStoredLayout(const uint8_t* indexed, uint16_t width, uint16_t height, uint8_t* output, uint32_t outputSize)
{
    uint32_t rowBytes = ((uint32_t)width + 7) >> 3;
    uint32_t monoSize = rowBytes * height;
    if ((height & 1) != 0 || outputSize < monoSize)
        return false;

    uint8_t* decoded = Allocate<uint8_t>("PCW mono", monoSize);
    if (decoded == 0)
        return false;

    for (uint16_t y = 0; y < height; y++)
    {
        const uint8_t* src = indexed + y * width;
        uint8_t* row = decoded + y * rowBytes;
        MemClear(row, rowBytes);
        for (uint16_t x = 0; x < width; x++)
        {
            if (src[x] != 0)
                row[x >> 3] |= (uint8_t)(0x80u >> (x & 7));
        }
    }

    MemClear(output, monoSize);
    for (uint32_t pair = 0; pair < height / 2; pair++)
    {
        uint32_t sourceTop = pair * rowBytes * 2;
        uint32_t sourceBottom = sourceTop + rowBytes * 2 - 1;
        uint32_t destBase = (pair >> 2) * width + (pair & 3) * 2;
        for (uint32_t xByte = 0; xByte < rowBytes; xByte++)
        {
            uint32_t dest = destBase + (xByte << 3);
            output[dest + 0] = decoded[sourceTop + xByte];
            output[dest + 1] = decoded[sourceBottom - xByte];
        }
    }

    Free(decoded);
    return true;
}

static void ClearSelection(bool* entries);
static int CountSelectedEntries(const bool* entries);
static int FirstSelectedEntry(const bool* entries);
static void SelectSingleEntry(bool* entries, int index);
static bool ParseSelectionToken(const char* token, bool* entries);
static bool IsPropertyToken(const char* token);
static bool ApplyPropertyToSelection(DMG* dmg, const bool* currentSelection, const char* token);
static void PrepareIndexedExportPalette(DMG* dmg, const DMG_Entry* entry, uint32_t* palette, int paletteSize, uint32_t* expandedPalette, uint32_t** exportPalette, int* exportPaletteSize);
static bool IsImageToken(const char* token);
static bool IsAudioToken(const char* token);
static bool IsTargetedFileToken(const char* token);
static bool ParseOptionValue(const char* ptr, int* value);
static bool ExecuteCLICommandLine(int argc, char* argv[]);
static bool TranslateModernArguments(Action action, int inputCount, const char* inputArgs[], const char** translatedArgs, int* translatedCount, const char** explicitOutputFile);
static bool PrepareSessionTarget(int argc, char* argv[], DMG** outDmg);
static bool RunSessionCommands(int argc, char* argv[], DMG* dmg);
static bool RunInteractiveSession(DMG* dmg);

static bool InferTargetIndexForToken(const char* token, const bool* currentSelection, bool explicitSelection, int currentIndex, bool currentFileSaved, const bool* usedEntries, int* targetIndex)
{
    const char* colon = strchr(token, ':');
    const char* slash = strrchr(token, '/');
    const char* backslash = strrchr(token, '\\');
    const char* filepart = slash ? slash + 1 : backslash ? backslash + 1 : token;

    if (colon != 0 && colon != token && isdigit(*token))
    {
        int value = atoi(token);
        if (value < 0 || value > 255)
            return false;
        *targetIndex = value;
        return true;
    }

    int selectionCount = CountSelectedEntries(currentSelection);
    if (explicitSelection && selectionCount > 1)
        return false;
    if (explicitSelection && selectionCount == 1)
    {
        *targetIndex = FirstSelectedEntry(currentSelection);
        return true;
    }
    if (isdigit(*filepart) && atoi(filepart) < 256)
    {
        *targetIndex = atoi(filepart);
        return true;
    }
    if (!currentFileSaved)
    {
        for (int i = 0; i < 256; i++)
        {
            if (!usedEntries[i])
            {
                *targetIndex = i;
                return true;
            }
        }
        return false;
    }
    if (currentIndex >= 0 && currentIndex < 255)
    {
        *targetIndex = currentIndex + 1;
        return true;
    }
    return false;
}

static bool InferDAT5CreateRange(int tokenCount, const char* tokens[], uint8_t* first, uint8_t* last)
{
    bool currentSelection[256];
    bool usedEntries[256];
    int currentIndex = -1;
    bool explicitSelection = false;
    bool currentFileSaved = false;
    bool found = false;
    uint8_t usedFirst = 255;
    uint8_t usedLast = 0;

    ClearSelection(currentSelection);
    ClearSelection(usedEntries);

    for (int i = 0; i < tokenCount; i++)
    {
        const char* token = tokens[i];
        bool selection[256];

        if (ParseSelectionToken(token, selection))
        {
            memcpy(currentSelection, selection, 256 * sizeof(bool));
            explicitSelection = true;
            currentIndex = FirstSelectedEntry(currentSelection);
            currentFileSaved = false;
            continue;
        }

        if (IsPropertyToken(token))
        {
            for (int n = 0; n < 256; n++)
            {
                if (!currentSelection[n])
                    continue;
                usedEntries[n] = true;
                if (!found)
                {
                    usedFirst = usedLast = (uint8_t)n;
                    found = true;
                }
                else
                {
                    if (n < usedFirst) usedFirst = (uint8_t)n;
                    if (n > usedLast) usedLast = (uint8_t)n;
                }
            }
            continue;
        }

        if (IsTargetedFileToken(token) || IsImageToken(token) || IsAudioToken(token))
        {
            int targetIndex = -1;
            if (!InferTargetIndexForToken(token, currentSelection, explicitSelection, currentIndex, currentFileSaved, usedEntries, &targetIndex))
                continue;
            usedEntries[targetIndex] = true;
            if (!found)
            {
                usedFirst = usedLast = (uint8_t)targetIndex;
                found = true;
            }
            else
            {
                if (targetIndex < usedFirst) usedFirst = (uint8_t)targetIndex;
                if (targetIndex > usedLast) usedLast = (uint8_t)targetIndex;
            }
            SelectSingleEntry(currentSelection, targetIndex);
            explicitSelection = false;
            currentIndex = targetIndex;
            currentFileSaved = true;
        }
    }

    if (!found)
        return false;
    *first = usedFirst;
    *last = usedLast;
    return true;
}

static bool GetUsedDAT5Range(DMG* dmg, uint8_t* first, uint8_t* last)
{
    bool found = false;
    uint8_t usedFirst = 255;
    uint8_t usedLast = 0;

    if (dmg == 0 || dmg->version != DMG_Version5)
        return false;

    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (!found)
        {
            usedFirst = (uint8_t)n;
            usedLast = (uint8_t)n;
            found = true;
        }
        else
        {
            if (n < usedFirst) usedFirst = (uint8_t)n;
            if (n > usedLast) usedLast = (uint8_t)n;
        }
    }

    if (!found)
        return false;
    if (first) *first = usedFirst;
    if (last) *last = usedLast;
    return true;
}

static bool GetUsedEntryRange(DMG* dmg, uint8_t* first, uint8_t* last)
{
    bool found = false;
    uint8_t usedFirst = 255;
    uint8_t usedLast = 0;

    if (dmg == 0)
        return false;

    int start = dmg->version == DMG_Version5 ? dmg->firstEntry : 0;
    int end = dmg->version == DMG_Version5 ? dmg->lastEntry : 255;

    for (int n = start; n <= end; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry == 0 || entry->type == DMGEntry_Empty)
            continue;
        if (!found)
        {
            usedFirst = (uint8_t)n;
            usedLast = (uint8_t)n;
            found = true;
        }
        else
        {
            if (n < usedFirst) usedFirst = (uint8_t)n;
            if (n > usedLast) usedLast = (uint8_t)n;
        }
    }

    if (!found)
        return false;
    if (first) *first = usedFirst;
    if (last) *last = usedLast;
    return true;
}

static bool EncodeDAT5Image(DMG_DAT5ColorMode mode, const uint8_t* indexed, uint16_t width, uint16_t height, uint8_t* output, uint32_t* outputSize)
{
    switch (mode)
    {
        case DMG_DAT5_COLORMODE_CGA:
            *outputSize = ((uint32_t)width * height + 3) / 4;
            return DMG_PackChunkyPixels(indexed, width, height, 2, output);
        case DMG_DAT5_COLORMODE_EGA:
            *outputSize = ((uint32_t)width * height + 1) / 2;
            return DMG_PackChunkyPixels(indexed, width, height, 4, output);
        case DMG_DAT5_COLORMODE_PLANAR4:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 4 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 4, output);
        case DMG_DAT5_COLORMODE_PLANAR5:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 5 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 5, output);
        case DMG_DAT5_COLORMODE_PLANAR8:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 8 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 8, output);
        case DMG_DAT5_COLORMODE_PLANAR4ST:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 4 * 2;
            return DMG_PackBitplaneWords(indexed, width, height, 4, output);
        case DMG_DAT5_COLORMODE_PLANAR8ST:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 8 * 2;
            return DMG_PackBitplaneWords(indexed, width, height, 8, output);
        default:
            return false;
    }
}

static uint32_t GetDAT5EncodedSize(DMG_DAT5ColorMode mode, uint16_t width, uint16_t height)
{
    switch (mode)
    {
        case DMG_DAT5_COLORMODE_CGA:
            return ((uint32_t)width * height + 3) / 4;
        case DMG_DAT5_COLORMODE_EGA:
            return ((uint32_t)width * height + 1) / 2;
        case DMG_DAT5_COLORMODE_PLANAR4:
        case DMG_DAT5_COLORMODE_PLANAR4ST:
            return ((uint32_t)(width + 15) >> 4) * height * 4 * 2;
        case DMG_DAT5_COLORMODE_PLANAR5:
            return ((uint32_t)(width + 15) >> 4) * height * 5 * 2;
        case DMG_DAT5_COLORMODE_PLANAR8:
        case DMG_DAT5_COLORMODE_PLANAR8ST:
            return ((uint32_t)(width + 15) >> 4) * height * 8 * 2;
        default:
            return 0;
    }
}

static uint32_t PaletteBrightness(uint32_t color)
{
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;
    return 299 * r + 587 * g + 114 * b;
}

static uint8_t GetMaxPixelIndex(const uint8_t* pixels, uint32_t pixelCount)
{
    uint8_t maxIndex = 0;
    for (uint32_t i = 0; i < pixelCount; i++)
    {
        if (pixels[i] > maxIndex)
            maxIndex = pixels[i];
    }
    return maxIndex;
}

static void RemapPaletteWithOrder(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int paletteSize, const uint16_t* order)
{
    uint8_t remap[256];
    uint32_t reordered[256];

    for (int i = 0; i < paletteSize; i++)
    {
        reordered[i] = palette[order[i]];
        remap[order[i]] = (uint8_t)i;
    }

    for (int i = 0; i < paletteSize; i++)
        palette[i] = reordered[i];

    for (uint32_t i = 0; i < pixelCount; i++)
        pixels[i] = remap[pixels[i]];
}

static void ReindexPaletteStd(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int paletteSize)
{
    if (paletteSize <= 1)
        return;

    uint16_t order[256];
    bool usedSlot[256];
    uint16_t finalOrder[256];
    int brightestSlot = paletteSize >= 16 ? 15 : (paletteSize - 1);

    for (int i = 0; i < paletteSize; i++)
        order[i] = (uint16_t)i;

    for (int i = 0; i < paletteSize - 1; i++)
    {
        int best = i;
        uint32_t bestBrightness = PaletteBrightness(palette[order[i]]);
        for (int j = i + 1; j < paletteSize; j++)
        {
            uint32_t brightness = PaletteBrightness(palette[order[j]]);
            if (brightness < bestBrightness ||
                (brightness == bestBrightness && order[j] < order[best]))
            {
                best = j;
                bestBrightness = brightness;
            }
        }
        if (best != i)
        {
            uint16_t tmp = order[i];
            order[i] = order[best];
            order[best] = tmp;
        }
    }

    for (int i = 0; i < paletteSize; i++)
        usedSlot[i] = false;

    finalOrder[0] = order[0];
    usedSlot[0] = true;

    finalOrder[brightestSlot] = order[paletteSize - 1];
    usedSlot[brightestSlot] = true;

    int source = paletteSize - 2;
    for (int slot = 1; slot < paletteSize && source > 0; slot++)
    {
        if (usedSlot[slot])
            continue;
        finalOrder[slot] = order[source];
        usedSlot[slot] = true;
        source--;
    }

    while (source > 0)
    {
        for (int slot = 1; slot < paletteSize && source > 0; slot++)
        {
            if (usedSlot[slot])
                continue;
            finalOrder[slot] = order[source];
            usedSlot[slot] = true;
            source--;
        }
    }

    RemapPaletteWithOrder(pixels, pixelCount, palette, paletteSize, finalOrder);
}

static void ReindexPaletteDark(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int paletteSize)
{
    if (paletteSize <= 1)
        return;

    uint16_t order[256];
    for (int i = 0; i < paletteSize; i++)
        order[i] = (uint16_t)i;

    for (int i = 0; i < paletteSize - 1; i++)
    {
        int best = i;
        uint32_t bestBrightness = PaletteBrightness(palette[order[i]]);
        for (int j = i + 1; j < paletteSize; j++)
        {
            uint32_t brightness = PaletteBrightness(palette[order[j]]);
            if (brightness < bestBrightness ||
                (brightness == bestBrightness && order[j] < order[best]))
            {
                best = j;
                bestBrightness = brightness;
            }
        }
        if (best != i)
        {
            uint16_t tmp = order[i];
            order[i] = order[best];
            order[best] = tmp;
        }
    }

    RemapPaletteWithOrder(pixels, pixelCount, palette, paletteSize, order);
}

static void ReindexPaletteMinimal(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int paletteSize)
{
    if (paletteSize <= 1)
        return;

    int darkest = 0;
    int brightest = 0;
    uint32_t darkestBrightness = PaletteBrightness(palette[0]);
    uint32_t brightestBrightness = darkestBrightness;

    for (int i = 1; i < paletteSize; i++)
    {
        uint32_t brightness = PaletteBrightness(palette[i]);
        if (brightness < darkestBrightness ||
            (brightness == darkestBrightness && i < darkest))
        {
            darkest = i;
            darkestBrightness = brightness;
        }
        if (brightness > brightestBrightness ||
            (brightness == brightestBrightness && i < brightest))
        {
            brightest = i;
            brightestBrightness = brightness;
        }
    }

    int brightestSlot = paletteSize >= 16 ? 15 : (paletteSize - 1);
    uint16_t order[256];
    uint8_t indexAtSlot[256];
    for (int i = 0; i < paletteSize; i++)
    {
        order[i] = (uint16_t)i;
        indexAtSlot[i] = (uint8_t)i;
    }

    if (darkest != 0)
    {
        uint8_t displaced = indexAtSlot[0];
        order[0] = (uint16_t)darkest;
        order[darkest] = displaced;
        indexAtSlot[0] = (uint8_t)darkest;
        indexAtSlot[darkest] = displaced;
    }

    if (brightestSlot >= 0 && brightestSlot < paletteSize)
    {
        int currentBrightestSlot = brightest;
        for (int slot = 0; slot < paletteSize; slot++)
        {
            if (order[slot] == (uint16_t)brightest)
            {
                currentBrightestSlot = slot;
                break;
            }
        }
        if (currentBrightestSlot != brightestSlot)
        {
            uint16_t tmp = order[brightestSlot];
            order[brightestSlot] = (uint16_t)brightest;
            order[currentBrightestSlot] = tmp;
        }
    }

    RemapPaletteWithOrder(pixels, pixelCount, palette, paletteSize, order);
}

static void ReindexPaletteReserve(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int* paletteSize, int paletteLimit, int* firstColor, int* lastColor)
{
    if (paletteSize == 0 || *paletteSize <= 0 || *paletteSize >= paletteLimit)
    {
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = *paletteSize > 0 ? *paletteSize - 1 : 0;
        return;
    }

    int reserve = paletteLimit - *paletteSize;
    if (reserve > 16)
        reserve = 16;
    if (reserve <= 0)
    {
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = *paletteSize > 0 ? *paletteSize - 1 : 0;
        return;
    }

    for (uint32_t i = 0; i < pixelCount; i++)
        pixels[i] = (uint8_t)(pixels[i] + reserve);
    if (firstColor) *firstColor = reserve;
    if (lastColor) *lastColor = reserve + *paletteSize - 1;
}

static void ReindexPaletteRange(uint8_t* pixels, uint32_t pixelCount, int paletteSize, int paletteLimit, int* firstColor, int* lastColor)
{
    if (paletteSize <= 0)
    {
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = 0;
        return;
    }

    int effectiveFirst = remapRangeFirst;
    int effectiveLast = remapRangeLast;
    if (effectiveFirst >= paletteLimit)
    {
        ShowWarning("Requested remap range starts outside the DAT mode palette; keeping default palette placement");
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = paletteSize - 1;
        return;
    }
    if (effectiveLast >= paletteLimit)
    {
        ShowWarning("Requested remap range exceeds the DAT mode palette; clamping to the mode limit");
        effectiveLast = paletteLimit - 1;
    }

    int available = effectiveLast - effectiveFirst + 1;
    if (available <= 0)
    {
        ShowWarning("Requested remap range is empty for this DAT mode; keeping default palette placement");
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = paletteSize - 1;
        return;
    }

    if (paletteSize > available)
    {
        ShowWarning("Image palette does not fit in requested remap range; keeping default palette placement");
        if (firstColor) *firstColor = 0;
        if (lastColor) *lastColor = paletteSize - 1;
        return;
    }

    for (uint32_t i = 0; i < pixelCount; i++)
        pixels[i] = (uint8_t)(pixels[i] + effectiveFirst);
    if (firstColor) *firstColor = effectiveFirst;
    if (lastColor) *lastColor = effectiveFirst + paletteSize - 1;
}

static void ApplyPaletteRemap(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, int* paletteSize, int paletteLimit, int* firstColor, int* lastColor)
{
    if (firstColor) *firstColor = 0;
    if (lastColor) *lastColor = *paletteSize > 0 ? *paletteSize - 1 : 0;
    switch (remapMode)
    {
        case REMAP_MIN:
            ReindexPaletteMinimal(pixels, pixelCount, palette, *paletteSize);
            break;
        case REMAP_RESERVE:
            ReindexPaletteReserve(pixels, pixelCount, palette, paletteSize, paletteLimit, firstColor, lastColor);
            break;
        case REMAP_STD:
            ReindexPaletteStd(pixels, pixelCount, palette, *paletteSize);
            break;
        case REMAP_DARK:
            ReindexPaletteDark(pixels, pixelCount, palette, *paletteSize);
            break;
        case REMAP_RANGE:
            ReindexPaletteRange(pixels, pixelCount, *paletteSize, paletteLimit, firstColor, lastColor);
            break;
        default:
            break;
    }
}

static bool CompressDAT5Image(const uint8_t* input, uint32_t inputSize, uint8_t** output, uint32_t* outputSize, bool* compressed)
{
    *compressed = false;
    *output = (uint8_t*)input;
    *outputSize = inputSize;

    uint32_t zx0Size = 0;
    uint8_t* zx0Data = DMG_CompressZX0(input, inputSize, &zx0Size);
    if (zx0Data == 0)
        return true;

    uint32_t savings = inputSize > zx0Size ? inputSize - zx0Size : 0;
    uint32_t minimumSavings = DAT5_MIN_ZX0_SAVINGS_BYTES;
    uint32_t percentSavings = (inputSize * DAT5_MIN_ZX0_SAVINGS_PERCENT + 99) / 100;
    if (percentSavings > minimumSavings)
        minimumSavings = percentSavings;

    if (zx0Size < inputSize && savings >= minimumSavings)
    {
        *compressed = true;
        *output = zx0Data;
        *outputSize = zx0Size;
        return true;
    }

    Free(zx0Data);
    return true;
}

static bool SaveWAV (const char* filename, uint8_t* data, size_t size, DMG_KHZ sampleRate)
{
	struct WAVHeader wav;
	File* file = File_Create(filename);
	uint8_t* ptr;

	if (file == NULL)
		return false;

	// Some samples have 0 padding at the end which produces nasty clicks
	ptr = data + size - 1;
	while (ptr > data && *ptr == 0)
		ptr--;
	size = ptr - data + 1;

	memcpy(wav.riff, "RIFF", 4);
	wav.fileSize = 36 + size;
	memcpy(wav.wave, "WAVE", 4);
	memcpy(wav.fmt0, "fmt ", 4);
	wav.fmtSize = 16;
	wav.format = 1;
	wav.channels = 1;
	switch (sampleRate)
	{
		case DMG_5KHZ:    wav.samplesPerSec =  5000; break;
		case DMG_7KHZ:    wav.samplesPerSec =  7000; break;
		case DMG_9_5KHZ:  wav.samplesPerSec =  9500; break;
		case DMG_15KHZ:   wav.samplesPerSec = 15000; break;
		case DMG_20KHZ:   wav.samplesPerSec = 20000; break;
		case DMG_30KHZ:   wav.samplesPerSec = 30000; break;
        case DMG_44_1KHZ: wav.samplesPerSec = 44100; break;
        case DMG_48KHZ:   wav.samplesPerSec = 48000; break;
		default:          wav.samplesPerSec = 11025; break;
	}
	wav.avgBytesPerSec = wav.samplesPerSec;
	wav.blockAlign = 1;
	wav.bitsPerSample = 8;
	memcpy(wav.data, "data", 4);
	wav.dataSize = size;

	if (File_Write(file, &wav, sizeof(wav)) != sizeof(wav))
	{
		File_Close(file);
		return false;
	}
	if (File_Write(file, data, size) != size)
	{
		File_Close(file);
		return false;
	}
	File_Close(file);
	return true;
}

static bool SaveExtractSidecarJSON(DMG* dmg, uint8_t index, const char* outputFileName, const DMG_Entry* entry)
{
    char jsonFileName[FILE_MAX_PATH];
    char json[256];
    int written = 0;
    bool wroteAny = false;
    int cloneSource = -1;
    File* file;

    if (outputFileName == 0 || entry == 0)
        return false;

    BuildSidecarPath(outputFileName, ".json", jsonFileName, sizeof(jsonFileName));

    if (entry->type != DMGEntry_Image)
    {
        return false;
    }

    written = snprintf(json, sizeof(json), "{\n");
    if (written <= 0 || written >= (int)sizeof(json))
        return false;

    if (dmg != 0)
    {
        for (int i = 0; i < index; i++)
        {
            DMG_Entry* other = DMG_GetEntry(dmg, (uint8_t)i);
            if (other == 0 || other->type != entry->type)
                continue;
            if (other->fileOffset == entry->fileOffset && other->length == entry->length)
            {
                cloneSource = i;
                break;
            }
        }
    }

    if (cloneSource >= 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written,
            "  \"clone\": 1,\n"
            "  \"location\": %d,\n",
            cloneSource);
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if (entry->x != 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written, "  \"X\": %d,\n", entry->x);
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if (entry->y != 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written, "  \"Y\": %d,\n", entry->y);
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if ((entry->flags & DMG_FLAG_FIXED) == 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written, "  \"float\": 1,\n");
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if ((entry->flags & DMG_FLAG_BUFFERED) != 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written, "  \"buffer\": 1,\n");
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if ((entry->flags & DMG_FLAG_COMPRESSED) == 0)
    {
        int amount = snprintf(json + written, sizeof(json) - written, "  \"compress\": 0,\n");
        if (amount <= 0 || amount >= (int)(sizeof(json) - written))
            return false;
        written += amount;
        wroteAny = true;
    }

    if (!wroteAny)
    {
        remove(jsonFileName);
        return true;
    }

    written -= 2;
    int amount = snprintf(json + written, sizeof(json) - written, "\n}\n");
    if (amount <= 0 || amount >= (int)(sizeof(json) - written))
        return false;
    written += amount;

    if (written <= 0 || written >= (int)sizeof(json))
        return false;

    file = File_Create(jsonFileName);
    if (file == 0)
        return false;
    bool ok = File_Write(file, json, (uint32_t)written) == (uint32_t)written;
    File_Close(file);
    return ok;
}

static uint16_t ReadLE16(const uint8_t* ptr)
{
    return (uint16_t)ptr[0] | (uint16_t)(ptr[1] << 8);
}

static uint32_t ReadLE32(const uint8_t* ptr)
{
    return (uint32_t)ptr[0] |
        ((uint32_t)ptr[1] << 8) |
        ((uint32_t)ptr[2] << 16) |
        ((uint32_t)ptr[3] << 24);
}

static bool TryMapWaveSampleRate(uint32_t samplesPerSec, DMG_KHZ* freq)
{
    for (int i = 0; frequencies[i].value != 0; i++)
    {
        if ((uint32_t)frequencies[i].value == samplesPerSec)
        {
            *freq = frequencies[i].freq;
            return true;
        }
    }
    return false;
}

static bool LoadWAVFile(const char* fileName, uint8_t** ioBuffer, uint32_t* ioBufferSize, uint32_t* sampleDataSize, DMG_KHZ* sampleRate)
{
    File* file = File_Open(fileName, ReadOnly);
    if (file == 0)
    {
        DMG_SetError(DMG_ERROR_FILE_NOT_FOUND);
        return false;
    }

    uint64_t fileSize64 = File_GetSize(file);
    if (fileSize64 < 44 || fileSize64 > 0xFFFFFFFFu)
    {
        File_Close(file);
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    uint32_t fileSize = (uint32_t)fileSize64;
    if (*ioBufferSize < fileSize)
    {
        uint8_t* resized = (uint8_t*)realloc(*ioBuffer, fileSize);
        if (resized == 0)
        {
            File_Close(file);
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            return false;
        }
        *ioBuffer = resized;
        *ioBufferSize = fileSize;
    }

    if (File_Read(file, *ioBuffer, fileSize) != fileSize)
    {
        File_Close(file);
        DMG_SetError(DMG_ERROR_READING_FILE);
        return false;
    }
    File_Close(file);

    uint8_t* fileData = *ioBuffer;
    if (memcmp(fileData + 0, "RIFF", 4) != 0 || memcmp(fileData + 8, "WAVE", 4) != 0)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    bool haveFmt = false;
    bool haveData = false;
    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t blockAlign = 0;
    uint32_t samplesPerSec = 0;
    uint8_t* dataChunk = 0;
    uint32_t dataChunkSize = 0;

    uint32_t offset = 12;
    while (offset + 8 <= fileSize)
    {
        const uint8_t* chunk = fileData + offset;
        uint32_t chunkSize = ReadLE32(chunk + 4);
        offset += 8;
        if (chunkSize > fileSize - offset)
        {
            DMG_SetError(DMG_ERROR_INVALID_IMAGE);
            return false;
        }

        if (memcmp(chunk + 0, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
            {
                DMG_SetError(DMG_ERROR_INVALID_IMAGE);
                return false;
            }
            format = ReadLE16(fileData + offset + 0);
            channels = ReadLE16(fileData + offset + 2);
            samplesPerSec = ReadLE32(fileData + offset + 4);
            blockAlign = ReadLE16(fileData + offset + 12);
            bitsPerSample = ReadLE16(fileData + offset + 14);
            haveFmt = true;
        }
        else if (memcmp(chunk + 0, "data", 4) == 0)
        {
            dataChunk = fileData + offset;
            dataChunkSize = chunkSize;
            haveData = true;
        }

        offset += chunkSize + (chunkSize & 1u);
    }

    if (!haveFmt || !haveData || dataChunk == 0)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }
    if (format != 1 || (channels != 1 && channels != 2) || (bitsPerSample != 8 && bitsPerSample != 16))
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }
    if (blockAlign == 0 || dataChunkSize < blockAlign)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }
    if (!TryMapWaveSampleRate(samplesPerSec, sampleRate))
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    uint32_t sampleCount = dataChunkSize / blockAlign;
    if (sampleCount == 0 || sampleCount > 0xFFFFu)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    uint8_t* output = *ioBuffer;
    if (channels == 1 && bitsPerSample == 8)
    {
        if (dataChunk != output)
            MemMove(output, dataChunk, sampleCount);
    }
    else if (channels == 1 && bitsPerSample == 16)
    {
        for (uint32_t i = 0; i < sampleCount; i++)
        {
            int16_t sample = (int16_t)ReadLE16(dataChunk + i * 2);
            output[i] = (uint8_t)(((int32_t)sample + 32768) >> 8);
        }
    }
    else if (channels == 2 && bitsPerSample == 8)
    {
        for (uint32_t i = 0; i < sampleCount; i++)
        {
            uint32_t left = dataChunk[i * 2 + 0];
            uint32_t right = dataChunk[i * 2 + 1];
            output[i] = (uint8_t)((left + right) >> 1);
        }
    }
    else
    {
        for (uint32_t i = 0; i < sampleCount; i++)
        {
            int16_t left = (int16_t)ReadLE16(dataChunk + i * 4 + 0);
            int16_t right = (int16_t)ReadLE16(dataChunk + i * 4 + 2);
            int32_t mixed = ((int32_t)left + (int32_t)right) / 2;
            output[i] = (uint8_t)((mixed + 32768) >> 8);
        }
    }

    *sampleDataSize = sampleCount;
    return true;
}

static bool IsExtractOutputToken(const char* token)
{
    return strnicmp(token, "out:", 4) == 0;
}

const char* MakeFileName(const char* outputDirectory, int index, const char* extension)
{
    const char* directory = outputDirectory;
    if (directory == 0 || directory[0] == 0)
        directory = ".";

    StrCopy(newfilename, sizeof(newfilename), directory);
    char* ptr = newfilename + strlen(newfilename);
    if (ptr > newfilename && ptr[-1] != '\\' && ptr[-1] != '/')
    {
        if ((size_t)(ptr - newfilename) + 1 < sizeof(newfilename))
        {
            *ptr++ = '/';
            *ptr = 0;
        }
    }
    snprintf(ptr, newfilename + sizeof(newfilename) - ptr, "%03d.%s", index, extension);
	return newfilename;
}

static void ClearSelection(bool* entries)
{
    MemClear(entries, 256 * sizeof(bool));
}

static int CountSelectedEntries(const bool* entries)
{
    int count = 0;
    for (int i = 0; i < 256; i++)
    {
        if (entries[i])
            count++;
    }
    return count;
}

static int FirstSelectedEntry(const bool* entries)
{
    for (int i = 0; i < 256; i++)
    {
        if (entries[i])
            return i;
    }
    return -1;
}

static void SelectSingleEntry(bool* entries, int index)
{
    ClearSelection(entries);
    if (index >= 0 && index < 256)
        entries[index] = true;
}

static bool ParseSelectionToken(const char* token, bool* entries)
{
    const char* ptr = token;
    ClearSelection(entries);

    while (*ptr)
    {
        if (isspace(*ptr))
        {
            ptr++;
            continue;
        }
        if (!isdigit(*ptr))
            return false;

        int index = 0;
        while (*ptr >= '0' && *ptr <= '9')
            index = index * 10 + *ptr++ - '0';
        if (index < 0 || index >= 256)
            return false;

        if (*ptr == '-')
        {
            ptr++;
            if (*ptr == 0)
            {
                while (index < 256)
                    entries[index++] = true;
                break;
            }
            if (!isdigit(*ptr))
                return false;

            int index2 = 0;
            while (*ptr >= '0' && *ptr <= '9')
                index2 = index2 * 10 + *ptr++ - '0';
            if (index2 < 0 || index2 >= 256 || index2 < index)
                return false;
            while (index <= index2)
                entries[index++] = true;
        }
        else
        {
            entries[index] = true;
        }

        if (*ptr == ',')
        {
            ptr++;
            continue;
        }
        if (*ptr != 0 && !isspace(*ptr))
            return false;
    }

    return CountSelectedEntries(entries) > 0;
}

static bool IsSelectionToken(const char* token)
{
    bool temp[256];
    return ParseSelectionToken(token, temp);
}

static bool ParseEntrySelectionList(int tokenCount, const char* tokens[])
{
    ClearSelection(selected);
    extractMode = ImageMode_Indexed;
    extractFormat = ExtractFormat_PNG;
    extractOutputDirectory[0] = 0;
    extractJsonSidecar = true;

    if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES || action == ACTION_TEST) && !OS_GetCurrentDirectory(extractOutputDirectory, sizeof(extractOutputDirectory)))
        StrCopy(extractOutputDirectory, sizeof(extractOutputDirectory), ".");

    for (int i = 0; i < tokenCount; i++)
    {
        const char* ptr = tokens[i];
        if (ptr == 0 || *ptr == 0)
            continue;

        if (IsSelectionToken(ptr))
        {
            bool temp[256];
            ParseSelectionToken(ptr, temp);
            for (int n = 0; n < 256; n++)
            {
                if (temp[n])
                    selected[n] = true;
            }
            continue;
        }

        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES || action == ACTION_TEST) && IsExtractOutputToken(ptr))
        {
            const char* value = ptr + 4;
            if (*value == 0)
            {
                fprintf(stderr, "Error: Missing output directory in argument: \"%s\"\n", tokens[i]);
                return false;
            }
            StrCopy(extractOutputDirectory, sizeof(extractOutputDirectory), value);
            continue;
        }

        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && stricmp(ptr, "set-json:off") == 0)
        {
            extractJsonSidecar = false;
            continue;
        }

        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && stricmp(ptr, "set-json:auto") == 0)
        {
            extractJsonSidecar = true;
            continue;
        }

        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && strnicmp(ptr, "set-json-file:", 14) == 0)
        {
            fprintf(stderr, "Error: Extract only supports -json auto/off or -nojson\n");
            return false;
        }

        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && strnicmp(ptr, "--as=", 5) == 0)
        {
            const char* value = ptr + 5;
            if (stricmp(value, "png") == 0) extractFormat = ExtractFormat_PNG;
            else if (stricmp(value, "pi1") == 0) extractFormat = ExtractFormat_PI1;
            else if (stricmp(value, "pl1") == 0) extractFormat = ExtractFormat_PL1;
            else if (stricmp(value, "iff") == 0 || stricmp(value, "ilbm") == 0) extractFormat = ExtractFormat_IFF;
            else
            {
                fprintf(stderr, "Error: Invalid extract format: \"%s\"\n", value);
                return false;
            }
            continue;
        }

        if (*ptr == '-')
        {
            ptr++;
            while (*ptr != 0)
            {
                if (action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES)
                {
                    switch(tolower(*ptr))
                    {
                        case 'c': extractMode = (DMG_ImageMode)((extractMode & 0xF0) | 0x01); break;
                        case 'e': extractMode = (DMG_ImageMode)((extractMode & 0xF0) | 0x02); break;
                        case 'i': extractMode = (DMG_ImageMode)((extractMode & 0x0F) | 0x40); break;
                        case 't': extractMode = (DMG_ImageMode)((extractMode & 0x0F) | 0x10); break;
                        default:
                            fprintf(stderr, "Error: Invalid option: \"%c\"\n", *ptr);
                            return false;
                    }
                }
                else
                {
                    fprintf(stderr, "Error: Invalid option: \"%c\"\n", *ptr);
                    return false;
                }
                ptr++;
            }
            continue;
        }

        fprintf(stderr, "Error: Invalid argument: \"%s\"\n", tokens[i]);
        return false;
    }

    if (CountSelectedEntries(selected) == 0)
        memset(selected, 1, sizeof(selected));
    return true;
}

static void ExtractSelectedEntries(DMG* dmg, bool saveToFile, bool paletteOnly)
{
    const char* outputFileName;
    int n;
    bool success;

    for (n = 0; n < 256; n++)
    {
        if (selected[n])
        {
            DMG_Entry* entry = DMG_GetEntry(dmg, n);
            size_t size;
            uint32_t* palette;
            uint32_t expandedPalette[256];

            if (entry == NULL)
            {
                if (DMG_GetError() != DMG_ERROR_NONE)
                    fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
                continue;
            }
            switch (entry->type)
            {
                case DMGEntry_Image:
                {
                    size = entry->width * entry->height * 4;
                    if (size == 0)
                        continue;
                    uint8_t* buffer = DMG_GetEntryData(dmg, n, extractMode);
                    if (buffer == 0)
                    {
                        fprintf(stderr, "%03d: Error: Unable to read image entry: %s\n", n, DMG_GetErrorString());
                        continue;
                    }
                    if (saveToFile && paletteOnly)
                    {
                        outputFileName = MakeFileName(extractOutputDirectory, n, "col");
                        palette = (uint32_t*)DMG_GetEntryStoredPalette(dmg, n);
                        int paletteSize = DMG_GetEntryPaletteSize(dmg, n);
                        success = SaveCOLPalette(outputFileName, palette, paletteSize);
                        if (!success)
                        {
                            fprintf(stderr, "%03d: Error: Unable to write palette to %s\n", n, outputFileName);
                            continue;
                        }
                        printf("%s saved\n", outputFileName);
                    }
                    else if (saveToFile)
                    {
                        const char* extension = "png";
                        uint32_t* exportPalette = 0;
                        int exportPaletteSize = 0;
                        if (extractFormat == ExtractFormat_PI1) extension = "pi1";
                        else if (extractFormat == ExtractFormat_PL1) extension = "pl1";
                        else if (extractFormat == ExtractFormat_IFF) extension = "iff";
                        outputFileName = MakeFileName(extractOutputDirectory, n, extension);
                        palette = DMG_GetEntryStoredPalette(dmg, n);
                        int paletteSize = DMG_GetEntryPaletteSize(dmg, n);
                        PrepareIndexedExportPalette(dmg, entry, palette, paletteSize, expandedPalette, &exportPalette, &exportPaletteSize);
                        if (extractFormat == ExtractFormat_PI1 || extractFormat == ExtractFormat_PL1)
                            success = SavePI1Indexed(outputFileName, buffer, entry->width, entry->height, exportPalette, exportPaletteSize);
                        else if (extractFormat == ExtractFormat_IFF)
                            success = SaveIFFIndexed(outputFileName, buffer, entry->width, entry->height, exportPalette, exportPaletteSize);
                        else if (DMG_IS_INDEXED(extractMode))
                            success = SavePNGIndexed(outputFileName, buffer, entry->width, entry->height, exportPalette, exportPaletteSize, 0);
                        else
                            success = SavePNGRGB32(outputFileName, (uint32_t*)buffer, entry->width, entry->height);
                        if (!success)
                        {
                            fprintf(stderr, "%03d: Error: Unable to write image to %s\n", n, outputFileName);
                            continue;
                        }
                        printf("%s saved\n", outputFileName);
                        if (extractJsonSidecar && !SaveExtractSidecarJSON(dmg, (uint8_t)n, outputFileName, entry))
                            fprintf(stderr, "%03d: Warning: Unable to write sidecar JSON for %s\n", n, outputFileName);
                    }
                    else
                    {
                        printf("%03d: (%dx%d image, %s) %d bytes ok.\n", n, entry->width, entry->height,
                               (entry->flags & DMG_FLAG_COMPRESSED) ? "compressed" : "uncompressed", entry->length);
                    }
                    break;
                }

                case DMGEntry_Audio:
                {
                    uint8_t* buffer = DMG_GetEntryData(dmg, n, ImageMode_Audio);
                    if (buffer == 0)
                    {
                        fprintf(stderr, "%03d: Error: Unable to read audio entry: %s\n", n, DMG_GetErrorString());
                        continue;
                    }
                    if (saveToFile)
                    {
                        outputFileName = MakeFileName(extractOutputDirectory, n, "wav");
                        if (!SaveWAV(outputFileName, buffer, entry->length, (DMG_KHZ)entry->x))
                        {
                            fprintf(stderr, "%03d: Error: Unable to write audio entry to %s\n", n, outputFileName);
                            continue;
                        }
                        printf("%s saved\n", outputFileName);
                    }
                    else
                    {
                        printf("%03d: %s (audio sample) %d bytes ok.\n", n, filename, entry->length);
                    }
                    break;
                }

                case DMGEntry_Empty:
                    break;
            }
        }
    }
}

static void PrepareIndexedExportPalette(DMG* dmg, const DMG_Entry* entry, uint32_t* palette, int paletteSize, uint32_t* expandedPalette, uint32_t** exportPalette, int* exportPaletteSize)
{
    *exportPalette = palette;
    *exportPaletteSize = paletteSize;

    if (dmg == 0 || entry == 0 || palette == 0 || paletteSize <= 0)
        return;
    if (dmg->version != DMG_Version5)
        return;

    int modePaletteSize = GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode);
    if (modePaletteSize <= 0 || modePaletteSize > 256)
        return;
    if (entry->firstColor == 0 && paletteSize >= modePaletteSize)
        return;
    if (entry->firstColor >= modePaletteSize)
        return;

    MemClear(expandedPalette, sizeof(uint32_t) * 256);
    int copyCount = paletteSize;
    if (copyCount > modePaletteSize - entry->firstColor)
        copyCount = modePaletteSize - entry->firstColor;
    if (copyCount > 0)
        MemCopy(expandedPalette + entry->firstColor, palette, sizeof(uint32_t) * copyCount);

    *exportPalette = expandedPalette;
    *exportPaletteSize = modePaletteSize;
}

static void DeleteSelectedEntries(DMG* dmg)
{
	int n;
	int count = 0;

	for (n = 0; n < 256; n++)
	{
		if (selected[n])
		{
			if (!DMG_RemoveEntry(dmg, n))
			{
				fprintf(stderr, "%03d: Error: Unable to delete entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			count++;
			printf("%03d: deleted\n", n);
		}
	}
	if (count == 0)
		printf("No entries deleted\n");
}

static const char *DescribeVersion(DMG_Version v)
{
	switch (v)
	{
		case DMG_Version1:      return "Version1";
		case DMG_Version1_CGA:  return "CGA";
		case DMG_Version1_EGA:  return "EGA";
        case DMG_Version1_PCW:  return "PCW";
		case DMG_Version2:      return "Version2";
        case DMG_Version5:      return "Version5";
		default:                return "unknown DAT";
	}
}

static const char* DescribeScreenMode(DDB_ScreenMode mode)
{
    switch (mode)
    {
        case ScreenMode_Default: return "Default";
        case ScreenMode_Text:    return "Text";
        case ScreenMode_CGA:     return "CGA";
        case ScreenMode_EGA:     return "EGA";
        case ScreenMode_VGA16:   return "VGA16";
        case ScreenMode_VGA:     return "320x200x256";
        case ScreenMode_HiRes:   return "640x200x256";
        case ScreenMode_SHiRes:  return "640x400x256";
        default:                 return "Unknown";
    }
}

static const char* DescribeDAT5ColorMode(uint8_t mode)
{
    switch ((DMG_DAT5ColorMode)mode)
    {
        case DMG_DAT5_COLORMODE_CGA:       return "CGA";
        case DMG_DAT5_COLORMODE_EGA:       return "EGA";
        case DMG_DAT5_COLORMODE_PLANAR4:   return "Planar4";
        case DMG_DAT5_COLORMODE_PLANAR5:   return "Planar5";
        case DMG_DAT5_COLORMODE_PLANAR8:   return "Planar8";
        case DMG_DAT5_COLORMODE_PLANAR4ST: return "Planar4ST";
        case DMG_DAT5_COLORMODE_PLANAR8ST: return "Planar8ST";
        default:                           return "Unknown";
    }
}

static uint32_t GetCompressedImageBaseline(DMG_Entry* entry)
{
    if (entry->type != DMGEntry_Image)
        return 0;

    if (dmg != 0 && dmg->version == DMG_Version5)
    {
        switch ((DMG_DAT5ColorMode)dmg->colorMode)
        {
            case DMG_DAT5_COLORMODE_CGA:
            case DMG_DAT5_COLORMODE_EGA:
            case DMG_DAT5_COLORMODE_PLANAR4:
            case DMG_DAT5_COLORMODE_PLANAR5:
            case DMG_DAT5_COLORMODE_PLANAR8:
            case DMG_DAT5_COLORMODE_PLANAR4ST:
            case DMG_DAT5_COLORMODE_PLANAR8ST:
                return DMG_DAT5StoredImageSize(dmg->colorMode, entry->width, entry->height);
            default:
                break;
        }
    }

    return DMG_CalculateRequiredSize(entry, ImageMode_Raw);
}

static void PrintCompressionGain(DMG_Entry* entry)
{
    if ((entry->flags & DMG_FLAG_COMPRESSED) == 0)
        return;

    uint32_t rawSize = GetCompressedImageBaseline(entry);
    if (rawSize == 0 || entry->length >= rawSize)
        return;

    uint32_t keptTimes10 = (entry->length * 1000 + rawSize / 2) / rawSize;
    printf(" (%u.%u%% size)", keptTimes10 / 10, keptTimes10 % 10);
}

static void ListSelectedEntries(DMG* dmg, bool verbose)
{
	int n, i;
    uint8_t order[256];
    int orderCount = 0;
    DMG_Entry* entryRefs[256];

	printf ("%s, %s, %s\n", DescribeScreenMode((DDB_ScreenMode)dmg->screenMode), DescribeVersion(dmg->version),
		dmg->littleEndian ? "little endian" : "big endian");
    if (dmg->version == DMG_Version5)
        printf ("     DAT5 %s, target %dx%d, entries %u-%u\n", DescribeDAT5ColorMode(dmg->colorMode), dmg->targetWidth, dmg->targetHeight, dmg->firstEntry, dmg->lastEntry);

	for (n = 0; n < 256; n++)
	{
		if (!selected[n])
            continue;
        DMG_Entry* entry = DMG_GetEntry(dmg, n);
        if (entry == NULL)
        {
            if (DMG_GetError() != DMG_ERROR_NONE)
                fprintf(stderr, "Error: Unable to read entry %d: %s\n", n, DMG_GetErrorString());
            continue;
        }
        if (entry->type == DMGEntry_Empty)
            continue;
        order[orderCount] = (uint8_t)n;
        entryRefs[orderCount] = entry;
        orderCount++;
	}

    if (!listSortById)
    {
        for (int a = 0; a < orderCount - 1; a++)
        {
            for (int b = a + 1; b < orderCount; b++)
            {
                uint32_t oa = entryRefs[a]->fileOffset;
                uint32_t ob = entryRefs[b]->fileOffset;
                if (ob < oa || (ob == oa && order[b] < order[a]))
                {
                    uint8_t ti = order[a];
                    order[a] = order[b];
                    order[b] = ti;
                    DMG_Entry* te = entryRefs[a];
                    entryRefs[a] = entryRefs[b];
                    entryRefs[b] = te;
                }
            }
        }
    }

    for (int p = 0; p < orderCount; p++)
    {
        n = order[p];
        DMG_Entry* entry = entryRefs[p];
        switch (entry->type)
        {
            case DMGEntry_Image:
                if (entry->width * entry->height == 0 || entry->length == 0)
                    continue;
                printf("%03d: Image %3dx%-3d %s %s X:%-4dY:%-4d C:%03d-%03d  %5d",
                    n, entry->width, entry->height,
                    (entry->flags & DMG_FLAG_BUFFERED)   ? "+buf":"    ",
                    (entry->flags & DMG_FLAG_FIXED)      ? "fix":"flt",
                    entry->x, entry->y, entry->firstColor, entry->lastColor, entry->length);
                PrintCompressionGain(entry);
                printf("\n");
                if (verbose)
                {
					uint32_t* storedPalette = DMG_GetEntryStoredPalette(dmg, n);
					uint16_t storedPaletteSize = DMG_GetEntryPaletteSize(dmg, n);
                    printf("     File offset: %08X\n", entry->fileOffset);
                    printf("     Color range:  %d-%d\n", entry->firstColor, entry->lastColor);
                    printf("     Bit depth:    %d\n", entry->bitDepth);
                    printf("     Palette size: %d\n", storedPaletteSize);
                    printf("     Palette:      ");
                    for (i = 0; i < storedPaletteSize; i++)
                    {
                        uint32_t c = storedPalette[i];
                        printf("%03X ", ((c >> 4) & 0xF) | ((c >> 8) & 0xF0) | ((c >> 12) & 0xF00));
                    }
                    printf("\n");
                    if (dmg->version != DMG_Version5)
                    {
                        const uint8_t* egaPalette = DMG_GetEntryEGAPalette(entry);
                        const uint8_t* cgaPalette = DMG_GetEntryCGAPalette(entry);
                        printf("     EGA Palette:  ");
                        for (i = 0; i < 16; i++)
                            printf(" %02d ", egaPalette != 0 ? egaPalette[i] : i);
                        printf("\n");
                        printf("     CGA Palette:  ");
                        for (i = 0; i < 4; i++)
                            printf(" %02d ", cgaPalette != 0 ? cgaPalette[i] : (i & 0x03));
                        printf (" (%s)", DMG_GetCGAMode(entry) == CGA_Blue ? "blue" : "red");
                        printf("\n");
                    }
                }
                break;

            case DMGEntry_Audio:
                printf("%03d: Audio sample   %-16s               %5d bytes\n",
                    n, DMG_DescribeFreq((DMG_KHZ)entry->x), entry->length);
                break;

            case DMGEntry_Empty:
                break;
        }
    }
}

static void RGBA32ToIndexed16 (uint32_t* pixels, int count, uint32_t* palette, uint8_t* out)
{
	static uint8_t tables[64][256];
	int n, tableCount = 1;

	memset(tables, 0, 64*256);
	for (n = 0; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		if (tables[0][r] == 0)
			tables[0][r] = tableCount++;
	}
	for (n = 0; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >> 8) & 0xFF;
		uint8_t t = tables[0][r];
		if (tables[t][g] == 0)
			tables[t][g] = tableCount++;
	}
	for (n = 0 ; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >> 8) & 0xFF;
		uint8_t b = (palette[n] >> 0) & 0xFF;
		uint8_t t = tables[0][r];
		uint8_t u = tables[t][g];
		tables[u][b] = n;
	}

	while (count-- > 0)
	{
		uint32_t* color = pixels++;
		uint8_t r = (*color >> 16) & 0xFF;
		uint8_t g = (*color >> 8) & 0xFF;
		uint8_t b = (*color >> 0) & 0xFF;
		*out++ = tables[tables[tables[0][r] & 0x1F][g] & 0x1F][b];
	}
}

static bool IsImageFileExtension(const char* extension)
{
	int n;

	for (n = 0; n < count; n++)
	{
		if (stricmp(extension, imageExtensions[n]) == 0)
			return true;
	}
	return false;
}

static bool IsAudioFileExtension(const char* extension)
{
    return stricmp(extension, "wav") == 0;
}

static bool LoadIndexedImageFile(const char* fileName, uint8_t* pixels, uint32_t pixelsBufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize, int maxColors, bool* reduced, int* sourceColorCount)
{
    const char* dot = strrchr(fileName, '.');
    if (dot == 0)
        return false;

    if (stricmp(dot + 1, "png") == 0)
        return LoadPNGIndexed(fileName, pixels, pixelsBufferSize, width, height, palette, paletteSize, 0, maxColors, reduced, sourceColorCount);

    if (reduced)
        *reduced = false;
    if (sourceColorCount)
        *sourceColorCount = 0;

    if (stricmp(dot + 1, "pi1") == 0 || stricmp(dot + 1, "pl1") == 0)
        return LoadPI1Indexed(fileName, pixels, pixelsBufferSize, width, height, palette, paletteSize);

    if (stricmp(dot + 1, "iff") == 0 || stricmp(dot + 1, "ilbm") == 0)
        return LoadIFFIndexed(fileName, pixels, pixelsBufferSize, width, height, palette, paletteSize);

#if HAS_PCX
    if (stricmp(dot + 1, "pcx") == 0 || stricmp(dot + 1, "vga") == 0)
    {
        uint16_t required = pixelsBufferSize > 0xFFFF ? 0xFFFF : (uint16_t)pixelsBufferSize;
        int w = 0;
        int h = 0;
        if (!DMG_DecompressPCX(fileName, pixels, &required, &w, &h, palette))
            return false;
        *width = (uint16_t)w;
        *height = (uint16_t)h;
        *paletteSize = 256;
        return true;
    }
#endif

    return false;
}

static int GetImportPaletteLimit(const DMG* dmg)
{
    if (dmg == 0)
        return 256;
    if (dmg->version == DMG_Version5)
        return GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode);
    if (dmg->version == DMG_Version1_CGA)
        return 4;
    if (dmg->version == DMG_Version1_PCW)
        return 2;
    return 16;
}

static int GetRequestedImportPaletteLimit(const DMG* dmg)
{
    int limit = GetImportPaletteLimit(dmg);
    if (remapMode == REMAP_RANGE)
    {
        int available = remapRangeLast - remapRangeFirst + 1;
        if (available > 0 && available < limit)
            limit = available;
    }
    return limit;
}

static bool IsImageToken(const char* token)
{
    const char* dot = strrchr(token, '.');
    return dot != 0 && IsImageFileExtension(dot + 1);
}

static bool IsAudioToken(const char* token)
{
    const char* dot = strrchr(token, '.');
    return dot != 0 && IsAudioFileExtension(dot + 1);
}

static bool IsTargetedFileToken(const char* token)
{
    const char* colon = strchr(token, ':');
    return colon != 0 && colon != token && isdigit(*token) && (IsImageToken(colon + 1) || IsAudioToken(colon + 1));
}

static bool IsPropertyToken(const char* token)
{
    for (int i = 0; i < colonOptionCount; i++)
    {
        if (strnicmp(token, colonOptions[i], strlen(colonOptions[i])) == 0)
            return true;
    }
    return false;
}

static bool ParseInputOptionToken(const char* token)
{
    int value = 0;
    if (strnicmp(token, "set-x:", 6) == 0 && ParseOptionValue(token + 6, &value))
    {
        pendingInputOptions.hasX = true;
        pendingInputOptions.x = value;
        return true;
    }
    if (strnicmp(token, "set-y:", 6) == 0 && ParseOptionValue(token + 6, &value))
    {
        pendingInputOptions.hasY = true;
        pendingInputOptions.y = value;
        return true;
    }
    if (strnicmp(token, "set-pcs:", 8) == 0 && ParseOptionValue(token + 8, &value))
    {
        pendingInputOptions.hasFirstColor = true;
        pendingInputOptions.firstColor = value;
        return true;
    }
    if (strnicmp(token, "set-pce:", 8) == 0 && ParseOptionValue(token + 8, &value))
    {
        pendingInputOptions.hasLastColor = true;
        pendingInputOptions.lastColor = value;
        return true;
    }
    if (strnicmp(token, "set-fixed:", 10) == 0 && ParseOptionValue(token + 10, &value))
    {
        pendingInputOptions.hasFixed = true;
        pendingInputOptions.fixed = value != 0;
        return true;
    }
    if (strnicmp(token, "set-buffer:", 11) == 0 && ParseOptionValue(token + 11, &value))
    {
        pendingInputOptions.hasBuffer = true;
        pendingInputOptions.buffer = value != 0;
        return true;
    }
    if (strnicmp(token, "set-amiga-hack:", 16) == 0 && ParseOptionValue(token + 16, &value))
    {
        pendingInputOptions.hasAmigaHack = true;
        pendingInputOptions.amigaHack = value != 0;
        return true;
    }
    if (strnicmp(token, "set-clone:", 10) == 0 && ParseOptionValue(token + 10, &value))
    {
        pendingInputOptions.hasClone = true;
        pendingInputOptions.cloneSource = value;
        return true;
    }
    if (stricmp(token, "set-json:off") == 0)
    {
        pendingInputOptions.useSidecarJson = false;
        pendingInputOptions.hasJsonFile = false;
        pendingInputOptions.jsonFile[0] = 0;
        return true;
    }
    if (stricmp(token, "set-json:auto") == 0)
    {
        pendingInputOptions.useSidecarJson = true;
        pendingInputOptions.hasJsonFile = false;
        pendingInputOptions.jsonFile[0] = 0;
        return true;
    }
    if (strnicmp(token, "set-json-file:", 14) == 0)
    {
        pendingInputOptions.useSidecarJson = false;
        pendingInputOptions.hasJsonFile = true;
        StrCopy(pendingInputOptions.jsonFile, sizeof(pendingInputOptions.jsonFile), token + 14);
        return true;
    }
    return false;
}

static bool IsInputOptionToken(const char* token)
{
    PendingInputOptions saved = pendingInputOptions;
    bool ok = ParseInputOptionToken(token);
    pendingInputOptions = saved;
    return ok;
}

static bool IsTrailingEntryOptionToken(const char* token)
{
    return strnicmp(token, "set-x:", 6) == 0 ||
        strnicmp(token, "set-y:", 6) == 0 ||
        strnicmp(token, "set-pcs:", 8) == 0 ||
        strnicmp(token, "set-pce:", 8) == 0 ||
        strnicmp(token, "set-fixed:", 10) == 0 ||
        strnicmp(token, "set-buffer:", 11) == 0;
}

static bool HasPendingTrailingEntryOptions()
{
    return pendingInputOptions.hasX || pendingInputOptions.hasY ||
        pendingInputOptions.hasFirstColor || pendingInputOptions.hasLastColor ||
        pendingInputOptions.hasBuffer || pendingInputOptions.hasFixed;
}

static bool HasPendingImageOnlyInputOptions()
{
    return pendingInputOptions.hasClone || pendingInputOptions.hasWidth || pendingInputOptions.hasHeight ||
        pendingInputOptions.hasAmigaHack || pendingInputOptions.hasJsonFile || !pendingInputOptions.useSidecarJson;
}

static bool HasPendingAudioUnsupportedOptions()
{
    return pendingInputOptions.hasX || pendingInputOptions.hasY ||
        pendingInputOptions.hasWidth || pendingInputOptions.hasHeight ||
        pendingInputOptions.hasFirstColor || pendingInputOptions.hasLastColor ||
        pendingInputOptions.hasAmigaHack || pendingInputOptions.hasClone ||
        pendingInputOptions.hasCompression;
}

static void ClearPendingTrailingEntryOptions()
{
    pendingInputOptions.hasX = false;
    pendingInputOptions.hasY = false;
    pendingInputOptions.hasFirstColor = false;
    pendingInputOptions.hasLastColor = false;
    pendingInputOptions.hasBuffer = false;
    pendingInputOptions.hasFixed = false;
}

static bool ConvertTrailingInputOptionToPropertyToken(const char* token, char* propertyToken, size_t propertyTokenSize)
{
    const char* propertyPrefix = 0;
    const char* value = 0;

    if (strnicmp(token, "set-x:", 6) == 0) { propertyPrefix = "x:"; value = token + 6; }
    else if (strnicmp(token, "set-y:", 6) == 0) { propertyPrefix = "y:"; value = token + 6; }
    else if (strnicmp(token, "set-pcs:", 8) == 0) { propertyPrefix = "first:"; value = token + 8; }
    else if (strnicmp(token, "set-pce:", 8) == 0) { propertyPrefix = "last:"; value = token + 8; }
    else if (strnicmp(token, "set-fixed:", 10) == 0) { propertyPrefix = "fixed:"; value = token + 10; }
    else if (strnicmp(token, "set-buffer:", 11) == 0) { propertyPrefix = "buffer:"; value = token + 11; }
    else
        return false;

    snprintf(propertyToken, propertyTokenSize, "%s%s", propertyPrefix, value);
    return true;
}

static bool ApplyPendingTrailingEntryOptionsToSelection(DMG* dmg, const bool* currentSelection)
{
    char propertyToken[64];

    if (pendingInputOptions.hasX)
    {
        snprintf(propertyToken, sizeof(propertyToken), "x:%d", pendingInputOptions.x);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }
    if (pendingInputOptions.hasY)
    {
        snprintf(propertyToken, sizeof(propertyToken), "y:%d", pendingInputOptions.y);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }
    if (pendingInputOptions.hasFirstColor)
    {
        snprintf(propertyToken, sizeof(propertyToken), "first:%d", pendingInputOptions.firstColor);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }
    if (pendingInputOptions.hasLastColor)
    {
        snprintf(propertyToken, sizeof(propertyToken), "last:%d", pendingInputOptions.lastColor);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }
    if (pendingInputOptions.hasBuffer)
    {
        snprintf(propertyToken, sizeof(propertyToken), "buffer:%d", pendingInputOptions.buffer ? 1 : 0);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }
    if (pendingInputOptions.hasFixed)
    {
        snprintf(propertyToken, sizeof(propertyToken), "fixed:%d", pendingInputOptions.fixed ? 1 : 0);
        if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
            return false;
    }

    ClearPendingTrailingEntryOptions();
    return true;
}

static bool ApplyPendingEntryOptions(DMG* dmg, int entryIndex)
{
    DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)entryIndex);
    if (entry == 0)
    {
        if (DMG_GetError() != DMG_ERROR_NONE)
            fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", entryIndex, DMG_GetErrorString());
        return false;
    }
    if (entry->type == DMGEntry_Empty)
    {
        fprintf(stderr, "%03d: Error: Entry is empty\n", entryIndex);
        return false;
    }

    if (pendingInputOptions.hasX) entry->x = pendingInputOptions.x;
    if (pendingInputOptions.hasY) entry->y = pendingInputOptions.y;
    if (pendingInputOptions.hasBuffer)
    {
        if (pendingInputOptions.buffer) entry->flags |= DMG_FLAG_BUFFERED;
        else entry->flags &= ~DMG_FLAG_BUFFERED;
    }
    if (pendingInputOptions.hasFixed)
    {
        if (pendingInputOptions.fixed) entry->flags |= DMG_FLAG_FIXED;
        else entry->flags &= ~DMG_FLAG_FIXED;
    }
    if (pendingInputOptions.hasAmigaHack)
    {
        if (pendingInputOptions.amigaHack) entry->flags |= DMG_FLAG_AMIPALHACK;
        else entry->flags &= ~DMG_FLAG_AMIPALHACK;
    }
    if (pendingInputOptions.hasFirstColor) entry->firstColor = (uint8_t)pendingInputOptions.firstColor;
    if (pendingInputOptions.hasLastColor) entry->lastColor = (uint8_t)pendingInputOptions.lastColor;
    return DMG_UpdateEntry(dmg, (uint8_t)entryIndex);
}

static bool IsClassicPaletteDAT(const DMG* dmg)
{
    return dmg != 0 && (dmg->version == DMG_Version1 || dmg->version == DMG_Version2);
}

static void BuildClassicPalette16(uint32_t* outputPalette, const uint32_t* inputPalette, int paletteSize, int firstColor)
{
    MemClear(outputPalette, sizeof(uint32_t) * 16);
    if (inputPalette == 0 || paletteSize <= 0)
        return;

    if (firstColor < 0)
        firstColor = 0;
    if (firstColor >= 16)
        return;

    int available = 16 - firstColor;
    if (paletteSize > available)
        paletteSize = available;
    for (int i = 0; i < paletteSize; i++)
        outputPalette[firstColor + i] = inputPalette[i];
}

static void ApplySavePriorityHints(DMG* dmg)
{
    if (dmg == 0)
        return;
    DMG_ClearSavePriority(dmg);
    for (int i = 0; i < priorityEntryCount; i++)
        DMG_AddSavePriority(dmg, priorityEntries[i]);
}

static bool CropPadIndexedImage(uint8_t** pixels, uint32_t* pixelsBufferSize, uint16_t* width, uint16_t* height, uint16_t targetWidth, uint16_t targetHeight)
{
    if (pixels == 0 || *pixels == 0 || pixelsBufferSize == 0 || width == 0 || height == 0)
        return false;
    if (targetWidth == 0 || targetHeight == 0)
        return false;
    if (*width == targetWidth && *height == targetHeight)
        return true;

    uint32_t targetSize = (uint32_t)targetWidth * targetHeight;
    uint8_t* resized = Allocate<uint8_t>("Cropped image", targetSize);
    if (resized == 0)
    {
        fprintf(stderr, "Error: Out of memory: unable to allocate %u bytes\n", targetSize);
        return false;
    }

    MemClear(resized, targetSize);
    uint16_t copyWidth = *width < targetWidth ? *width : targetWidth;
    uint16_t copyHeight = *height < targetHeight ? *height : targetHeight;
    for (uint16_t y = 0; y < copyHeight; y++)
        MemCopy(resized + (uint32_t)y * targetWidth, *pixels + (uint32_t)y * (*width), copyWidth);

    if (*pixelsBufferSize < targetSize)
    {
        uint8_t* newPixels = (uint8_t*)realloc(*pixels, targetSize);
        if (newPixels == 0)
        {
            Free(resized);
            fprintf(stderr, "Error: Out of memory: unable to allocate %u bytes\n", targetSize);
            return false;
        }
        *pixels = newPixels;
        *pixelsBufferSize = targetSize;
    }

    MemCopy(*pixels, resized, targetSize);
    Free(resized);
    *width = targetWidth;
    *height = targetHeight;
    return true;
}

static const char* BuildWorkingFileName(const char* targetFileName)
{
    const char* working = ChangeExtension(targetFileName, ".wrk");
    if (strcmp(working, targetFileName) != 0)
        return working;
    snprintf(newfilename, sizeof(newfilename), "%s.tmp", targetFileName);
    return newfilename;
}

static bool ParseRemapMode(const char* token, RemapMode* mode)
{
    if (strnicmp(token, "remap:", 6) != 0)
        return false;
    const char* value = token + 6;
    if (stricmp(value, "min") == 0)
        *mode = REMAP_MIN;
    else if (stricmp(value, "reserve") == 0)
        *mode = REMAP_RESERVE;
    else if (stricmp(value, "std") == 0)
        *mode = REMAP_STD;
    else if (stricmp(value, "dark") == 0)
        *mode = REMAP_DARK;
    else if (isdigit(*value))
    {
        char* end = 0;
        long first = strtol(value, &end, 10);
        if (end == value || *end != '-')
            return false;
        const char* lastPtr = end + 1;
        if (!isdigit(*lastPtr))
            return false;
        long last = strtol(lastPtr, &end, 10);
        if (*end != 0 || first < 0 || first > 255 || last < 0 || last > 255 || last < first)
            return false;
        remapRangeFirst = (int)first;
        remapRangeLast = (int)last;
        *mode = REMAP_RANGE;
    }
    else
        return false;
    return true;
}

static bool IsRemapToken(const char* token)
{
    RemapMode mode;
    return ParseRemapMode(token, &mode);
}

static bool IsCreateToken(const char* token)
{
    return strnicmp(token, "mode:", 5) == 0 || strnicmp(token, "screen:", 7) == 0;
}

static bool IsDAT5ScreenToken(const char* token)
{
    return strnicmp(token, "screen:", 7) == 0;
}

static bool IsDAT5ModeToken(const char* token)
{
    return strnicmp(token, "mode:", 5) == 0;
}

static bool IsCompressionToken(const char* token)
{
    return strnicmp(token, "compression:", 12) == 0;
}

static bool IsPriorityToken(const char* token)
{
    return strnicmp(token, "priority:", 9) == 0;
}

static bool ParseGlobalImportSizeToken(const char* token)
{
    int value = 0;
    if (strnicmp(token, "set-width:", 10) == 0 && ParseOptionValue(token + 10, &value))
    {
        createImportWidthExplicit = true;
        createImportWidth = (uint16_t)value;
        return true;
    }
    if (strnicmp(token, "set-height:", 11) == 0 && ParseOptionValue(token + 11, &value))
    {
        createImportHeightExplicit = true;
        createImportHeight = (uint16_t)value;
        return true;
    }
    return false;
}

static bool IsGlobalImportSizeToken(const char* token)
{
    return strnicmp(token, "set-width:", 10) == 0 || strnicmp(token, "set-height:", 11) == 0;
}

static bool HasWildcardChars(const char* token)
{
    return token != 0 && (strchr(token, '*') != 0 || strchr(token, '?') != 0);
}

static bool WildcardMatch(const char* pattern, const char* text)
{
    while (*pattern != 0)
    {
        if (*pattern == '*')
        {
            pattern++;
            if (*pattern == 0)
                return true;
            while (*text != 0)
            {
                if (WildcardMatch(pattern, text))
                    return true;
                text++;
            }
            return false;
        }
        if (*text == 0)
            return false;
        if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*text))
            return false;
        pattern++;
        text++;
    }
    return *text == 0;
}

static const char* GetLastPathSeparator(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (slash == 0)
        return backslash;
    if (backslash == 0)
        return slash;
    return slash > backslash ? slash : backslash;
}

static char* DuplicateString(const char* text)
{
    size_t len = strlen(text) + 1;
    char* copy = (char*)malloc(len);
    if (copy != 0)
        memcpy(copy, text, len);
    return copy;
}

static char* DuplicatePrefixedString(const char* prefix, const char* suffix)
{
    char temp[FILE_MAX_PATH * 2];
    snprintf(temp, sizeof(temp), "%s%s", prefix, suffix);
    return DuplicateString(temp);
}

static bool AppendTranslatedArgument(const char** translatedArgs, int* translatedCount, int maxTranslatedCount, const char* value, const char* sourceToken)
{
    if (*translatedCount >= maxTranslatedCount)
        return SetCommandError("Too many processed arguments (limit %d) while handling \"%s\"", maxTranslatedCount, sourceToken != 0 ? sourceToken : "");
    translatedArgs[(*translatedCount)++] = value;
    return true;
}

static bool AppendTranslatedDuplicate(const char** translatedArgs, int* translatedCount, int maxTranslatedCount, const char* value, const char* sourceToken)
{
    char* copy = DuplicateString(value);
    if (copy == 0)
        return SetCommandError("Out of memory while handling \"%s\"", sourceToken != 0 ? sourceToken : value);
    return AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedCount, copy, sourceToken);
}

static bool AppendTranslatedPrefixed(const char** translatedArgs, int* translatedCount, int maxTranslatedCount, const char* prefix, const char* value, const char* sourceToken)
{
    char* copy = DuplicatePrefixedString(prefix, value);
    if (copy == 0)
        return SetCommandError("Out of memory while handling \"%s\"", sourceToken != 0 ? sourceToken : value);
    return AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedCount, copy, sourceToken);
}

static const char* ConsumeOptionValue(const char* token, const char* inlineValue, int inputCount, const char* inputArgs[], int* index)
{
    if (*inlineValue != 0)
        return inlineValue;
    if (*index + 1 >= inputCount)
    {
        SetCommandError("Missing value for option \"%s\"", token);
        return 0;
    }
    (*index)++;
    return inputArgs[*index];
}

static void ScanIgnoreMissingArguments(int argc, const char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        const char* token = argv[i];
        if (token != 0 && (strcmp(token, "--ignore-missing") == 0 || strcmp(token, "-ignore-missing") == 0))
            ignoreMissingWildcards = true;
    }
}

static bool MatchFlagToken(const char* token, const char* name)
{
    if (token == 0 || name == 0)
        return false;
    if (strcmp(token, name) == 0)
        return true;
    if (name[0] == '-' && name[1] == '-' && token[0] == '-' && token[1] != '-')
        return strcmp(token + 1, name + 2) == 0;
    return false;
}

static const char* MatchOptionValueToken(const char* token, const char* name)
{
    size_t nameLength;

    if (token == 0 || name == 0)
        return 0;
    if (strcmp(token, name) == 0)
        return "";

    nameLength = strlen(name);
    if (strncmp(token, name, nameLength) == 0 && token[nameLength] == '=')
        return token + nameLength + 1;

    if (name[0] == '-' && name[1] == '-' && token[0] == '-' && token[1] != '-')
    {
        const char* shortName = name + 2;
        size_t shortNameLength = strlen(shortName);
        if (strcmp(token + 1, shortName) == 0)
            return "";
        if (strncmp(token + 1, shortName, shortNameLength) == 0 && token[1 + shortNameLength] == '=')
            return token + 1 + shortNameLength + 1;
    }

    return 0;
}

static int CompareStringsIgnoreCase(const void* a, const void* b)
{
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return stricmp(sa, sb);
}

struct DirectoryImportEntry
{
    bool hasPNG;
    bool hasPI1;
    bool hasWAV;
    char pngPath[FILE_MAX_PATH];
    char pi1Path[FILE_MAX_PATH];
    char wavPath[FILE_MAX_PATH];
};

static bool IsRecognizedDirectoryImportExtension(const char* extension)
{
    return stricmp(extension, "png") == 0 ||
        stricmp(extension, "pi1") == 0 ||
        stricmp(extension, "wav") == 0 ||
        stricmp(extension, "json") == 0;
}

static bool IsNumericLocationName(const char* text)
{
    if (text == 0 || *text == 0)
        return false;
    while (*text != 0)
    {
        if (!isdigit((unsigned char)*text))
            return false;
        text++;
    }
    return true;
}

static bool AppendExpandedOwnedArgument(const char** outTokens, int* outCount, int maxOutCount, const char* value, const char* sourceToken)
{
    if (*outCount >= maxOutCount)
    {
        SetCommandError("Too many arguments after expanding \"%s\" (limit %d)", sourceToken, maxOutCount);
        return false;
    }
    char* copy = DuplicateString(value);
    if (copy == 0)
    {
        SetCommandError("Out of memory expanding \"%s\"", sourceToken);
        return false;
    }
    outTokens[(*outCount)++] = copy;
    return true;
}

static bool CollectDirectoryImportEntries(const char* directory, DirectoryImportEntry* entries, bool* foundAnySupportedFiles)
{
    char pattern[FILE_MAX_PATH];
    FindFileResults results;
    const char* separator = GetLastPathSeparator(directory);
    char joinSeparator = (separator != 0 && *separator == '\\') ? '\\' : '/';

    if (directory == 0 || *directory == 0)
        return true;

    if (separator != 0 && separator[1] == 0)
    {
        if (strlen(directory) + 1 >= sizeof(pattern))
        {
            SetCommandError("Folder path too long: %s", directory);
            return false;
        }
        snprintf(pattern, sizeof(pattern), "%s*", directory);
    }
    else
    {
        if (strlen(directory) + 2 >= sizeof(pattern))
        {
            SetCommandError("Folder path too long: %s", directory);
            return false;
        }
        snprintf(pattern, sizeof(pattern), "%s%c*", directory, joinSeparator);
    }

    if (!File_FindFirst(pattern, &results))
    {
        if (ignoreMissingWildcards)
            return true;
        SetCommandError("Invalid folder: %s", directory);
        return false;
    }

    do
    {
        if ((results.attributes & FileAttribute_Directory) != 0)
            continue;

        char fullPath[FILE_MAX_PATH];
        if (strchr(results.fileName, '/') != 0 || strchr(results.fileName, '\\') != 0)
        {
            StrCopy(fullPath, sizeof(fullPath), results.fileName);
        }
        else if (separator != 0 && separator[1] == 0)
        {
            StrCopy(fullPath, sizeof(fullPath), directory);
            StrCat(fullPath, sizeof(fullPath), results.fileName);
        }
        else
        {
            StrCopy(fullPath, sizeof(fullPath), directory);
            char separatorText[2] = { joinSeparator, 0 };
            StrCat(fullPath, sizeof(fullPath), separatorText);
            StrCat(fullPath, sizeof(fullPath), results.fileName);
        }

        const char* filePart = GetLastPathSeparator(fullPath);
        filePart = filePart != 0 ? filePart + 1 : fullPath;
        const char* dot = strrchr(filePart, '.');
        if (dot == 0)
            continue;
        if (!IsRecognizedDirectoryImportExtension(dot + 1))
            continue;

        *foundAnySupportedFiles = true;

        char baseName[FILE_MAX_PATH];
        size_t baseLength = (size_t)(dot - filePart);
        if (baseLength >= sizeof(baseName))
            baseLength = sizeof(baseName) - 1;
        memcpy(baseName, filePart, baseLength);
        baseName[baseLength] = 0;

        if (stricmp(baseName, "DAAD") == 0)
            continue;
        if (!IsNumericLocationName(baseName))
        {
            SetCommandError("Invalid location number in %s", fullPath);
            return false;
        }

        int location = atoi(baseName);
        if (location < 0 || location > 255)
        {
            SetCommandError("Invalid location number in %s", fullPath);
            return false;
        }

        DirectoryImportEntry* entry = &entries[location];
        if (stricmp(dot + 1, "png") == 0)
        {
            entry->hasPNG = true;
            StrCopy(entry->pngPath, sizeof(entry->pngPath), fullPath);
        }
        else if (stricmp(dot + 1, "pi1") == 0)
        {
            entry->hasPI1 = true;
            StrCopy(entry->pi1Path, sizeof(entry->pi1Path), fullPath);
        }
        else if (stricmp(dot + 1, "wav") == 0)
        {
            entry->hasWAV = true;
            StrCopy(entry->wavPath, sizeof(entry->wavPath), fullPath);
        }
    } while (File_FindNext(&results));

    return true;
}

static int ExpandDirectoryToken(const char* token, const char** outTokens, int* outCount, int maxOutCount)
{
    if (token == 0 || *token == 0)
        return 0;
    if (token[0] == '-')
        return 0;
    if (strchr(token, ';') == 0 && GetLastPathSeparator(token) == 0)
        return 0;
    if (HasWildcardChars(token) || IsSelectionToken(token) || IsPropertyToken(token) || IsRemapToken(token) || IsCompressionToken(token) || IsPriorityToken(token) || IsCreateToken(token) || IsTargetedFileToken(token) || IsImageToken(token) || IsAudioToken(token))
        return 0;

    DirectoryImportEntry entries[256];
    MemClear(entries, sizeof(entries));
    bool foundAnySupportedFiles = false;
    bool processedDirectory = false;

    const char* part = token;
    while (*part != 0)
    {
        const char* next = strchr(part, ';');
        char directory[FILE_MAX_PATH];
        size_t length = next != 0 ? (size_t)(next - part) : strlen(part);
        if (length >= sizeof(directory))
            length = sizeof(directory) - 1;
        memcpy(directory, part, length);
        directory[length] = 0;

        if (directory[0] != 0)
        {
            processedDirectory = true;
            if (!CollectDirectoryImportEntries(directory, entries, &foundAnySupportedFiles))
                return -1;
        }

        if (next == 0)
            break;
        part = next + 1;
    }

    if (!processedDirectory)
        return 0;

    int before = *outCount;
    for (int location = 0; location < 256; location++)
    {
        if (entries[location].hasPNG)
        {
            if (!AppendExpandedOwnedArgument(outTokens, outCount, maxOutCount, entries[location].pngPath, token))
                return -1;
        }
        else if (entries[location].hasPI1)
        {
            if (!AppendExpandedOwnedArgument(outTokens, outCount, maxOutCount, entries[location].pi1Path, token))
                return -1;
        }
        else if (entries[location].hasWAV)
        {
            if (!AppendExpandedOwnedArgument(outTokens, outCount, maxOutCount, entries[location].wavPath, token))
                return -1;
        }
    }

    if (*outCount != before)
        return 1;
    if (foundAnySupportedFiles)
        return 1;
    return processedDirectory ? 1 : 0;
}

static int ExpandWildcardToken(const char* token, const char** outTokens, int* outCount, int maxOutCount)
{
    if (!HasWildcardChars(token))
        return 0;
    if (IsSelectionToken(token) || IsPropertyToken(token) || IsRemapToken(token) || IsCompressionToken(token) || IsPriorityToken(token) || IsCreateToken(token))
        return 0;

    const char* pattern = token;
    if (IsTargetedFileToken(token))
        return 0;
    const char* colon = strchr(token, ':');
    if (colon != 0 && colon != token && isdigit((unsigned char)*token))
        return 0;

    FindFileResults results;
    char* matches[CLI_MAX_ARGUMENTS];
    int matchCount = 0;
    if (File_FindFirst(pattern, &results))
    {
        do
        {
            if ((results.attributes & FileAttribute_Directory) != 0)
                continue;

            char fullPath[FILE_MAX_PATH];
            const char* separator = GetLastPathSeparator(pattern);
            if (strchr(results.fileName, '/') != 0 || strchr(results.fileName, '\\') != 0 || separator == 0)
            {
                StrCopy(fullPath, sizeof(fullPath), results.fileName);
            }
            else
            {
                size_t prefixLength = (size_t)(separator - pattern + 1);
                if (prefixLength >= sizeof(fullPath))
                    continue;
                memcpy(fullPath, pattern, prefixLength);
                fullPath[prefixLength] = 0;
                StrCat(fullPath, sizeof(fullPath), results.fileName);
            }

            if (!WildcardMatch(pattern, fullPath))
                continue;

            matches[matchCount] = DuplicateString(fullPath);
            if (matches[matchCount] == 0)
            {
                for (int i = 0; i < matchCount; i++)
                    free(matches[i]);
                SetCommandError("Out of memory expanding wildcard \"%s\"", token);
                return -1;
            }
            matchCount++;
            if (*outCount + matchCount > maxOutCount)
            {
                for (int i = 0; i < matchCount; i++)
                    free(matches[i]);
                SetCommandError("Too many arguments after expanding wildcard \"%s\" (limit %d)", token, maxOutCount);
                return -1;
            }
        } while (File_FindNext(&results));
    }

    if (matchCount == 0)
    {
        if (ignoreMissingWildcards)
        {
            ignoredWildcardExpansionCount++;
            return 1;
        }
        SetCommandError("No files matched wildcard \"%s\"", token);
        return -1;
    }

    qsort(matches, matchCount, sizeof(matches[0]), CompareStringsIgnoreCase);
    for (int i = 0; i < matchCount; i++)
        outTokens[(*outCount)++] = matches[i];
    return 1;
}

static int ExpandArgumentTokens(int argc, const char* argv[], const char** expandedArgs, bool* expandedArgOwned, int maxExpandedArgs)
{
    int expandedArgCount = 0;
    for (int i = 0; i < argc; i++)
    {
        int before = expandedArgCount;
        int dirResult = ExpandDirectoryToken(argv[i], expandedArgs, &expandedArgCount, maxExpandedArgs);
        if (dirResult < 0)
            return -1;
        if (dirResult > 0)
        {
            for (int n = before; n < expandedArgCount; n++)
                expandedArgOwned[n] = true;
            continue;
        }

        before = expandedArgCount;
        int result = ExpandWildcardToken(argv[i], expandedArgs, &expandedArgCount, maxExpandedArgs);
        if (result < 0)
            return -1;
        if (result > 0)
        {
            for (int n = before; n < expandedArgCount; n++)
                expandedArgOwned[n] = true;
            continue;
        }
        if (expandedArgCount >= maxExpandedArgs)
        {
            SetCommandError("Too many arguments (limit %d), offending token \"%s\"", maxExpandedArgs, argv[i]);
            return -1;
        }
        expandedArgs[expandedArgCount] = argv[i];
        expandedArgOwned[expandedArgCount] = false;
        expandedArgCount++;
    }
    return expandedArgCount;
}

static void FreeExpandedArgumentTokens(int expandedArgCount, const char** expandedArgs, const bool* expandedArgOwned)
{
    for (int i = 0; i < expandedArgCount; i++)
    {
        if (expandedArgOwned[i])
            free((void*)expandedArgs[i]);
    }
}

static int GetPriorityRank(uint8_t index)
{
    for (int i = 0; i < priorityEntryCount; i++)
    {
        if (priorityEntries[i] == index)
            return i;
    }
    return 0x7FFF;
}

static bool TryGetDirectTargetIndexForCreate(const char* token, int* targetIndex)
{
    const char* colon = strchr(token, ':');
    const char* slash = strrchr(token, '/');
    const char* backslash = strrchr(token, '\\');
    const char* filepart = slash ? slash + 1 : backslash ? backslash + 1 : token;

    if (colon != 0 && colon != token && isdigit(*token))
    {
        int value = atoi(token);
        if (value < 0 || value > 255)
            return false;
        *targetIndex = value;
        return true;
    }

    if (isdigit(*filepart))
    {
        int value = atoi(filepart);
        if (value >= 0 && value < 256)
        {
            *targetIndex = value;
            return true;
        }
    }

    return false;
}

static bool BuildDirectCreatePriorityArguments(int tokenCount, const char* tokens[], const char** reorderedTokens)
{
    struct ImageTokenInfo
    {
        const char* token;
        int targetIndex;
        int originalOrder;
    };

    ImageTokenInfo imageTokens[CLI_MAX_ARGUMENTS];
    int imageCount = 0;
    int outCount = 0;

    for (int i = 0; i < tokenCount; i++)
    {
        const char* token = tokens[i];
        if (token == 0 || *token == 0)
            continue;

        bool selection[256];
        if (ParseSelectionToken(token, selection))
            return false;
        if (IsPropertyToken(token))
            return false;

        if (IsTargetedFileToken(token) || IsImageToken(token) || IsAudioToken(token))
        {
            int targetIndex = -1;
            if (!TryGetDirectTargetIndexForCreate(token, &targetIndex))
                return false;
            imageTokens[imageCount].token = token;
            imageTokens[imageCount].targetIndex = targetIndex;
            imageTokens[imageCount].originalOrder = imageCount;
            imageCount++;
        }
        else
        {
            reorderedTokens[outCount++] = token;
        }
    }

    if (imageCount == 0)
        return false;

    for (int i = 0; i < imageCount - 1; i++)
    {
        for (int j = i + 1; j < imageCount; j++)
        {
            int ai = GetPriorityRank((uint8_t)imageTokens[i].targetIndex);
            int aj = GetPriorityRank((uint8_t)imageTokens[j].targetIndex);
            bool swap = false;
            if (ai != aj)
                swap = ai < aj ? false : true;
            else
                swap = imageTokens[i].originalOrder > imageTokens[j].originalOrder;
            if (swap)
            {
                ImageTokenInfo tmp = imageTokens[i];
                imageTokens[i] = imageTokens[j];
                imageTokens[j] = tmp;
            }
        }
    }

    for (int i = 0; i < imageCount; i++)
        reorderedTokens[outCount++] = imageTokens[i].token;

    return true;
}

static bool IsEntryChangeToken(const char* token)
{
    return IsSelectionToken(token) ||
        IsTargetedFileToken(token) ||
        IsImageToken(token) ||
        IsAudioToken(token) ||
        IsRemapToken(token) ||
        IsCompressionToken(token) ||
        IsPriorityToken(token) ||
        IsPropertyToken(token) ||
        IsCreateToken(token);
}

static void ResetPriorityEntries()
{
    priorityEntryCount = 0;
}

static bool AddPriorityEntry(int value)
{
    if (value < 0 || value > 255)
        return false;
    for (int i = 0; i < priorityEntryCount; i++)
    {
        if (priorityEntries[i] == (uint8_t)value)
            return true;
    }
    priorityEntries[priorityEntryCount++] = (uint8_t)value;
    return true;
}

static bool ParseOptionValue(const char* ptr, int* value);

static bool ParsePriorityToken(const char* token)
{
    if (!IsPriorityToken(token))
        return false;

    const char* ptr = token + 9;
    if (*ptr == 0)
        return false;

    while (*ptr != 0)
    {
        if (!isdigit(*ptr))
            return false;
        int start = atoi(ptr);
        while (isdigit(*ptr)) ptr++;
        int end = start;
        if (*ptr == '-')
        {
            ptr++;
            if (!isdigit(*ptr))
                return false;
            end = atoi(ptr);
            while (isdigit(*ptr)) ptr++;
        }
        if (start <= end)
        {
            for (int n = start; n <= end; n++)
                if (!AddPriorityEntry(n))
                    return false;
        }
        else
        {
            for (int n = start; n >= end; n--)
                if (!AddPriorityEntry(n))
                    return false;
        }
        if (*ptr == 0)
            break;
        if (*ptr != ',')
            return false;
        ptr++;
    }
    return true;
}

static bool ParseCompressionToken(const char* token)
{
    if (!IsCompressionToken(token))
        return false;

    int value = 0;
    if (!ParseOptionValue(token + 12, &value) || (value != 0 && value != 1))
        return false;

    compressionEnabled = value != 0;
    return true;
}

static DDB_ScreenMode GetDAT5ScreenModeForSettings(DMG_DAT5ColorMode colorMode, uint16_t width, uint16_t height)
{
    if (width == 640 && height == 400)
        return ScreenMode_SHiRes;
    if (width == 640 && height == 200)
        return ScreenMode_HiRes;
    if (colorMode == DMG_DAT5_COLORMODE_CGA)
        return ScreenMode_CGA;
    if (colorMode == DMG_DAT5_COLORMODE_EGA)
        return ScreenMode_EGA;
    if (DMG_DAT5ModePlaneCount(colorMode) >= 8)
        return ScreenMode_VGA;
    return ScreenMode_VGA16;
}

static bool ApplyDAT5HeaderToken(DMG* dmg, const char* token)
{
    if (dmg == 0 || dmg->version != DMG_Version5)
    {
        fprintf(stderr, "Error: DAT5 header options are only valid for DAT5 files\n");
        return false;
    }

    if (IsDAT5ScreenToken(token))
    {
        uint16_t width = 0;
        uint16_t height = 0;
        if (!ParseDAT5Size(token + 7, &width, &height))
        {
            fprintf(stderr, "Error: Invalid DAT5 screen size: \"%s\"\n", token + 7);
            return false;
        }

        dmg->targetWidth = width;
        dmg->targetHeight = height;
        dmg->screenMode = GetDAT5ScreenModeForSettings((DMG_DAT5ColorMode)dmg->colorMode, width, height);
        if (!DMG_UpdateFileHeader(dmg))
        {
            fprintf(stderr, "Error: Unable to update DAT5 header: %s\n", DMG_GetErrorString());
            return false;
        }
        printf("DAT5 target screen set to %ux%u\n", (unsigned)width, (unsigned)height);
        return true;
    }

    if (IsDAT5ModeToken(token))
    {
        DMG_DAT5ColorMode mode = DMG_DAT5_COLORMODE_NONE;
        if (!ParseDAT5Mode(token + 5, &mode))
        {
            fprintf(stderr, "Error: Invalid DAT5 mode: \"%s\"\n", token + 5);
            return false;
        }

        createDAT5 = true;
        createDAT5Mode = mode;
        printf("DAT5 mode set to %s\n", DescribeDAT5ColorMode(mode));
        return true;
    }

    return false;
}

static bool HasBufferedEntries(DMG* dmg)
{
    if (dmg == 0)
        return false;
    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry != 0 && entry->type != DMGEntry_Empty && (entry->flags & DMG_FLAG_BUFFERED))
            return true;
    }
    return false;
}

static int BuildRebuildOrder(DMG* dmg, uint8_t* order)
{
    bool added[256];
    MemClear(added, sizeof(added));
    int count = 0;
    int start = dmg->version == DMG_Version5 ? dmg->firstEntry : 0;
    int end = dmg->version == DMG_Version5 ? dmg->lastEntry : 255;

    for (int i = 0; i < priorityEntryCount; i++)
    {
        uint8_t n = priorityEntries[i];
        DMG_Entry* entry = DMG_GetEntry(dmg, n);
        if (entry == 0 || entry->type == DMGEntry_Empty || added[n])
            continue;
        order[count++] = n;
        added[n] = true;
    }

    for (int n = start; n <= end; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry == 0 || entry->type == DMGEntry_Empty || added[n] || (entry->flags & DMG_FLAG_BUFFERED) == 0)
            continue;
        order[count++] = (uint8_t)n;
        added[n] = true;
    }

    for (int n = start; n <= end; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry == 0 || entry->type == DMGEntry_Empty || added[n])
            continue;
        order[count++] = (uint8_t)n;
        added[n] = true;
    }

    return count;
}

static bool ParseOptionValue(const char* ptr, int* value)
{
    if (isdigit(*ptr))
    {
        *value = atoi(ptr);
        return true;
    }
    if (stricmp(ptr, "true") == 0)
    {
        *value = 1;
        return true;
    }
    if (stricmp(ptr, "false") == 0)
    {
        *value = 0;
        return true;
    }
    if (stricmp(ptr, "red") == 0)
    {
        *value = 1;
        return true;
    }
    if (stricmp(ptr, "blue") == 0)
    {
        *value = 0;
        return true;
    }
    return false;
}

static bool ApplyPropertyToEntry(DMG* dmg, int entryIndex, ColonOption option, int value)
{
    DMG_Entry* entry = DMG_GetEntry(dmg, entryIndex);
    if (entry == 0)
    {
        if (DMG_GetError() != DMG_ERROR_NONE)
            fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", entryIndex, DMG_GetErrorString());
        return false;
    }

    switch (option)
    {
        case OPTION_X:
            if (entry->type != DMGEntry_Image)
            {
                fprintf(stderr, "%03d: Error: X coordinate only valid for images\n", entryIndex);
                return false;
            }
            if (value < 0 || value > DMG_MAX_IMAGE_WIDTH)
            {
                fprintf(stderr, "%03d: Error: Invalid X coordinate: %d\n", entryIndex, value);
                return false;
            }
            entry->x = value;
            printf("%03d: X coordinate set to %d\n", entryIndex, value);
            break;

        case OPTION_Y:
            if (entry->type != DMGEntry_Image)
            {
                fprintf(stderr, "%03d: Error: Y coordinate only valid for images\n", entryIndex);
                return false;
            }
            if (value < 0 || value > DMG_MAX_IMAGE_HEIGHT)
            {
                fprintf(stderr, "%03d: Error: Invalid Y coordinate: %d\n", entryIndex, value);
                return false;
            }
            entry->y = value;
            printf("%03d: Y coordinate set to %d\n", entryIndex, value);
            break;

        case OPTION_FIRST_COLOR:
            if (value < 0 || value > 15)
            {
                fprintf(stderr, "%03d: Error: Invalid first color: %d\n", entryIndex, value);
                return false;
            }
            entry->firstColor = value;
            printf("%03d: First color set to %d\n", entryIndex, value);
            break;

        case OPTION_LAST_COLOR:
            if (value < 0 || value > 15)
            {
                fprintf(stderr, "%03d: Error: Invalid last color: %d\n", entryIndex, value);
                return false;
            }
            entry->lastColor = value;
            printf("%03d: Last color set to %d\n", entryIndex, value);
            break;

        case OPTION_BUFFER:
            if (value < 0 || value > 1)
            {
                fprintf(stderr, "%03d: Error: Invalid buffer flag: %d\n", entryIndex, value);
                return false;
            }
            if (value)
                entry->flags |= DMG_FLAG_BUFFERED;
            else
                entry->flags &= ~DMG_FLAG_BUFFERED;
            printf("%03d: Buffer flag set to %s\n", entryIndex, value ? "true" : "false");
            break;

        case OPTION_FIXED:
            if (value < 0 || value > 1)
            {
                fprintf(stderr, "%03d: Error: Invalid fixed flag: %d\n", entryIndex, value);
                return false;
            }
            if (value)
                entry->flags |= DMG_FLAG_FIXED;
            else
                entry->flags &= ~DMG_FLAG_FIXED;
            printf("%03d: Fixed flag set to %s\n", entryIndex, value ? "true" : "false");
            break;

        case OPTION_CGA:
            if (value < 0 || value > 1)
            {
                fprintf(stderr, "%03d: Error: Invalid CGA mode: %d\n", entryIndex, value);
                return false;
            }
            DMG_SetCGAMode(entry, (DMG_CGAMode)value);
            printf("%03d: CGA mode set to %s\n", entryIndex, value ? "blue" : "red");
            break;

        case OPTION_FREQ:
        {
            if (entry->type != DMGEntry_Audio)
            {
                fprintf(stderr, "%03d: Error: Frequency only valid for audio\n", entryIndex);
                return false;
            }
            int i;
            for (i = 0; frequencies[i].value != 0; i++)
            {
                if (frequencies[i].value == value)
                {
                    entry->x = frequencies[i].freq;
                    break;
                }
            }
            if (frequencies[i].value == 0)
            {
                fprintf(stderr, "%03d: Error: Invalid frequency: %d\n", entryIndex, value);
                return false;
            }
            printf("%03d: Frequency set to %s\n", entryIndex, DMG_DescribeFreq((DMG_KHZ)entry->x));
            break;
        }
    }

    DMG_UpdateEntry(dmg, entryIndex);
    return true;
}

static bool ApplyPropertyToSelection(DMG* dmg, const bool* currentSelection, const char* token)
{
    int option = -1;
    const char* valuePtr = 0;
    for (int i = 0; i < colonOptionCount; i++)
    {
        size_t len = strlen(colonOptions[i]);
        if (strnicmp(token, colonOptions[i], len) == 0)
        {
            option = i;
            valuePtr = token + len;
            break;
        }
    }

    if (option < 0)
    {
        fprintf(stderr, "Error: Unknown option: \"%s\"\n", token);
        return false;
    }
    if (CountSelectedEntries(currentSelection) == 0)
    {
        fprintf(stderr, "Error: Entry selector required before \"%s\"\n", token);
        return false;
    }

    int value = 0;
    if (!ParseOptionValue(valuePtr, &value))
    {
        fprintf(stderr, "Error: Invalid value: \"%s\"\n", valuePtr);
        return false;
    }

    for (int i = 0; i < 256; i++)
    {
        if (!currentSelection[i])
            continue;

        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)i);
        if (entry == 0)
        {
            if (DMG_GetError() != DMG_ERROR_NONE)
                fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", i, DMG_GetErrorString());
            continue;
        }
        if (entry->type == DMGEntry_Empty)
            continue;

        ApplyPropertyToEntry(dmg, i, (ColonOption)option, value);
    }
    return true;
}

static bool ApplyImageToken(DMG* dmg, const char* token, bool* currentSelection, bool* explicitSelection, int* currentIndex, bool* currentFileSaved)
{
    const char* path = token;
    const char* colon = strchr(token, ':');
    const char* slash = strrchr(token, '/');
    const char* backslash = strrchr(token, '\\');
    const char* filepart = slash ? slash + 1 : backslash ? backslash + 1 : token;
    int targetIndex = -1;

    PendingInputOptions effectiveOptions;
    MemClear(&effectiveOptions, sizeof(effectiveOptions));
    effectiveOptions.useSidecarJson = true;

    if (colon != 0 && colon != token && isdigit(*token))
    {
        targetIndex = atoi(token);
        if (targetIndex > 255)
        {
            fprintf(stderr, "Error: Invalid index: %d\n", targetIndex);
            return false;
        }
        path = colon + 1;
    }
    else
    {
        int selectionCount = CountSelectedEntries(currentSelection);
        if (*explicitSelection && selectionCount > 1)
        {
            fprintf(stderr, "Error: Image token \"%s\" requires a single selected entry or an explicit #:<file> target\n", token);
            return false;
        }
        if (*explicitSelection && selectionCount == 1)
        {
            targetIndex = FirstSelectedEntry(currentSelection);
        }
        else if (isdigit(*filepart) && atoi(filepart) < 256)
        {
            targetIndex = atoi(filepart);
        }
        else if (!*currentFileSaved)
        {
            targetIndex = DMG_FindFreeEntry(dmg);
        }
        else if (*currentIndex >= 0 && *currentIndex < 255)
        {
            targetIndex = *currentIndex + 1;
        }
        else
        {
            fprintf(stderr, "Error: Unable to determine target entry for \"%s\"\n", token);
            return false;
        }
    }

    if (pendingInputOptions.useSidecarJson)
    {
        char sidecar[FILE_MAX_PATH];
        BuildSidecarPath(path, ".json", sidecar, sizeof(sidecar));
        LoadPendingOptionsFromJson(sidecar, &effectiveOptions);
    }
    else if (pendingInputOptions.hasJsonFile)
    {
        LoadPendingOptionsFromJson(pendingInputOptions.jsonFile, &effectiveOptions);
    }

    if (pendingInputOptions.hasX) { effectiveOptions.hasX = true; effectiveOptions.x = pendingInputOptions.x; }
    if (pendingInputOptions.hasY) { effectiveOptions.hasY = true; effectiveOptions.y = pendingInputOptions.y; }
    if (createImportWidthExplicit && !effectiveOptions.hasWidth) { effectiveOptions.hasWidth = true; effectiveOptions.width = createImportWidth; }
    if (createImportHeightExplicit && !effectiveOptions.hasHeight) { effectiveOptions.hasHeight = true; effectiveOptions.height = createImportHeight; }
    if (pendingInputOptions.hasFirstColor) { effectiveOptions.hasFirstColor = true; effectiveOptions.firstColor = pendingInputOptions.firstColor; }
    if (pendingInputOptions.hasLastColor) { effectiveOptions.hasLastColor = true; effectiveOptions.lastColor = pendingInputOptions.lastColor; }
    if (pendingInputOptions.hasBuffer) { effectiveOptions.hasBuffer = true; effectiveOptions.buffer = pendingInputOptions.buffer; }
    if (pendingInputOptions.hasFixed) { effectiveOptions.hasFixed = true; effectiveOptions.fixed = pendingInputOptions.fixed; }
    if (pendingInputOptions.hasAmigaHack) { effectiveOptions.hasAmigaHack = true; effectiveOptions.amigaHack = pendingInputOptions.amigaHack; }
    if (pendingInputOptions.hasClone) { effectiveOptions.hasClone = true; effectiveOptions.cloneSource = pendingInputOptions.cloneSource; }
    if (pendingInputOptions.hasCompression) { effectiveOptions.hasCompression = true; effectiveOptions.compression = pendingInputOptions.compression; }

    bool localCompressionEnabled = effectiveOptions.hasCompression ? effectiveOptions.compression : compressionEnabled;

    if (effectiveOptions.hasClone)
    {
        if (!DMG_ReuseEntryData(dmg, (uint8_t)targetIndex, (uint8_t)effectiveOptions.cloneSource))
        {
            fprintf(stderr, "Error: Unable to clone entry %d into %d: %s\n", effectiveOptions.cloneSource, targetIndex, DMG_GetErrorString());
            return false;
        }
        DMG_Entry* cloned = DMG_GetEntry(dmg, (uint8_t)targetIndex);
        if (cloned != 0)
        {
            if (effectiveOptions.hasX) cloned->x = effectiveOptions.x;
            if (effectiveOptions.hasY) cloned->y = effectiveOptions.y;
            if (effectiveOptions.hasBuffer)
            {
                if (effectiveOptions.buffer) cloned->flags |= DMG_FLAG_BUFFERED;
                else cloned->flags &= ~DMG_FLAG_BUFFERED;
            }
            if (effectiveOptions.hasFixed)
            {
                if (effectiveOptions.fixed) cloned->flags |= DMG_FLAG_FIXED;
                else cloned->flags &= ~DMG_FLAG_FIXED;
            }
            if (effectiveOptions.hasAmigaHack)
            {
                if (effectiveOptions.amigaHack) cloned->flags |= DMG_FLAG_AMIPALHACK;
                else cloned->flags &= ~DMG_FLAG_AMIPALHACK;
            }
            DMG_UpdateEntry(dmg, (uint8_t)targetIndex);
        }
        SelectSingleEntry(currentSelection, targetIndex);
        *explicitSelection = false;
        *currentIndex = targetIndex;
        *currentFileSaved = true;
        ResetPendingInputOptions();
        printf("%03d: Cloned entry %03d\n", targetIndex, effectiveOptions.cloneSource);
        return true;
    }

    if (bufferSize < LOAD_BUFFER_SIZE)
    {
        bufferSize = LOAD_BUFFER_SIZE;
        buffer = (uint8_t*)realloc(buffer, bufferSize);
        if (buffer == NULL)
        {
            fprintf(stderr, "Error: Out of memory: unable to allocate %d bytes\n", bufferSize);
            return false;
        }
    }

    if (effectiveOptions.hasWidth && (effectiveOptions.width <= 0 || effectiveOptions.width > DMG_MAX_IMAGE_WIDTH))
    {
        fprintf(stderr, "Error: Invalid import width: %d\n", effectiveOptions.width);
        return false;
    }
    if (effectiveOptions.hasHeight && (effectiveOptions.height <= 0 || effectiveOptions.height > DMG_MAX_IMAGE_HEIGHT))
    {
        fprintf(stderr, "Error: Invalid import height: %d\n", effectiveOptions.height);
        return false;
    }

    uint16_t width = 0, height = 0;
    uint32_t palette[256];
    int paletteSize = 0;
    int sourceColorCount = 0;
    int importPaletteLimit = GetRequestedImportPaletteLimit(dmg);
    int firstColor = 0;
    int lastColor = 0;
    bool quantized = false;
    if (!LoadIndexedImageFile(path, buffer, bufferSize, &width, &height, &palette[0], &paletteSize, importPaletteLimit, &quantized, &sourceColorCount))
    {
        fprintf(stderr, "Error: Unable to load image \"%s\": %s\n", path, DMG_GetErrorString());
        return false;
    }
    if (effectiveOptions.hasWidth || effectiveOptions.hasHeight)
    {
        uint16_t targetWidth = effectiveOptions.hasWidth ? (uint16_t)effectiveOptions.width : width;
        uint16_t targetHeight = effectiveOptions.hasHeight ? (uint16_t)effectiveOptions.height : height;
        if (!CropPadIndexedImage(&buffer, &bufferSize, &width, &height, targetWidth, targetHeight))
            return false;
    }
    if (paletteSize > 0)
        lastColor = paletteSize - 1;

    uint32_t size = width * height;
    if (dmg->version == DMG_Version5)
    {
        uint32_t encodedBytes = GetDAT5EncodedSize((DMG_DAT5ColorMode)dmg->colorMode, width, height);
        uint32_t totalBytes = size + encodedBytes;
        if (bufferSize < totalBytes)
        {
            bufferSize = totalBytes;
            buffer = (uint8_t*)realloc(buffer, bufferSize);
            if (buffer == NULL)
            {
                fprintf(stderr, "Error: Out of memory: unable to allocate %d bytes\n", bufferSize);
                return false;
            }
            if (!LoadIndexedImageFile(path, buffer, bufferSize, &width, &height, &palette[0], &paletteSize, importPaletteLimit, &quantized, &sourceColorCount))
            {
                fprintf(stderr, "Error: Unable to reload image \"%s\": %s\n", path, DMG_GetErrorString());
                return false;
            }
            size = width * height;
        }
    }
    if (quantized)
    {
        char warning[256];
        snprintf(warning, sizeof(warning), "Image \"%s\" reduced from %d to %d colors", path, sourceColorCount, paletteSize);
        ShowWarning(warning);
    }
    if (remapMode != REMAP_NONE && dmg->version == DMG_Version5 && paletteSize > 0)
        ApplyPaletteRemap(buffer, size, palette, &paletteSize, GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode), &firstColor, &lastColor);

    if (effectiveOptions.hasFirstColor)
        firstColor = effectiveOptions.firstColor;
    if (effectiveOptions.hasLastColor)
        lastColor = effectiveOptions.lastColor;

    if (dmg->version == DMG_Version5)
    {
        int limit = GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode);
        uint32_t encodedSize = 0;
        uint32_t storedSize = 0;
        uint8_t* storedBuffer = 0;
        uint8_t* outBuffer = buffer + size;
        bool compressed = false;

        if (paletteSize > limit)
        {
            fprintf(stderr, "Error: Image palette has %d colors, limit is %d for this DAT5 mode\n", paletteSize, limit);
            return false;
        }
        char cacheFile[FILE_MAX_PATH];
        uint8_t* cachedPayload = 0;
        uint32_t cachedPayloadSize = 0;
        BuildSidecarPath(path, DMG_CACHE_EXTENSION, cacheFile, sizeof(cacheFile));
        if (!TryLoadCachedPayload(path, cacheFile, dmg->version, dmg->colorMode, (uint8_t)remapMode, (uint8_t)firstColor, (uint8_t)lastColor, width, height, localCompressionEnabled ? 1 : 0, &cachedPayload, &cachedPayloadSize))
        {
            if (!EncodeDAT5Image((DMG_DAT5ColorMode)dmg->colorMode, buffer, width, height, outBuffer, &encodedSize))
            {
                fprintf(stderr, "Error: Unable to encode image \"%s\" for DAT5 mode\n", path);
                return false;
            }
            if (localCompressionEnabled)
            {
                if (!CompressDAT5Image(outBuffer, encodedSize, &storedBuffer, &storedSize, &compressed))
                {
                    fprintf(stderr, "Error: Unable to compress image \"%s\"\n", path);
                    return false;
                }
            }
            else
            {
                storedBuffer = outBuffer;
                storedSize = encodedSize;
            }
            SaveCachedPayload(path, cacheFile, dmg->version, dmg->colorMode, (uint8_t)remapMode, (uint8_t)firstColor, (uint8_t)lastColor, width, height, localCompressionEnabled ? 1 : 0, storedBuffer, storedSize);
        }
        else
        {
            storedBuffer = cachedPayload;
            storedSize = cachedPayloadSize;
            compressed = localCompressionEnabled;
        }
        if (!DMG_SetEntryPaletteRange(dmg, targetIndex, palette, paletteSize, (uint8_t)firstColor, (uint8_t)lastColor))
        {
            fprintf(stderr, "Error: Unable to set image palette: %s\n", DMG_GetErrorString());
            if (compressed)
                Free(storedBuffer);
            return false;
        }
        if (!DMG_SetImageDataEx(dmg, targetIndex, storedBuffer, width, height, storedSize, compressed, GetBitDepthForMode((DMG_DAT5ColorMode)dmg->colorMode)))
        {
            fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
            if (compressed)
                Free(storedBuffer);
            return false;
        }
        printf("%03d: Added image %s (%u bytes%s)\n", targetIndex, path, storedSize, compressed ? ", ZX0" : "");
        if (storedBuffer == cachedPayload)
            Free(cachedPayload);
        else if (compressed)
            Free(storedBuffer);
    }
    else
    {
        uint8_t maxIndex = GetMaxPixelIndex(buffer, size);
        if (paletteSize > 16 || maxIndex > 15)
        {
            fprintf(stderr, "Error: Legacy DAT formats only support up to 16 colors. Use DAT5 mode:planar4/mode:planar5/mode:planar8.\n");
            return false;
        }
        if (remapMode != REMAP_NONE && IsClassicPaletteDAT(dmg) && paletteSize > 0)
            ApplyPaletteRemap(buffer, size, palette, &paletteSize, 16, &firstColor, &lastColor);
        uint8_t* storedBuffer = buffer;
        uint16_t compressedSize = (uint16_t)size;
        bool compressed = false;
        if (dmg->version == DMG_Version1_EGA || dmg->version == DMG_Version1_CGA || dmg->version == DMG_Version1_PCW)
        {
            uint8_t* outBuffer = buffer + size;
            if (dmg->version == DMG_Version1_EGA)
            {
                compressedSize = (uint16_t)(((uint32_t)(width + 15) >> 4) * height * 4 * 2);
                if (!DMG_PackBitplaneBytes(buffer, width, height, 4, outBuffer))
                {
                    fprintf(stderr, "Error: Unable to encode EGA image \"%s\"\n", path);
                    return false;
                }
            }
            else if (dmg->version == DMG_Version1_CGA)
            {
                if (maxIndex > 3)
                {
                    fprintf(stderr, "Error: CGA output only supports 4 colors\n");
                    return false;
                }
                compressedSize = (uint16_t)(((uint32_t)width * height + 3) / 4);
                if (!DMG_PackChunkyPixels(buffer, width, height, 2, outBuffer))
                {
                    fprintf(stderr, "Error: Unable to encode CGA image \"%s\"\n", path);
                    return false;
                }
            }
            else
            {
                compressedSize = (uint16_t)((((uint32_t)width + 7) >> 3) * height);
                if (!EncodePCWStoredLayout(buffer, width, height, outBuffer, LOAD_BUFFER_SIZE - size))
                {
                    fprintf(stderr, "Error: Unable to encode PCW image \"%s\"\n", path);
                    return false;
                }
            }
            storedBuffer = outBuffer;
        }
        else if (!localCompressionEnabled)
        {
            uint8_t* outBuffer = buffer + size;
            compressedSize = (uint16_t)((((uint32_t)width + 7) >> 3) * height * 4u);
            if (!DMG_PackPlanar8Pixels(buffer, width, height, outBuffer))
            {
                fprintf(stderr, "Error: Unable to encode image \"%s\"\n", path);
                return false;
            }
            storedBuffer = outBuffer;
        }
        else if (localCompressionEnabled)
        {
            uint8_t* outBuffer = buffer + size;
            if (!CompressImage(buffer, size, outBuffer, LOAD_BUFFER_SIZE - size, &compressed, &compressedSize, debug))
            {
                fprintf(stderr, "Error: Unable to compress image \"%s\": %s\n", path, DMG_GetErrorString());
                return false;
            }
            if (!compressed && !dmg->littleEndian && (dmg->version == DMG_Version1 || dmg->version == DMG_Version2))
            {
                compressedSize = (uint16_t)((((uint32_t)width + 7) >> 3) * height * 4u);
                if (!DMG_PackPlanar8Pixels(buffer, width, height, outBuffer))
                {
                    fprintf(stderr, "Error: Unable to encode image \"%s\"\n", path);
                    return false;
                }
                storedBuffer = outBuffer;
            }
            else
            {
                storedBuffer = outBuffer;
            }
        }
        uint32_t classicPalette[16];
        BuildClassicPalette16(classicPalette, palette, paletteSize, firstColor);
        if (!DMG_SetEntryPalette(dmg, targetIndex, classicPalette))
        {
            fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
            return false;
        }
        if (!DMG_SetImageData(dmg, targetIndex, storedBuffer, width, height, compressedSize, compressed))
        {
            fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
            return false;
        }
        printf("%03d: Added image %s (%d bytes%s)\n", targetIndex, path, compressedSize, compressed ? ", compressed" : "");
    }

    DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)targetIndex);
    if (entry != 0)
    {
        if (effectiveOptions.hasX) entry->x = effectiveOptions.x;
        if (effectiveOptions.hasY) entry->y = effectiveOptions.y;
        if (effectiveOptions.hasBuffer)
        {
            if (effectiveOptions.buffer) entry->flags |= DMG_FLAG_BUFFERED;
            else entry->flags &= ~DMG_FLAG_BUFFERED;
        }
        if (effectiveOptions.hasFixed)
        {
            if (effectiveOptions.fixed) entry->flags |= DMG_FLAG_FIXED;
            else entry->flags &= ~DMG_FLAG_FIXED;
        }
        else if (entry->type == DMGEntry_Image)
        {
            entry->flags |= DMG_FLAG_FIXED;
        }
        if (effectiveOptions.hasAmigaHack)
        {
            if (effectiveOptions.amigaHack) entry->flags |= DMG_FLAG_AMIPALHACK;
            else entry->flags &= ~DMG_FLAG_AMIPALHACK;
        }
        if (effectiveOptions.hasFirstColor) entry->firstColor = (uint8_t)effectiveOptions.firstColor;
        else if (entry->type == DMGEntry_Image) entry->firstColor = (uint8_t)firstColor;
        if (effectiveOptions.hasLastColor) entry->lastColor = (uint8_t)effectiveOptions.lastColor;
        else if (entry->type == DMGEntry_Image) entry->lastColor = (uint8_t)lastColor;
        DMG_UpdateEntry(dmg, (uint8_t)targetIndex);
    }

    SelectSingleEntry(currentSelection, targetIndex);
    *explicitSelection = false;
    *currentIndex = targetIndex;
    *currentFileSaved = true;
    ResetPendingInputOptions();
    return true;
}

static bool ApplyAudioToken(DMG* dmg, const char* token, bool* currentSelection, bool* explicitSelection, int* currentIndex, bool* currentFileSaved)
{
    const char* path = token;
    const char* colon = strchr(token, ':');
    const char* slash = strrchr(token, '/');
    const char* backslash = strrchr(token, '\\');
    const char* filepart = slash ? slash + 1 : backslash ? backslash + 1 : token;
    int targetIndex = -1;

    if (HasPendingAudioUnsupportedOptions())
    {
        fprintf(stderr, "Error: Audio input \"%s\" does not support image-only options\n", token);
        return false;
    }

    if (colon != 0 && colon != token && isdigit(*token))
    {
        targetIndex = atoi(token);
        if (targetIndex > 255)
        {
            fprintf(stderr, "Error: Invalid index: %d\n", targetIndex);
            return false;
        }
        path = colon + 1;
    }
    else
    {
        int selectionCount = CountSelectedEntries(currentSelection);
        if (*explicitSelection && selectionCount > 1)
        {
            fprintf(stderr, "Error: Audio token \"%s\" requires a single selected entry or an explicit #:<file> target\n", token);
            return false;
        }
        if (*explicitSelection && selectionCount == 1)
            targetIndex = FirstSelectedEntry(currentSelection);
        else if (isdigit(*filepart) && atoi(filepart) < 256)
            targetIndex = atoi(filepart);
        else if (!*currentFileSaved)
            targetIndex = DMG_FindFreeEntry(dmg);
        else if (*currentIndex >= 0 && *currentIndex < 255)
            targetIndex = *currentIndex + 1;
        else
        {
            fprintf(stderr, "Error: Unable to determine target entry for \"%s\"\n", token);
            return false;
        }
    }

    DMG_KHZ sampleRate;
    uint32_t sampleSize = 0;
    if (!LoadWAVFile(path, &buffer, &bufferSize, &sampleSize, &sampleRate))
    {
        fprintf(stderr, "Error: Unable to load audio \"%s\": %s\n", path, DMG_GetErrorString());
        return false;
    }

    if (!DMG_SetAudioData(dmg, (uint8_t)targetIndex, buffer, (uint16_t)sampleSize, sampleRate))
    {
        fprintf(stderr, "Error: Unable to set audio data: %s\n", DMG_GetErrorString());
        return false;
    }

    DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)targetIndex);
    if (entry != 0)
    {
        if (pendingInputOptions.hasBuffer)
        {
            if (pendingInputOptions.buffer) entry->flags |= DMG_FLAG_BUFFERED;
            else entry->flags &= ~DMG_FLAG_BUFFERED;
        }
        if (pendingInputOptions.hasFixed)
        {
            if (pendingInputOptions.fixed) entry->flags |= DMG_FLAG_FIXED;
            else entry->flags &= ~DMG_FLAG_FIXED;
        }
        DMG_UpdateEntry(dmg, (uint8_t)targetIndex);
    }

    printf("%03d: Added audio %s (%u bytes, %s)\n", targetIndex, path, sampleSize, DMG_DescribeFreq(sampleRate));

    SelectSingleEntry(currentSelection, targetIndex);
    *explicitSelection = false;
    *currentIndex = targetIndex;
    *currentFileSaved = true;
    ResetPendingInputOptions();
    return true;
}

static bool ApplyFileToken(DMG* dmg, const char* token, bool* currentSelection, bool* explicitSelection, int* currentIndex, bool* currentFileSaved)
{
    const char* path = token;
    const char* colon = strchr(token, ':');
    if (colon != 0 && colon != token && isdigit(*token))
        path = colon + 1;

    if (IsAudioToken(path))
        return ApplyAudioToken(dmg, token, currentSelection, explicitSelection, currentIndex, currentFileSaved);
    return ApplyImageToken(dmg, token, currentSelection, explicitSelection, currentIndex, currentFileSaved);
}

static bool ParseEntryChanges(DMG* dmg, int tokenCount, const char* tokens[])
{
    int currentIndex = -1;
    bool currentFileSaved = false;
    bool pendingInputRequiresFile = false;
    bool currentSelection[256];
    bool explicitSelection = false;
    ClearSelection(currentSelection);
    ResetPendingInputOptions();

    for (int i = 0; i < tokenCount; i++)
    {
        const char* token = tokens[i];
        if (token == 0 || *token == 0)
            continue;

        if (IsPriorityToken(token))
        {
            if (!ParsePriorityToken(token))
            {
                fprintf(stderr, "Error: Invalid priority list: \"%s\"\n", token);
                return false;
            }
            continue;
        }

        if (IsRemapToken(token))
        {
            if (!ParseRemapMode(token, &remapMode))
            {
                fprintf(stderr, "Error: Invalid remap mode: \"%s\"\n", token);
                return false;
            }
            continue;
        }

        if (IsCompressionToken(token))
        {
            if (!ParseCompressionToken(token))
            {
                fprintf(stderr, "Error: Invalid compression setting: \"%s\"\n", token);
                return false;
            }
            continue;
        }

        if (IsGlobalImportSizeToken(token))
            continue;

        if (IsInputOptionToken(token))
        {
            if (!ParseInputOptionToken(token))
            {
                fprintf(stderr, "Error: Invalid input option: \"%s\"\n", token);
                return false;
            }
            pendingInputRequiresFile = true;

            if (action == ACTION_UPDATE && !currentFileSaved && CountSelectedEntries(currentSelection) > 0 && IsTrailingEntryOptionToken(token))
            {
                char propertyToken[64];
                if (!ConvertTrailingInputOptionToPropertyToken(token, propertyToken, sizeof(propertyToken)))
                {
                    fprintf(stderr, "Error: Invalid input option: \"%s\"\n", token);
                    return false;
                }
                if (!ApplyPropertyToSelection(dmg, currentSelection, propertyToken))
                    return false;
                ClearPendingTrailingEntryOptions();
                pendingInputRequiresFile = HasPendingTrailingEntryOptions() || HasPendingImageOnlyInputOptions();
                continue;
            }

            if (currentFileSaved && currentIndex >= 0 && IsTrailingEntryOptionToken(token))
            {
                if (!ApplyPendingEntryOptions(dmg, currentIndex))
                    return false;
                ResetPendingInputOptions();
                pendingInputRequiresFile = false;
            }
            continue;
        }

        if (IsCreateToken(token))
        {
            if (action == ACTION_UPDATE)
            {
                if (IsDAT5ModeToken(token))
                    continue;

                if (IsDAT5ScreenToken(token))
                {
                    if (dmg->version == DMG_Version5 && !createFormatExplicit)
                    {
                        if (!ApplyDAT5HeaderToken(dmg, token))
                            return false;
                    }
                    continue;
                }
            }
            continue;
        }

        if (IsSelectionToken(token))
        {
            if (!ParseSelectionToken(token, currentSelection))
            {
                fprintf(stderr, "Error: Invalid index list: \"%s\"\n", token);
                return false;
            }
            explicitSelection = true;
            currentIndex = FirstSelectedEntry(currentSelection);
            currentFileSaved = false;
            if (action == ACTION_UPDATE && HasPendingTrailingEntryOptions())
            {
                if (!ApplyPendingTrailingEntryOptionsToSelection(dmg, currentSelection))
                    return false;
            }
            continue;
        }

        if (IsPropertyToken(token))
        {
            if (!ApplyPropertyToSelection(dmg, currentSelection, token))
                return false;
            continue;
        }

        if (IsTargetedFileToken(token) || IsImageToken(token) || IsAudioToken(token))
        {
            if (!ApplyFileToken(dmg, token, currentSelection, &explicitSelection, &currentIndex, &currentFileSaved))
                return false;
            pendingInputRequiresFile = false;
            continue;
        }

        fprintf(stderr, "Error: Invalid argument: \"%s\"\n", token);
        return false;
    }

    if (HasPendingTrailingEntryOptions() || HasPendingImageOnlyInputOptions())
    {
        if (ignoreMissingWildcards && ignoredWildcardExpansionCount > 0 && pendingInputRequiresFile)
        {
            ResetPendingInputOptions();
            return true;
        }
        if (action == ACTION_UPDATE)
            fprintf(stderr, "Error: Option requires a following compatible input file, or an entry selection before it during update\n");
        else
            fprintf(stderr, "Error: Option requires a following compatible input file\n");
        return false;
    }

    return true;
}

bool RebuildDAT(DMG* dmg, const char* outputFileName)
{
	int n, i;
    uint8_t rebuildOrder[256];
    uint8_t firstEntry = dmg->version == DMG_Version5 ? dmg->firstEntry : 0;
    uint8_t lastEntry = dmg->version == DMG_Version5 ? dmg->lastEntry : 255;
    bool outputIsDAT5 = dmg->version == DMG_Version5 || createDAT5;
    DMG_DAT5ColorMode outputColorMode = dmg->version == DMG_Version5 ? (DMG_DAT5ColorMode)dmg->colorMode : DMG_DAT5_COLORMODE_NONE;
    uint16_t outputWidth = dmg->version == DMG_Version5 ? dmg->targetWidth : 320;
    uint16_t outputHeight = dmg->version == DMG_Version5 ? dmg->targetHeight : 200;

    if (createDAT5)
    {
        outputWidth = createDAT5Width;
        outputHeight = createDAT5Height;
        if (createDAT5Mode != DMG_DAT5_COLORMODE_NONE)
            outputColorMode = createDAT5Mode;
    }

    if (outputIsDAT5 && outputColorMode == DMG_DAT5_COLORMODE_NONE)
    {
        fprintf(stderr, "Error: DAT5 rebuild requires mode:<cga|ega|planar4|planar5|planar8|planar4st|planar8st>\n");
        return false;
    }

    if (outputIsDAT5)
    {
        if (!GetUsedEntryRange(dmg, &firstEntry, &lastEntry))
        {
            firstEntry = dmg->version == DMG_Version5 ? dmg->firstEntry : 0;
            lastEntry = firstEntry;
        }
    }
	DMG* out = outputIsDAT5 ?
        DMG_CreateDAT5(outputFileName, outputColorMode, outputWidth, outputHeight, firstEntry, lastEntry) :
        DMG_Create(outputFileName);
	if (out == NULL)
	{
		fprintf(stderr, "Error: Failed to create \"%s\": %s\n", outputFileName, DMG_GetErrorString());
		return false;
	}

	int rebuildCount = BuildRebuildOrder(dmg, rebuildOrder);
	for (i = 0; i < rebuildCount; i++)
	{
        n = rebuildOrder[i];
		DMG_Entry* entry = DMG_GetEntry(dmg, n);
		if (entry == NULL)
		{
			if (DMG_GetError() != DMG_ERROR_NONE)
				fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
			continue;
		}
		if (entry->type == DMGEntry_Empty)
			continue;
		DMG_Entry* outEntry = DMG_GetEntry(out, n);
		if (outEntry == NULL)
		{
			fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
			continue;
		}

        if (entry->RGB32Palette != 0)
        {
            uint16_t paletteSize = DMG_GetEntryPaletteSize(dmg, n);
            if (!DMG_SetEntryPaletteRange(out, n, entry->RGB32Palette, paletteSize, entry->firstColor, entry->lastColor))
            {
                fprintf(stderr, "%03d: Error: Unable to copy palette: %s\n", n, DMG_GetErrorString());
                continue;
            }
        }
        if (entry->type == DMGEntry_Image && dmg->version != DMG_Version5 && entry->conversionPalette != 0)
        {
            if (!DMG_SetEntryConversionPalette(out, n, entry->conversionPalette))
            {
                fprintf(stderr, "%03d: Error: Unable to copy conversion palette: %s\n", n, DMG_GetErrorString());
                continue;
            }
        }
        outEntry->bitDepth = entry->bitDepth;
        outEntry->flags = entry->flags;
		outEntry->x = entry->x;
		outEntry->y = entry->y;
		outEntry->firstColor = entry->firstColor;
		outEntry->lastColor = entry->lastColor;

		if (entry->type == DMGEntry_Audio)
		{
			uint16_t size = entry->length;
			uint8_t* data = DMG_GetEntryData(dmg, n, ImageMode_Audio);
			if (data == 0)
			{
				fprintf(stderr, "%03d: Error: Unable to read audio entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			if (!DMG_SetAudioData(out, n, data, size, (DMG_KHZ)entry->x))
			{
				fprintf(stderr, "Error: Unable to set audio data: %s\n", DMG_GetErrorString());
				DMG_Close(out);
				return false;
			}
			printf("%03d: Added audio (%5d bytes, %s)\n", n, size, DMG_DescribeFreq((DMG_KHZ)entry->x));
		}

		if (entry->type == DMGEntry_Image)
		{
			uint16_t width = entry->width;
			uint16_t height = entry->height;
			uint32_t size = width * height;
			bool compressed;
			uint16_t compressedSize;
			uint8_t* outPtr;
            uint32_t paletteBuffer[256];
            uint32_t classicPalette[16];
            uint32_t requiredBuffer = size + 64;

            if (out->version == DMG_Version5)
                requiredBuffer = size + GetDAT5EncodedSize((DMG_DAT5ColorMode)out->colorMode, width, height);

			if (bufferSize < requiredBuffer)
			{
				bufferSize = requiredBuffer;
				buffer = (uint8_t*)realloc(buffer, bufferSize);
				if (buffer == NULL)
				{
					fprintf(stderr, "Error: Out of memory: unable to allocate %d bytes\n", bufferSize);
					DMG_Close(out);
					return false;
				}
			}
			outPtr = buffer;

			uint8_t* inPtr = DMG_GetEntryData(dmg, n, ImageMode_Indexed);
			if (inPtr == 0)
			{
				fprintf(stderr, "%03d: Error: Unable to read image entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
            uint32_t* palettePtr = dmg->version == DMG_Version5 ? DMG_GetEntryStoredPalette(dmg, n) : DMG_GetEntryPalette(dmg, n);
            int paletteSize = dmg->version == DMG_Version5 ? entry->paletteColors : 16;
            int firstColor = dmg->version == DMG_Version5 ? entry->firstColor : 0;
            int lastColor = dmg->version == DMG_Version5 ? entry->lastColor : 15;
            if (out->version == DMG_Version5)
            {
                uint32_t encodedSize = 0;
                uint32_t storedSize = 0;
                uint8_t* storedBuffer = 0;
                if (paletteSize > 0 && DMG_DAT5ModeUsesPalette(out->colorMode))
                {
                    memcpy(paletteBuffer, palettePtr, paletteSize * sizeof(uint32_t));
                    if (remapMode != REMAP_NONE)
                        ApplyPaletteRemap(inPtr, size, paletteBuffer, &paletteSize, GetPaletteLimit((DMG_DAT5ColorMode)out->colorMode), &firstColor, &lastColor);
                    if (!DMG_SetEntryPaletteRange(out, n, paletteBuffer, paletteSize, (uint8_t)firstColor, (uint8_t)lastColor))
                    {
                        fprintf(stderr, "%03d: Error: Unable to copy palette: %s\n", n, DMG_GetErrorString());
                        DMG_Close(out);
                        return false;
                    }
                }
                if (!EncodeDAT5Image((DMG_DAT5ColorMode)out->colorMode, inPtr, width, height, outPtr, &encodedSize))
                {
                    fprintf(stderr, "%03d: Error: Unable to encode DAT5 image\n", n);
                    DMG_Close(out);
                    return false;
                }
                if (compressionEnabled)
                {
                    if (!CompressDAT5Image(outPtr, encodedSize, &storedBuffer, &storedSize, &compressed))
                    {
                        fprintf(stderr, "%03d: Error: Unable to compress DAT5 image\n", n);
                        DMG_Close(out);
                        return false;
                    }
                }
                else
                {
                    storedBuffer = outPtr;
                    storedSize = encodedSize;
                }
                if (!DMG_SetImageDataEx(out, n, storedBuffer, width, height, storedSize, compressed, GetBitDepthForMode((DMG_DAT5ColorMode)out->colorMode)))
                {
                    fprintf(stderr, "Error: Unable to set DAT5 image data: %s\n", DMG_GetErrorString());
                    if (compressed)
                        Free(storedBuffer);
                    DMG_Close(out);
                    return false;
                }
                printf("%03d: Added image (%5u bytes%s)\n", n, storedSize, compressed ? ", ZX0" : "");
                if (compressed)
                    Free(storedBuffer);
                continue;
            }

            if (IsClassicPaletteDAT(out) && palettePtr != 0)
            {
                memcpy(paletteBuffer, palettePtr, paletteSize * sizeof(uint32_t));
                if (remapMode != REMAP_NONE && paletteSize > 0)
                    ApplyPaletteRemap(inPtr, size, paletteBuffer, &paletteSize, 16, &firstColor, &lastColor);
                BuildClassicPalette16(classicPalette, paletteBuffer, paletteSize, firstColor);
                if (!DMG_SetEntryPalette(out, n, classicPalette))
                {
                    fprintf(stderr, "%03d: Error: Unable to copy palette: %s\n", n, DMG_GetErrorString());
                    DMG_Close(out);
                    return false;
                }
            }

            uint8_t* storedBuffer = inPtr;
            compressedSize = (uint16_t)size;
            if (compressionEnabled)
            {
			    if (!CompressImage(inPtr, size, outPtr, size, &compressed, &compressedSize, debug))
			    {
				    fprintf(stderr, "Error: Unable to compress image \"%s\": %s\n", outPtr, DMG_GetErrorString());
				    DMG_Close(out);
				    return false;
			    }
                if (!compressed && !out->littleEndian && (out->version == DMG_Version1 || out->version == DMG_Version2))
                {
                    compressedSize = (uint16_t)((((uint32_t)width + 7) >> 3) * height * 4u);
                    if (!DMG_PackPlanar8Pixels(inPtr, width, height, outPtr))
                    {
                        fprintf(stderr, "Error: Unable to encode image during rebuild\n");
                        DMG_Close(out);
                        return false;
                    }
                    storedBuffer = outPtr;
                }
                else
                {
                    storedBuffer = outPtr;
                }
            }
			if (!DMG_SetImageData(out, n, storedBuffer, width, height, compressedSize, compressed))
			{
				fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
				DMG_Close(out);
				return false;
			}
			printf("%03d: Added image (%5d bytes%s)\n", n, compressedSize, compressed ? ", compressed" : "");
		}
		else
		{
			DMG_UpdateEntry(out, n);
		}
	}
    if (!DMG_Save(out))
    {
        fprintf(stderr, "Error: Failed to save \"%s\": %s\n", outputFileName, DMG_GetErrorString());
        DMG_Close(out);
        return false;
    }
	DMG_Close(out);
	return true;
}

static bool ParseCreateArguments(int tokenCount, const char* tokens[])
{
    for (int i = 0; i < tokenCount; i++)
    {
        if (IsPriorityToken(tokens[i]))
        {
            if (!ParsePriorityToken(tokens[i]))
            {
                fprintf(stderr, "Error: Invalid priority list: \"%s\"\n", tokens[i]);
                return false;
            }
        }
        else if (IsRemapToken(tokens[i]))
        {
            if (!ParseRemapMode(tokens[i], &remapMode))
            {
                fprintf(stderr, "Error: Invalid remap mode: \"%s\"\n", tokens[i]);
                return false;
            }
        }
        else if (IsCompressionToken(tokens[i]))
        {
            if (!ParseCompressionToken(tokens[i]))
            {
                fprintf(stderr, "Error: Invalid compression setting: \"%s\"\n", tokens[i]);
                return false;
            }
        }
        else if (IsGlobalImportSizeToken(tokens[i]))
        {
            if (!ParseGlobalImportSizeToken(tokens[i]))
            {
                fprintf(stderr, "Error: Invalid import size setting: \"%s\"\n", tokens[i]);
                return false;
            }
        }
        else if (strnicmp(tokens[i], "mode:", 5) == 0)
        {
            createDAT5 = true;
            if (!ParseDAT5Mode(tokens[i] + 5, &createDAT5Mode))
            {
                fprintf(stderr, "Error: Invalid DAT5 mode: \"%s\"\n", tokens[i] + 5);
                return false;
            }
        }
        else if (strnicmp(tokens[i], "screen:", 7) == 0)
        {
            createDAT5 = true;
            if (!ParseDAT5Size(tokens[i] + 7, &createDAT5Width, &createDAT5Height))
            {
                fprintf(stderr, "Error: Invalid DAT5 screen size: \"%s\"\n", tokens[i] + 7);
                return false;
            }
        }
    }
    return true;
}

static bool ParseUpdateArguments(int tokenCount, const char* tokens[], const char** outputFileName, bool* hasExplicitOutputFile, int* editTokenCount, const char** editTokens)
{
    *outputFileName = 0;
    *hasExplicitOutputFile = false;
    *editTokenCount = 0;

    for (int i = 0; i < tokenCount; i++)
    {
        const char* token = tokens[i];
        if (IsPriorityToken(token))
        {
            editTokens[(*editTokenCount)++] = token;
            continue;
        }
        if (IsRemapToken(token))
        {
            editTokens[(*editTokenCount)++] = token;
            continue;
        }

        if (*outputFileName == 0 && !IsEntryChangeToken(token))
        {
            *outputFileName = token;
            *hasExplicitOutputFile = true;
            continue;
        }

        editTokens[(*editTokenCount)++] = token;
    }

    return true;
}

static int FilterUpdateEditTokens(DMG* dmg, int tokenCount, const char* tokens[], const char** filteredTokens)
{
    int filteredCount = 0;
    for (int i = 0; i < tokenCount; i++)
    {
        const char* token = tokens[i];
        if (IsDAT5ModeToken(token))
            continue;
        if (dmg->version != DMG_Version5 && IsDAT5ScreenToken(token))
            continue;
        filteredTokens[filteredCount++] = token;
    }
    return filteredCount;
}

static bool UpdateTokenRequiresRebuild(const char* token)
{
    if (token == 0 || *token == 0)
        return false;
    if (IsPriorityToken(token))
        return true;
    if (IsRemapToken(token))
        return true;
    if (IsCompressionToken(token))
        return true;
    if (IsDAT5ModeToken(token))
        return true;
    if (IsDAT5ScreenToken(token))
        return false;
    if (IsCreateToken(token))
        return true;
    if (IsTargetedFileToken(token) || IsImageToken(token) || IsAudioToken(token))
        return true;
    return false;
}

static bool UpdateRequiresRebuild(DMG* dmg, const char* outputFileName, int editTokenCount, const char* editTokens[])
{
    if (outputFileName != 0)
        return true;
    if (editTokenCount == 0)
        return true;
    if (dmg->version != DMG_Version5)
        return true;

    for (int i = 0; i < editTokenCount; i++)
    {
        if (UpdateTokenRequiresRebuild(editTokens[i]))
            return true;
    }
    return false;
}

static void ResetCommandExecutionState(Action commandAction)
{
    action = commandAction;
    extractMode = ImageMode_Indexed;
    ResetCreateSettings();
    remapMode = REMAP_NONE;
    remapRangeFirst = 0;
    remapRangeLast = 0;
    compressionEnabled = true;
    ignoreMissingWildcards = false;
    ResetPriorityEntries();
    ClearSelection(selected);
}

static bool ExecuteDMGAction(DMG* dmg, Action commandAction, int argumentCount, const char* arguments[])
{
    ResetCommandExecutionState(commandAction);

    switch (commandAction)
    {
        case ACTION_LIST:
            if (ParseEntrySelectionList(argumentCount, arguments))
            {
                ListSelectedEntries(dmg, verbose);
                return true;
            }
            return false;

        case ACTION_DELETE:
            if (ParseEntrySelectionList(argumentCount, arguments))
            {
                DeleteSelectedEntries(dmg);
                return true;
            }
            return false;

        case ACTION_EXTRACT:
        case ACTION_EXTRACT_PALETTES:
        case ACTION_TEST:
            if (ParseEntrySelectionList(argumentCount, arguments))
            {
                ExtractSelectedEntries(dmg, commandAction != ACTION_TEST, commandAction == ACTION_EXTRACT_PALETTES);
                return true;
            }
            return false;

        case ACTION_ADD:
            if (!ParseCreateArguments(argumentCount, arguments))
                return false;
            return ParseEntryChanges(dmg, argumentCount, arguments);

        case ACTION_UPDATE:
        {
            const char* outputFileName = 0;
            const char* editTokens[CLI_MAX_ARGUMENTS];
            int editTokenCount = 0;
            bool hasExplicitOutputFile = false;
            bool replaceOriginal = false;
            ParseUpdateArguments(argumentCount, arguments, &outputFileName, &hasExplicitOutputFile, &editTokenCount, editTokens);
            if (!ParseCreateArguments(argumentCount, arguments))
                return false;
            if (editTokenCount > 0 && !ParseEntryChanges(dmg, editTokenCount, editTokens))
                return false;
            if (!UpdateRequiresRebuild(dmg, outputFileName, editTokenCount, editTokens))
            {
                if (dmg->dirty)
                    printf("%s updated in place.\n", filename);
                else
                    printf("%s already up to date.\n", filename);
                return true;
            }

            if (!hasExplicitOutputFile)
            {
                outputFileName = ChangeExtension(filename, ".new");
                replaceOriginal = true;
            }

            if (!RebuildDAT(dmg, outputFileName))
                return false;

            if (replaceOriginal)
            {
                remove(filename);
                if (rename(outputFileName, filename) != 0)
                {
                    fprintf(stderr, "Error: Failed to replace \"%s\" with rebuilt file \"%s\"\n", filename, outputFileName);
                    return false;
                }
                printf("%s updated.\n", filename);
            }
            else
            {
                printf("%s written.\n", outputFileName);
            }
            return true;
        }

        default:
            fprintf(stderr, "Error: Unsupported command in this mode\n");
            return false;
    }
}

static const CLI_ActionSpec* FindCLIActionSpec(const char* token)
{
    if (token == 0)
        return 0;
    for (int i = 0; actionSpecs[i].name != 0; i++)
    {
        if (stricmp(actionSpecs[i].name, token) == 0)
            return &actionSpecs[i];
    }
    return 0;
}

typedef struct
{
    DMG* dmg;
    bool* keepRunning;
}
DMG_SessionContext;

static bool ExecuteSessionCommand(int argc, char* argv[], void* context)
{
    DMG_SessionContext* session = (DMG_SessionContext*)context;
    if (argc <= 0)
        return true;

    if (stricmp(argv[0], "exit") == 0 || stricmp(argv[0], "quit") == 0)
    {
        if (session->keepRunning != 0)
            *session->keepRunning = false;
        return true;
    }

    if (stricmp(argv[0], "help") == 0 || stricmp(argv[0], "h") == 0)
    {
        PrintHelp();
        return true;
    }

    const CLI_ActionSpec* actionSpec = FindCLIActionSpec(argv[0]);
    if (actionSpec == 0)
    {
        fprintf(stderr, "Error: Unknown session command: \"%s\"\n", argv[0]);
        return false;
    }

    Action commandAction = (Action)actionSpec->value;
    if (commandAction == ACTION_HELP || commandAction == ACTION_SHELL || commandAction == ACTION_NEW)
    {
        fprintf(stderr, "Error: Command \"%s\" is not available inside a DMG session\n", argv[0]);
        return false;
    }

    verbose = actionSpec->canonicalName != 0 && stricmp(actionSpec->canonicalName, "list-palettes") == 0;

    ignoreMissingWildcards = false;
    ScanIgnoreMissingArguments(argc - 1, (const char**)argv + 1);

    const char* expandedArgs[CLI_MAX_ARGUMENTS * 4];
    bool expandedArgOwned[CLI_MAX_ARGUMENTS * 4];
    int expandedArgCount = ExpandArgumentTokens(argc - 1, (const char**)argv + 1, expandedArgs, expandedArgOwned, CLI_MAX_ARGUMENTS * 4);
    if (expandedArgCount < 0)
    {
        fprintf(stderr, "Error: %s\n", GetCommandError());
        return false;
    }

    const char* translatedArgs[CLI_MAX_ARGUMENTS * 4];
    int translatedArgCount = 0;
    const char* explicitOutputFile = 0;
    bool ok = TranslateModernArguments(commandAction, expandedArgCount, expandedArgs, translatedArgs, &translatedArgCount, &explicitOutputFile);
    if (!ok)
    {
        fprintf(stderr, "Error: %s\n", GetCommandError());
    }
    else if (explicitOutputFile != 0)
    {
        fprintf(stderr, "Error: Command \"%s\" does not support -output/--output inside session mode\n", argv[0]);
        ok = false;
    }
    else
    {
        ok = ExecuteDMGAction(session->dmg, commandAction, translatedArgCount, translatedArgs);
    }

    FreeExpandedArgumentTokens(expandedArgCount, expandedArgs, expandedArgOwned);
    return ok;
}

static bool RunSessionCommands(int argc, char* argv[], DMG* dmg)
{
    char errorBuffer[256];
    DMG_SessionContext context = { dmg, 0 };

    if (argc == 1 && argv[0][0] == '@' && argv[0][1] != 0)
    {
        if (!Session_ExecuteCommandFile(argv[0] + 1, "::", ExecuteSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
        {
            fprintf(stderr, "Error: %s\n", errorBuffer);
            return false;
        }
        return true;
    }

    if (!Session_ExecuteTokenStream(argc, argv, "::", ExecuteSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
    {
        fprintf(stderr, "Error: %s\n", errorBuffer);
        return false;
    }
    return true;
}

static bool RunInteractiveSession(DMG* dmg)
{
    printf("Entering interactive session for %s. Type HELP for commands, EXIT to quit.\n", filename);
    bool keepRunning = true;
    char line[512];
    while (keepRunning)
    {
        printf("dmg %s> ", filename);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }

        char* argv[64];
        int argc = Session_TokenizeLine(line, argv, 64);
        if (argc == 0)
            continue;

        char errorBuffer[256];
        DMG_SessionContext context = { dmg, &keepRunning };
        if (!Session_ExecuteTokenStream(argc, argv, "::", ExecuteSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
            fprintf(stderr, "Error: %s\n", errorBuffer);
    }
    return true;
}

static bool PrepareSessionTarget(int argc, char* argv[], DMG** outDmg)
{
    char parseError[256];
    CLI_CommandLine commandLine;
    char* parseArgv[CLI_MAX_ARGUMENTS + 1];
    parseArgv[0] = (char*)"dmg";
    for (int i = 0; i < argc && i < CLI_MAX_ARGUMENTS; i++)
        parseArgv[i + 1] = argv[i];

    if (!CLI_ParseCommandLine(argc + 1, parseArgv, actionSpecs, ACTION_LIST, optionSpecs, &commandLine, parseError, sizeof(parseError)))
    {
        fprintf(stderr, "Error: %s\n", parseError);
        return false;
    }

    if (CLI_HasOption(&commandLine, DMG_OPTION_HELP))
    {
        PrintHelp();
        return false;
    }

    if (commandLine.actionName != 0)
    {
        fprintf(stderr, "Error: Batch mode only accepts a DAT file before --\n");
        return false;
    }

    if (commandLine.argumentCount < 1)
    {
        fprintf(stderr, "Error: Missing filename before --\n");
        return false;
    }

    debug = CLI_HasOption(&commandLine, DMG_OPTION_VERBOSE);
    listSortById = !CLI_HasOption(&commandLine, DMG_OPTION_FILE_ORDER);

    if (strlen(commandLine.arguments[0]) > 1000)
    {
        fprintf(stderr, "Error: Invalid filename: \"%s\"\n", commandLine.arguments[0]);
        return false;
    }
    strcpy(filename, commandLine.arguments[0]);

    *outDmg = DMG_Open(filename, false);
    if (*outDmg == NULL)
    {
        fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
        return false;
    }

    return true;
}

static bool CopyFileExact(const char* sourceFileName, const char* destFileName)
{
    File* src = File_Open(sourceFileName, ReadOnly);
    if (src == 0)
        return false;
    File* dst = File_Create(destFileName);
    if (dst == 0)
    {
        File_Close(src);
        return false;
    }

    uint8_t* temp = Allocate<uint8_t>("DMG copy", 65536);
    if (temp == 0)
    {
        File_Close(dst);
        File_Close(src);
        return false;
    }

    bool ok = true;
    while (true)
    {
        uint64_t read = File_Read(src, temp, 65536);
        if (read == 0)
            break;
        if (File_Write(dst, temp, read) != read)
        {
            ok = false;
            break;
        }
    }

    Free(temp);
    File_Close(dst);
    File_Close(src);
    return ok;
}

static bool TranslateModernArguments(Action action, int inputCount, const char* inputArgs[], const char** translatedArgs, int* translatedCount, const char** explicitOutputFile)
{
    const int maxTranslatedArgs = CLI_MAX_ARGUMENTS * 4;
    int nextId = -1;
    bool haveExplicitId = false;
    *translatedCount = 0;
    *explicitOutputFile = 0;
    ClearCommandError();

    for (int i = 0; i < inputCount; i++)
    {
        const char* token = inputArgs[i];
        const char* inlineValue;
        if (token == 0 || *token == 0)
            continue;

        if (MatchFlagToken(token, "--ignore-missing"))
        {
            ignoreMissingWildcards = true;
            continue;
        }

        inlineValue = MatchOptionValueToken(token, "--format");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            bool isDAT5 = false;
            DMG_Version legacyVersion = DMG_Version2;
            if (value == 0)
                return false;
            if (!ParseContainerFormat(value, &isDAT5, &legacyVersion))
                return SetCommandError("Invalid container format \"%s\" for option \"%s\"", value, token);
            createFormatExplicit = true;
            createDAT5 = isDAT5;
            createLegacyVersion = legacyVersion;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--mode");
        if (inlineValue == 0)
            inlineValue = MatchOptionValueToken(token, "--type");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "mode:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--screen");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "screen:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--id");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            nextId = atoi(value);
            haveExplicitId = true;
            continue;
        }
        if (MatchFlagToken(token, "--fixed")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-fixed:1", token)) return false; continue; }
        if (MatchFlagToken(token, "--float")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-fixed:0", token)) return false; continue; }
        if (MatchFlagToken(token, "--buffer")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-buffer:1", token)) return false; continue; }
        if (MatchFlagToken(token, "--nobuffer")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-buffer:0", token)) return false; continue; }
        if (MatchFlagToken(token, "--compress")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "compression:1", token)) return false; continue; }
        if (MatchFlagToken(token, "--nocompress")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "compression:0", token)) return false; continue; }
        if (MatchFlagToken(token, "--amiga-hack")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-amiga-hack:1", token)) return false; continue; }
        if (MatchFlagToken(token, "--noamiga-hack")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-amiga-hack:0", token)) return false; continue; }
        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && MatchFlagToken(token, "--nojson")) { if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-json:off", token)) return false; continue; }
        inlineValue = MatchOptionValueToken(token, "--x");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-x:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--y");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-y:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--pcs");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-pcs:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--pce");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-pce:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--width");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-width:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--height");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-height:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--clone");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-clone:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--json");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (stricmp(value, "off") == 0)
            {
                if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-json:off", token))
                    return false;
            }
            else if (stricmp(value, "auto") == 0)
            {
                if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, "set-json:auto", token))
                    return false;
            }
            else if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "set-json-file:", value, token))
            {
                return false;
            }
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--remap");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "remap:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--priority");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "priority:", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--as");
        if ((action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES) && inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "--as=", value, token))
                return false;
            continue;
        }
        inlineValue = MatchOptionValueToken(token, "--output");
        if (inlineValue != 0)
        {
            const char* value = ConsumeOptionValue(token, inlineValue, inputCount, inputArgs, &i);
            if (value == 0)
                return false;
            if (action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES || action == ACTION_TEST)
            {
                if (!AppendTranslatedPrefixed(translatedArgs, translatedCount, maxTranslatedArgs, "out:", value, token))
                    return false;
            }
            else
                *explicitOutputFile = value;
            continue;
        }

        if (haveExplicitId)
        {
            char idToken[16];
            snprintf(idToken, sizeof(idToken), "%d", nextId);
            if (!AppendTranslatedDuplicate(translatedArgs, translatedCount, maxTranslatedArgs, idToken, token))
                return false;
            nextId++;
        }
        if (!AppendTranslatedArgument(translatedArgs, translatedCount, maxTranslatedArgs, token, token))
            return false;
    }
    return true;
}

static bool ExecuteCLICommandLine(int argc, char *argv[])
{
    DMG* dmg = 0;
    const char* workingFileName = 0;
    const char* explicitOutputFile = 0;
    const char* translatedArgs[CLI_MAX_ARGUMENTS * 4];
    const char* expandedArgs[CLI_MAX_ARGUMENTS * 4];
    bool expandedArgOwned[CLI_MAX_ARGUMENTS * 4];
    const char* finalOutputFile = 0;
    int translatedCount = 0;
    int expandedCount = 0;
    int argIndex = 1;
    bool actionSet = false;
    bool filenameSet = false;
    bool ok = true;
    bool changed = false;
    bool replaceTarget = false;
    bool result = false;
    bool saveAttempted = false;

    if (argc < 2)
    {
        PrintHelp();
        return true;
    }

    ResetCreateSettings();
    ResetPriorityEntries();
    remapMode = REMAP_NONE;
    remapRangeFirst = 0;
    remapRangeLast = 0;
    compressionEnabled = true;
    ignoreMissingWildcards = false;
    ignoredWildcardExpansionCount = 0;
    debug = false;
    listSortById = true;
    verbose = false;

    action = ACTION_LIST;
    while (argIndex < argc)
    {
        const char* token = argv[argIndex];
        if (strcmp(token, "-v") == 0 || strcmp(token, "--verbose") == 0)
        {
            argIndex++;
            debug = true;
            continue;
        }
        if (strcmp(token, "-n") == 0 || strcmp(token, "--sort-by-id") == 0)
        {
            argIndex++;
            listSortById = true;
            continue;
        }
        if (strcmp(token, "--file-order") == 0)
        {
            argIndex++;
            listSortById = false;
            continue;
        }
        if (strcmp(token, "-h") == 0 || strcmp(token, "--help") == 0)
        {
            PrintHelp();
            return true;
        }
        if (MatchFlagToken(token, "--ignore-missing"))
        {
            ignoreMissingWildcards = true;
            argIndex++;
            continue;
        }

        if (!actionSet)
        {
            const CLI_ActionSpec* actionSpec = FindCLIActionSpec(token);
            if (actionSpec != 0)
            {
                action = (Action)actionSpec->value;
                verbose = actionSpec->canonicalName != 0 && stricmp(actionSpec->canonicalName, "list-palettes") == 0;
                actionSet = true;
                argIndex++;
                continue;
            }
        }

        if (!filenameSet)
        {
            StrCopy(filename, sizeof(filename), token);
            filenameSet = true;
            argIndex++;
            continue;
        }

        if (!actionSet)
        {
            const CLI_ActionSpec* actionSpec = FindCLIActionSpec(token);
            if (actionSpec != 0)
            {
                action = (Action)actionSpec->value;
                verbose = actionSpec->canonicalName != 0 && stricmp(actionSpec->canonicalName, "list-palettes") == 0;
                actionSet = true;
                argIndex++;
                continue;
            }
        }

        break;
    }

    if (!filenameSet)
    {
        fprintf(stderr, "Error: Missing filename\n");
        return false;
    }

    ScanIgnoreMissingArguments(argc - argIndex, (const char**)argv + argIndex);

    expandedCount = ExpandArgumentTokens(argc - argIndex, (const char**)argv + argIndex, expandedArgs, expandedArgOwned, CLI_MAX_ARGUMENTS * 4);
    if (expandedCount < 0)
    {
        fprintf(stderr, "Error: %s\n", GetCommandError());
        goto cleanup;
    }

    if (!TranslateModernArguments(action, expandedCount, expandedArgs, translatedArgs, &translatedCount, &explicitOutputFile))
    {
        fprintf(stderr, "Error: %s\n", GetCommandError());
        goto cleanup;
    }

    if (action == ACTION_HELP)
    {
        PrintHelp();
        result = true;
        goto cleanup;
    }

    if (action == ACTION_LIST || action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES || action == ACTION_TEST)
    {
        dmg = DMG_Open(filename, true);
        if (dmg == 0)
        {
            fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
            goto cleanup;
        }
        bool ok = ParseEntrySelectionList(translatedCount, translatedArgs);
        if (ok)
        {
            if (action == ACTION_LIST) ListSelectedEntries(dmg, verbose);
            else ExtractSelectedEntries(dmg, action != ACTION_TEST, action == ACTION_EXTRACT_PALETTES);
        }
        DMG_Close(dmg);
        dmg = 0;
        result = ok;
        goto cleanup;
    }

    if (action == ACTION_SHELL)
    {
        dmg = DMG_Open(filename, false);
        if (dmg == 0)
        {
            fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
            goto cleanup;
        }
        bool ok = RunInteractiveSession(dmg);
        if (ok && dmg->dirty)
            ok = DMG_Save(dmg);
        DMG_Close(dmg);
        dmg = 0;
        result = ok;
        goto cleanup;
    }

    if (action == ACTION_NEW)
    {
        workingFileName = BuildWorkingFileName(filename);
        if (!ParseCreateArguments(translatedCount, translatedArgs))
            goto cleanup;
        if (createDAT5)
        {
            uint8_t firstEntry = 0;
            uint8_t lastEntry = 255;
            if (createDAT5Mode == DMG_DAT5_COLORMODE_NONE)
            {
                fprintf(stderr, "Error: DAT5 creation requires --mode\n");
                goto cleanup;
            }
            InferDAT5CreateRange(translatedCount, translatedArgs, &firstEntry, &lastEntry);
            dmg = DMG_CreateDAT5(workingFileName, createDAT5Mode, createDAT5Width, createDAT5Height, firstEntry, lastEntry);
        }
        else
        {
            dmg = DMG_CreateFormat(workingFileName, createLegacyVersion);
        }
        if (dmg == 0)
        {
            fprintf(stderr, "Error: Failed to create \"%s\" (working file \"%s\"): %s\n", filename, workingFileName, DMG_GetErrorString());
            goto cleanup;
        }
        bool ok = ParseEntryChanges(dmg, translatedCount, translatedArgs);
        saveAttempted = false;
        if (ok)
        {
            ApplySavePriorityHints(dmg);
            if (!dmg->dirty)
                dmg->dirty = true;
            saveAttempted = true;
            ok = DMG_Save(dmg);
        }
        if (!ok && saveAttempted)
            fprintf(stderr, "Error: Failed to write \"%s\" (working file \"%s\"): %s\n", filename, workingFileName, DMG_GetErrorString());
        DMG_Close(dmg);
        dmg = 0;
        if (!ok)
        {
            remove(workingFileName);
            goto cleanup;
        }
        remove(filename);
        result = rename(workingFileName, filename) == 0;
        goto cleanup;
    }

    if (action == ACTION_UPDATE)
    {
        const char* editTokens[CLI_MAX_ARGUMENTS];
        int editTokenCount = translatedCount;
        bool replaceTarget = explicitOutputFile == 0 || strcmp(explicitOutputFile, filename) == 0;
        const char* finalOutputFile = explicitOutputFile != 0 ? explicitOutputFile : filename;
        for (int i = 0; i < translatedCount; i++)
            editTokens[i] = translatedArgs[i];

        dmg = DMG_Open(filename, false);
        if (dmg == 0)
        {
            fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
            goto cleanup;
        }

        bool ok = ParseCreateArguments(translatedCount, translatedArgs);
        if (ok && editTokenCount > 0)
            ok = ParseEntryChanges(dmg, editTokenCount, editTokens);
        if (!ok)
        {
            DMG_Close(dmg);
            dmg = 0;
            goto cleanup;
        }

        if (!UpdateRequiresRebuild(dmg, explicitOutputFile, editTokenCount, editTokens))
        {
            bool modifiedInPlace = dmg->dirty;
            if (modifiedInPlace)
            {
                ApplySavePriorityHints(dmg);
                ok = DMG_Save(dmg);
            }
            DMG_Close(dmg);
            dmg = 0;
            if (!ok)
            {
                fprintf(stderr, "Error: Failed to update \"%s\": %s\n", filename, DMG_GetErrorString());
                goto cleanup;
            }
            if (modifiedInPlace)
                printf("%s updated in place.\n", filename);
            else
                printf("%s already up to date.\n", filename);
            result = true;
            goto cleanup;
        }

        workingFileName = replaceTarget ? BuildWorkingFileName(finalOutputFile) : finalOutputFile;
        ok = RebuildDAT(dmg, workingFileName);
        DMG_Close(dmg);
        dmg = 0;
        if (!ok)
        {
            if (replaceTarget)
                remove(workingFileName);
            goto cleanup;
        }

        if (replaceTarget)
        {
            remove(finalOutputFile);
            if (rename(workingFileName, finalOutputFile) != 0)
            {
                fprintf(stderr, "Error: Failed to replace \"%s\"\n", finalOutputFile);
                goto cleanup;
            }
            printf("%s updated.\n", finalOutputFile);
        }
        else
        {
            printf("%s written.\n", finalOutputFile);
        }
        result = true;
        goto cleanup;
    }

    finalOutputFile = explicitOutputFile != 0 ? explicitOutputFile : filename;
    replaceTarget = explicitOutputFile == 0 || strcmp(explicitOutputFile, filename) == 0;
    workingFileName = replaceTarget ? BuildWorkingFileName(finalOutputFile) : finalOutputFile;
    if (!CopyFileExact(filename, workingFileName))
    {
        fprintf(stderr, "Error: Failed to create working copy \"%s\"\n", workingFileName);
        goto cleanup;
    }

    dmg = DMG_Open(workingFileName, false);
    if (dmg == 0)
    {
        fprintf(stderr, "Error: Failed to open \"%s\": %s\n", workingFileName, DMG_GetErrorString());
        goto cleanup;
    }

    ok = true;
    changed = false;
    if (action == ACTION_DELETE)
    {
        ok = ParseEntrySelectionList(translatedCount, translatedArgs);
        if (ok)
            DeleteSelectedEntries(dmg);
    }
    else
    {
        ok = ParseCreateArguments(translatedCount, translatedArgs);
        if (ok)
            ok = ParseEntryChanges(dmg, translatedCount, translatedArgs);
    }
    changed = ok && dmg->dirty;
    saveAttempted = false;
    if (changed)
    {
        ApplySavePriorityHints(dmg);
        saveAttempted = true;
        ok = DMG_Save(dmg);
    }
    if (!ok && saveAttempted)
        fprintf(stderr, "Error: Failed to write \"%s\": %s\n", workingFileName, DMG_GetErrorString());
    DMG_Close(dmg);
    dmg = 0;

    if (!ok)
    {
        if (replaceTarget)
            remove(workingFileName);
        goto cleanup;
    }

    if (replaceTarget && !changed)
    {
        remove(workingFileName);
        result = true;
        goto cleanup;
    }

    if (replaceTarget)
    {
        remove(finalOutputFile);
        if (rename(workingFileName, finalOutputFile) != 0)
        {
            fprintf(stderr, "Error: Failed to replace \"%s\"\n", finalOutputFile);
            goto cleanup;
        }
    }
    result = true;

cleanup:
    if (dmg != 0)
        DMG_Close(dmg);
    FreeExpandedArgumentTokens(expandedCount, expandedArgs, expandedArgOwned);
    return result;
}

static bool RunCommandLine(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintHelp();
        return true;
    }

    int separatorIndex = -1;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            separatorIndex = i;
            break;
        }
    }

    if (separatorIndex >= 0)
    {
        DMG* dmg = 0;
        if (separatorIndex == 1)
        {
            fprintf(stderr, "Error: Missing filename before --\n");
            return false;
        }
        if (!PrepareSessionTarget(separatorIndex - 1, argv + 1, &dmg))
            return false;
        if (separatorIndex + 1 >= argc)
        {
            fprintf(stderr, "Error: Missing session command after --\n");
            DMG_Close(dmg);
            return false;
        }
        bool ok = RunSessionCommands(argc - separatorIndex - 1, argv + separatorIndex + 1, dmg);
        if (ok && dmg->dirty)
            ok = DMG_Save(dmg);
        DMG_Close(dmg);
        return ok;
    }

    if (argc >= 3 && argv[argc - 1][0] == '@' && argv[argc - 1][1] != 0)
    {
        DMG* dmg = 0;
        if (!PrepareSessionTarget(argc - 2, argv + 1, &dmg))
            return false;
        bool ok = RunSessionCommands(1, argv + argc - 1, dmg);
        if (ok && dmg->dirty)
            ok = DMG_Save(dmg);
        DMG_Close(dmg);
        return ok;
    }

    return ExecuteCLICommandLine(argc, argv);
}

int main (int argc, char *argv[])
{
    DMG_SetWarningHandler(ShowWarning);
    return RunCommandLine(argc, argv) ? 0 : 1;
}
