#include <dmg.h>
#include <dmg_pack.h>
#include <img.h>
#include <cli_parser.h>
#include <ddb.h>
#include <os_lib.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define LOAD_BUFFER_SIZE 1024*1024*3

static bool debug = false;
static const uint32_t DAT5_MIN_ZX0_SAVINGS_BYTES = 32;
static const uint32_t DAT5_MIN_ZX0_SAVINGS_PERCENT = 1;

static const char* imageExtensions[] = { "png", "pcx", "vga" };
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
static char filename[1024];
static char newfilename[1024];
static bool selected[256];
static bool verbose = false;
static bool listSortById = false;
static bool readOnly = true;
static bool createDAT5 = false;
static DMG_DAT5ColorMode createDAT5Mode = DMG_DAT5_COLORMODE_NONE;
static uint16_t createDAT5Width = 320;
static uint16_t createDAT5Height = 200;
static bool compressionEnabled = true;

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
	ACTION_HELP,
}
Action;

static Action action = ACTION_LIST;

typedef enum
{
    DMG_OPTION_VERBOSE = 1,
    DMG_OPTION_SORT_BY_ID,
}
DmgOption;

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
	printf("Usage: dmg [action] [global options] <file.dat> [arguments]\n\n");
	printf("Actions:\n\n");
	printf("    l     List contents of DAT file (default)\n");
	printf("    v     List contents of DAT file (with palettes)\n");
	printf("    x     Extract image/audio entries\n");
	printf("    t     Test image/audio entries\n");
	printf("    p     Extract image palettes\n");
	printf("    a     Add new images or audio entries\n");
	printf("    e     Edit image or audio entries\n");
	printf("    d     Delete image/audio entries\n");
	printf("    n     Create a new DAT file\n");
	printf("    u     Update/rebuild DAT file to newer version\n");
	printf("    h     Show this help\n");
    printf("\n");
    printf("Global options:\n\n");
    printf("   -v     Enable verbose/debug output\n");
    printf("   -n     List entries by id instead of file offset\n");
    printf("\n");
	printf("Selectors:\n\n");
	printf("   #          Select one entry (0-255)\n");
	printf("   #,#...     Select a comma-separated list of entries\n");
	printf("   #-#        Select an inclusive range of entries\n");
	printf("   #-         Select from the given entry to 255\n");
	printf("\n");
	printf("Extract/test options:\n\n");
	printf("   If no selector is supplied, all entries are used.\n");
	printf("   -i         Save indexed images (default)\n");
	printf("   -t         Save truecolor images\n");
	printf("   -e         Export EGA version of images/palettes\n");
	printf("   -c         Export CGA version of images/palettes\n");
	printf("\n");
	printf("Add/edit/update arguments:\n\n");
	printf("   Selectors set the current target entries.\n");
	printf("   Following property tokens apply to the current target entries\n");
	printf("   until another selector appears.\n");
	printf("   remap:* tokens affect DAT5 image writes/rebuilds for this command.\n");
	printf("   Image/audio files affect one entry only.\n");
	printf("   Use #:<file> to target a specific entry.\n\n");
	printf("   file.png   Add/replace image contents & palette\n");
    printf("   file.pcx   Add/replace image contents & palette\n");
    printf("   file.vga   Add/replace image contents & palette\n");
	printf("   file.wav   Add/replace audio sample contents\n");
	printf("   #:<file>   Add/replace image or audio in a specific entry\n");
	printf("   x:#        Set X coordinate\n");
	printf("   y:#        Set Y coordinate\n");
	printf("   first:#    Set first color\n");
	printf("   last:#     Set last color\n");
	printf("   cga:#      Set CGA mode (red or blue)\n");
	printf("   freq:#     Set frequency (5, 7, 9.5, 15, 20 or 30)\n");
	printf("   buffer:#   Set buffer flag (0 or 1)\n");
    printf("   fixed:#    Set fixed flag (0 or 1)\n");
    printf("   remap:min  Remap DAT5 palettes by fixing only slots 0 and 15\n");
    printf("   remap:reserve Reserve low color slots by shifting the image palette up\n");
    printf("                up to 16 slots, or as many as the mode still allows\n");
    printf("   remap:std  Remap DAT5 palettes using the standard bright-slot order\n");
    printf("   remap:dark Remap DAT5 palettes strictly from dark to bright\n");
    printf("   remap:A-B  Shift image palette indices into color range A-B when it fits\n");
    printf("   compression:# Set image compression for a/n/u (0 or 1)\n");
    printf("   priority:#,#... Prioritize physical DAT order for these entries on n/u\n");
    printf("   mode:<id>  When creating a DAT5: cga, ega, i16, i32, i256\n");
    printf("   screen:WxH When creating a DAT5: 320x200, 640x200 or 640x400\n");
    printf("\n");
    printf("Examples:\n\n");
    printf("   dmg u file.dat 0-50 fixed:1\n");
    printf("   dmg e file.dat 12 x:24 y:80\n");
    printf("   dmg a file.dat remap:std 0:image.png\n");
    printf("   dmg u file.dat out.dat 0-50 fixed:1\n");
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
        case DMG_DAT5_COLORMODE_I16: return 16;
        case DMG_DAT5_COLORMODE_I32: return 32;
        case DMG_DAT5_COLORMODE_I256: return 256;
        default: return 16;
    }
}

static uint8_t GetBitDepthForMode(DMG_DAT5ColorMode mode)
{
    switch (mode)
    {
        case DMG_DAT5_COLORMODE_CGA: return 0;
        case DMG_DAT5_COLORMODE_EGA: return 0;
        case DMG_DAT5_COLORMODE_I16: return 4;
        case DMG_DAT5_COLORMODE_I32: return 5;
        case DMG_DAT5_COLORMODE_I256: return 8;
        default: return 4;
    }
}

static bool ParseDAT5Mode(const char* value, DMG_DAT5ColorMode* mode)
{
    if (stricmp(value, "cga") == 0) *mode = DMG_DAT5_COLORMODE_CGA;
    else if (stricmp(value, "ega") == 0) *mode = DMG_DAT5_COLORMODE_EGA;
    else if (stricmp(value, "i16") == 0 || stricmp(value, "st") == 0) *mode = DMG_DAT5_COLORMODE_I16;
    else if (stricmp(value, "i32") == 0 || stricmp(value, "ocs") == 0 || stricmp(value, "amiga") == 0) *mode = DMG_DAT5_COLORMODE_I32;
    else if (stricmp(value, "i256") == 0 || stricmp(value, "vga") == 0 || stricmp(value, "aga") == 0) *mode = DMG_DAT5_COLORMODE_I256;
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

static void ClearSelection(bool* entries);
static int CountSelectedEntries(const bool* entries);
static int FirstSelectedEntry(const bool* entries);
static void SelectSingleEntry(bool* entries, int index);
static bool ParseSelectionToken(const char* token, bool* entries);
static bool IsPropertyToken(const char* token);
static bool IsImageToken(const char* token);
static bool IsTargetedFileToken(const char* token);

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

        if (IsTargetedFileToken(token) || IsImageToken(token))
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
        case DMG_DAT5_COLORMODE_I16:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 4 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 4, output);
        case DMG_DAT5_COLORMODE_I32:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 5 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 5, output);
        case DMG_DAT5_COLORMODE_I256:
            *outputSize = ((uint32_t)(width + 15) >> 4) * height * 8 * 2;
            return DMG_PackBitplaneBytes(indexed, width, height, 8, output);
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
        case DMG_DAT5_COLORMODE_I16:
            return ((uint32_t)(width + 15) >> 4) * height * 4 * 2;
        case DMG_DAT5_COLORMODE_I32:
            return ((uint32_t)(width + 15) >> 4) * height * 5 * 2;
        case DMG_DAT5_COLORMODE_I256:
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

    OSFree(zx0Data);
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

const char* MakeFileName(const char* original, int index, const char* extension)
{
	char* ptr = newfilename;

	while (ptr < newfilename + sizeof(newfilename) - 16 && *original)
		*ptr++ = *original++;
	*ptr = 0;
	while (ptr > newfilename && *ptr != '\\' && *ptr != '/')
		ptr--;
	if (*ptr == '\\' || *ptr == '/')
		ptr++;
	snprintf(ptr, newfilename+sizeof(newfilename)-ptr, "%03d.%s", index, extension);
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
						outputFileName = MakeFileName(filename, n, "col");
                        palette = (uint32_t*)DMG_GetEntryStoredPalette(dmg, n);
						success = SaveCOLPalette(outputFileName, palette, DMG_GetEntryPaletteSize(dmg, n));
						if (!success)
						{
							fprintf(stderr, "%03d: Error: Unable to write palette to %s\n", n, outputFileName);
							continue;
						}
						printf("%s saved\n", outputFileName);
					}
					else if (saveToFile)
					{
						outputFileName = MakeFileName(filename, n, "png");
						if (DMG_IS_INDEXED(extractMode))
						{
                            palette = DMG_GetEntryStoredPalette(dmg, n);
							success = SavePNGIndexed(outputFileName, buffer, entry->width, entry->height, palette, DMG_GetEntryPaletteSize(dmg, n), 0);
						}
						else if (DMG_IS_RGBA32(extractMode))
						{
							success = SavePNGRGB32(outputFileName, (uint32_t*)buffer, entry->width, entry->height);
						}
						if (!success)
						{
							fprintf(stderr, "%03d: Error: Unable to write image to %s\n", n, outputFileName);
							continue;
						}
						printf("%s saved\n", outputFileName);
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
					uint8_t* buffer = DMG_GetEntryData(dmg, n, extractMode);
					if (buffer == 0)
					{
						fprintf(stderr, "%03d: Error: Unable to read audio entry: %s\n", n, DMG_GetErrorString());
						continue;
					}
					if (saveToFile)
					{
						outputFileName = MakeFileName(filename, n, "wav");
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
        case DMG_DAT5_COLORMODE_CGA:  return "CGA";
        case DMG_DAT5_COLORMODE_EGA:  return "EGA";
        case DMG_DAT5_COLORMODE_I16:  return "I16";
        case DMG_DAT5_COLORMODE_I32:  return "I32";
        case DMG_DAT5_COLORMODE_I256: return "I256";
        default:                      return "Unknown";
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
                return ((uint32_t)entry->width * entry->height + 3) / 4;
            case DMG_DAT5_COLORMODE_EGA:
                return ((uint32_t)entry->width * entry->height + 1) / 2;
            case DMG_DAT5_COLORMODE_I16:
            case DMG_DAT5_COLORMODE_I32:
            case DMG_DAT5_COLORMODE_I256:
                return ((uint32_t)(entry->width + 7) >> 3) * entry->height * entry->bitDepth;
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
                printf("%03d: Image %3dx%-3d %s %s at X:%-4d Y:%-4d %5d bytes",
                    n, entry->width, entry->height,
                    (entry->flags & DMG_FLAG_BUFFERED)   ? "buffer ":"       ",
                    (entry->flags & DMG_FLAG_FIXED)      ? "fixed":"float",
                    entry->x, entry->y, entry->length);
                PrintCompressionGain(entry);
                printf("\n");
                if (verbose)
                {
                    printf("     File offset: %08X\n", entry->fileOffset);
                    printf("     Color range:  %d-%d\n", entry->firstColor, entry->lastColor);
                    printf("     Bit depth:    %d\n", entry->bitDepth);
                    printf("     Palette size: %d\n", DMG_GetEntryPaletteSize(dmg, n));
                    printf("     Palette:      ");
                    for (i = 0; i < DMG_GetEntryPaletteSize(dmg, n); i++)
                    {
                        uint32_t c = DMG_GetEntryStoredPalette(dmg, n)[i];
                        printf("%03X ", ((c >> 4) & 0xF) | ((c >> 8) & 0xF0) | ((c >> 12) & 0xF00));
                    }
                    printf("\n");
                    if (dmg->version != DMG_Version5)
                    {
                        printf("     EGA Palette:  ");
                        for (i = 0; i < 16; i++)
                            printf(" %02d ", entry->EGAPalette[i]);
                        printf("\n");
                        printf("     CGA Palette:  ");
                        for (i = 0; i < 4; i++)
                            printf(" %02d ", entry->CGAPalette[i]);
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

static bool LoadIndexedImageFile(const char* fileName, uint8_t* pixels, uint32_t pixelsBufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize)
{
    const char* dot = strrchr(fileName, '.');
    if (dot == 0)
        return false;

    if (stricmp(dot + 1, "png") == 0)
        return LoadPNGIndexed(fileName, pixels, pixelsBufferSize, width, height, palette, paletteSize, 0);

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

static bool IsImageToken(const char* token)
{
    const char* dot = strrchr(token, '.');
    return dot != 0 && IsImageFileExtension(dot + 1);
}

static bool IsTargetedFileToken(const char* token)
{
    const char* colon = strchr(token, ':');
    return colon != 0 && colon != token && isdigit(*token) && IsImageToken(colon + 1);
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

static bool IsCompressionToken(const char* token)
{
    return strnicmp(token, "compression:", 12) == 0;
}

static bool IsPriorityToken(const char* token)
{
    return strnicmp(token, "priority:", 9) == 0;
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

static int CompareStringsIgnoreCase(const void* a, const void* b)
{
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return stricmp(sa, sb);
}

static bool ExpandWildcardToken(const char* token, const char** outTokens, int* outCount)
{
    if (!HasWildcardChars(token))
        return false;
    if (IsSelectionToken(token) || IsPropertyToken(token) || IsRemapToken(token) || IsCompressionToken(token) || IsPriorityToken(token) || IsCreateToken(token))
        return false;

    const char* pattern = token;
    if (IsTargetedFileToken(token))
        return false;
    const char* colon = strchr(token, ':');
    if (colon != 0 && colon != token && isdigit((unsigned char)*token))
        return false;

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
                fprintf(stderr, "Error: Out of memory expanding wildcard \"%s\"\n", token);
                exit(1);
            }
            matchCount++;
            if (matchCount >= CLI_MAX_ARGUMENTS)
                break;
        } while (File_FindNext(&results));
    }

    if (matchCount == 0)
        return false;

    qsort(matches, matchCount, sizeof(matches[0]), CompareStringsIgnoreCase);
    for (int i = 0; i < matchCount; i++)
        outTokens[(*outCount)++] = matches[i];
    return true;
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

        if (IsTargetedFileToken(token) || IsImageToken(token))
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

    for (int i = 0; i < priorityEntryCount; i++)
    {
        uint8_t n = priorityEntries[i];
        DMG_Entry* entry = DMG_GetEntry(dmg, n);
        if (entry == 0 || entry->type == DMGEntry_Empty || added[n])
            continue;
        order[count++] = n;
        added[n] = true;
    }

    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
    {
        DMG_Entry* entry = DMG_GetEntry(dmg, (uint8_t)n);
        if (entry == 0 || entry->type == DMGEntry_Empty || added[n] || (entry->flags & DMG_FLAG_BUFFERED) == 0)
            continue;
        order[count++] = (uint8_t)n;
        added[n] = true;
    }

    for (int n = dmg->firstEntry; n <= dmg->lastEntry; n++)
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
        if (currentSelection[i])
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

    uint16_t width = 0, height = 0;
    uint32_t palette[256];
    int paletteSize = 0;
    int firstColor = 0;
    int lastColor = 0;
    if (!LoadIndexedImageFile(path, buffer, bufferSize, &width, &height, &palette[0], &paletteSize))
    {
        fprintf(stderr, "Error: Unable to load image \"%s\": %s\n", path, DMG_GetErrorString());
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
            if (!LoadIndexedImageFile(path, buffer, bufferSize, &width, &height, &palette[0], &paletteSize))
            {
                fprintf(stderr, "Error: Unable to reload image \"%s\": %s\n", path, DMG_GetErrorString());
                return false;
            }
            size = width * height;
        }
    }
    if (remapMode != REMAP_NONE && dmg->version == DMG_Version5 && paletteSize > 0)
        ApplyPaletteRemap(buffer, size, palette, &paletteSize, GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode), &firstColor, &lastColor);

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
        if (!EncodeDAT5Image((DMG_DAT5ColorMode)dmg->colorMode, buffer, width, height, outBuffer, &encodedSize))
        {
            fprintf(stderr, "Error: Unable to encode image \"%s\" for DAT5 mode\n", path);
            return false;
        }
        if (compressionEnabled)
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
        if (!DMG_SetEntryPaletteRange(dmg, targetIndex, palette, paletteSize, (uint8_t)firstColor, (uint8_t)lastColor))
        {
            fprintf(stderr, "Error: Unable to set image palette: %s\n", DMG_GetErrorString());
            if (compressed)
                OSFree(storedBuffer);
            return false;
        }
        if (!DMG_SetImageDataEx(dmg, targetIndex, storedBuffer, width, height, storedSize, compressed, GetBitDepthForMode((DMG_DAT5ColorMode)dmg->colorMode)))
        {
            fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
            if (compressed)
                OSFree(storedBuffer);
            return false;
        }
        printf("%03d: Added image %s (%u bytes%s)\n", targetIndex, path, storedSize, compressed ? ", ZX0" : "");
        if (compressed)
            OSFree(storedBuffer);
    }
    else
    {
        uint8_t maxIndex = GetMaxPixelIndex(buffer, size);
        if (paletteSize > 16 || maxIndex > 15)
        {
            fprintf(stderr, "Error: Legacy DAT formats only support up to 16 colors. Use DAT5 mode:i16/mode:i32/mode:i256.\n");
            return false;
        }
        uint8_t* storedBuffer = buffer;
        uint16_t compressedSize = (uint16_t)size;
        bool compressed = false;
        if (compressionEnabled)
        {
            uint8_t* outBuffer = buffer + size;
            if (!CompressImage(buffer, size, outBuffer, LOAD_BUFFER_SIZE - size, &compressed, &compressedSize, debug))
            {
                fprintf(stderr, "Error: Unable to compress image \"%s\": %s\n", path, DMG_GetErrorString());
                return false;
            }
            storedBuffer = outBuffer;
        }
        if (!DMG_SetEntryPalette(dmg, targetIndex, palette))
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

    SelectSingleEntry(currentSelection, targetIndex);
    *explicitSelection = false;
    *currentIndex = targetIndex;
    *currentFileSaved = true;
    return true;
}

static bool ParseEntryChanges(DMG* dmg, int tokenCount, const char* tokens[])
{
    int currentIndex = -1;
    bool currentFileSaved = false;
    bool currentSelection[256];
    bool explicitSelection = false;
    ClearSelection(currentSelection);

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

        if (IsCreateToken(token))
            continue;

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
            continue;
        }

        if (IsPropertyToken(token))
        {
            if (!ApplyPropertyToSelection(dmg, currentSelection, token))
                return false;
            continue;
        }

        if (IsTargetedFileToken(token) || IsImageToken(token))
        {
            if (!ApplyImageToken(dmg, token, currentSelection, &explicitSelection, &currentIndex, &currentFileSaved))
                return false;
            continue;
        }

        fprintf(stderr, "Error: Invalid argument: \"%s\"\n", token);
        return false;
    }

    return true;
}

bool RebuildDAT(DMG* dmg, const char* outputFileName)
{
	int n, i;
    uint8_t rebuildOrder[256];
    uint8_t firstEntry = dmg->firstEntry;
    uint8_t lastEntry = dmg->lastEntry;
    if (dmg->version == DMG_Version5)
    {
        if (!GetUsedDAT5Range(dmg, &firstEntry, &lastEntry))
        {
            firstEntry = dmg->firstEntry;
            lastEntry = dmg->firstEntry;
        }
    }
	DMG* out = dmg->version == DMG_Version5 ?
        DMG_CreateDAT5(outputFileName, (DMG_DAT5ColorMode)dmg->colorMode, dmg->targetWidth, dmg->targetHeight, firstEntry, lastEntry) :
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

		memcpy (outEntry->RGB32Palette, entry->RGB32Palette, sizeof(entry->RGB32Palette));
		memcpy (outEntry->CGAPalette, entry->CGAPalette, sizeof(entry->CGAPalette));
		memcpy (outEntry->EGAPalette, entry->EGAPalette, sizeof(entry->EGAPalette));
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
            uint32_t requiredBuffer = size + 64;

            if (dmg->version == DMG_Version5)
                requiredBuffer = size + GetDAT5EncodedSize((DMG_DAT5ColorMode)dmg->colorMode, width, height);

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
            if (dmg->version == DMG_Version5)
            {
                uint32_t encodedSize = 0;
                uint32_t storedSize = 0;
                uint8_t* storedBuffer = 0;
                uint32_t paletteBuffer[256];
                uint32_t* palettePtr = DMG_GetEntryStoredPalette(dmg, n);
                int paletteSize = entry->paletteColors;
                int firstColor = entry->firstColor;
                int lastColor = entry->lastColor;
                if (paletteSize > 0)
                {
                    memcpy(paletteBuffer, palettePtr, paletteSize * sizeof(uint32_t));
                    if (remapMode != REMAP_NONE)
                        ApplyPaletteRemap(inPtr, size, paletteBuffer, &paletteSize, GetPaletteLimit((DMG_DAT5ColorMode)dmg->colorMode), &firstColor, &lastColor);
                    if (!DMG_SetEntryPaletteRange(out, n, paletteBuffer, paletteSize, (uint8_t)firstColor, (uint8_t)lastColor))
                    {
                        fprintf(stderr, "%03d: Error: Unable to copy palette: %s\n", n, DMG_GetErrorString());
                        DMG_Close(out);
                        return false;
                    }
                }
                if (!EncodeDAT5Image((DMG_DAT5ColorMode)dmg->colorMode, inPtr, width, height, outPtr, &encodedSize))
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
                if (!DMG_SetImageDataEx(out, n, storedBuffer, width, height, storedSize, compressed, entry->bitDepth))
                {
                    fprintf(stderr, "Error: Unable to set DAT5 image data: %s\n", DMG_GetErrorString());
                    if (compressed)
                        OSFree(storedBuffer);
                    DMG_Close(out);
                    return false;
                }
                printf("%03d: Added image (%5u bytes%s)\n", n, storedSize, compressed ? ", ZX0" : "");
                if (compressed)
                    OSFree(storedBuffer);
                continue;
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
                storedBuffer = outPtr;
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

static bool UpdateTokenRequiresRebuild(const char* token)
{
    if (token == 0 || *token == 0)
        return false;
    if (IsPriorityToken(token))
        return true;
    if (IsRemapToken(token))
        return true;
    if (IsCreateToken(token))
        return true;
    if (IsTargetedFileToken(token) || IsImageToken(token))
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

int main (int argc, char *argv[])
{
	DMG* dmg = NULL;
	const char* outputFileName;
    const char* workingFileName = 0;
	int n;
    int exitCode = 0;
    char parseError[256];
    CLI_CommandLine commandLine;
    static const CLI_ActionSpec actionSpecs[] =
    {
        { "l", ACTION_LIST },
        { "v", ACTION_LIST },
        { "x", ACTION_EXTRACT },
        { "p", ACTION_EXTRACT_PALETTES },
        { "t", ACTION_TEST },
        { "a", ACTION_ADD },
        { "m", ACTION_ADD },
        { "e", ACTION_ADD },
        { "d", ACTION_DELETE },
        { "c", ACTION_NEW },
        { "n", ACTION_NEW },
        { "o", ACTION_UPDATE },
        { "u", ACTION_UPDATE },
        { "h", ACTION_HELP },
        { 0, 0 }
    };
    static const CLI_OptionSpec optionSpecs[] =
    {
        { 'v', DMG_OPTION_VERBOSE, CLI_OPTION_NONE },
        { 'n', DMG_OPTION_SORT_BY_ID, CLI_OPTION_NONE },
        { 0, 0, CLI_OPTION_NONE }
    };

	if (argc < 2)
	{
		PrintHelp();
		return 0;
	}

    if (!CLI_ParseCommandLine(argc, argv, actionSpecs, ACTION_LIST, optionSpecs, &commandLine, parseError, sizeof(parseError)))
    {
        fprintf(stderr, "Error: %s\n", parseError);
        return 1;
    }

    action = (Action)commandLine.action;
    verbose = commandLine.actionName != 0 && stricmp(commandLine.actionName, "v") == 0;
    debug = CLI_HasOption(&commandLine, DMG_OPTION_VERBOSE);
    listSortById = CLI_HasOption(&commandLine, DMG_OPTION_SORT_BY_ID);
    readOnly = action != ACTION_ADD && action != ACTION_DELETE && action != ACTION_NEW && action != ACTION_UPDATE;

    if (action == ACTION_HELP)
    {
        PrintHelp();
        return 0;
    }

    if (commandLine.argumentCount < 1)
    {
        fprintf(stderr, "Error: Missing filename\n");
        return 1;
    }

	DMG_SetWarningHandler(ShowWarning);
    remapMode = REMAP_NONE;
    remapRangeFirst = 0;
    remapRangeLast = 0;
    compressionEnabled = true;
    ResetPriorityEntries();

	if (strlen(commandLine.arguments[0]) > 1000)
	{
		fprintf(stderr, "Error: Invalid filename: \"%s\"\n", commandLine.arguments[0]);
		return 1;
	}
	strcpy(filename, commandLine.arguments[0]);

    const char** remainingArgs = commandLine.arguments + 1;
    int remainingArgCount = commandLine.argumentCount - 1;
    const char* expandedArgs[CLI_MAX_ARGUMENTS * 4];
    bool expandedArgOwned[CLI_MAX_ARGUMENTS * 4];
    int expandedArgCount = 0;
    const char* reorderedCreateArgs[CLI_MAX_ARGUMENTS];
    bool createPriorityHandledDirectly = false;

    for (int i = 0; i < remainingArgCount; i++)
    {
        int before = expandedArgCount;
        if (ExpandWildcardToken(remainingArgs[i], expandedArgs, &expandedArgCount))
        {
            for (int n = before; n < expandedArgCount; n++)
                expandedArgOwned[n] = true;
            continue;
        }
        expandedArgs[expandedArgCount] = remainingArgs[i];
        expandedArgOwned[expandedArgCount] = false;
        expandedArgCount++;
    }
    remainingArgs = expandedArgs;
    remainingArgCount = expandedArgCount;

    if (action == ACTION_NEW && !ParseCreateArguments(remainingArgCount, remainingArgs))
        return 1;

    if (action == ACTION_NEW && priorityEntryCount > 0)
    {
        createPriorityHandledDirectly = BuildDirectCreatePriorityArguments(remainingArgCount, remainingArgs, reorderedCreateArgs);
        if (createPriorityHandledDirectly)
            remainingArgs = reorderedCreateArgs;
    }

	if (action == ACTION_NEW)
	{
        workingFileName = ChangeExtension(filename, ".wrk");
        if (createDAT5)
        {
            uint8_t firstEntry = 0;
            uint8_t lastEntry = 255;
            if (createDAT5Mode == DMG_DAT5_COLORMODE_NONE)
            {
                fprintf(stderr, "Error: DAT5 creation requires mode:<cga|ega|i16|i32|i256>\n");
                return 1;
            }
            InferDAT5CreateRange(remainingArgCount, remainingArgs, &firstEntry, &lastEntry);
            dmg = DMG_CreateDAT5(workingFileName, createDAT5Mode, createDAT5Width, createDAT5Height, firstEntry, lastEntry);
        }
        else
		    dmg = DMG_Create(workingFileName);
		if (dmg == NULL)
		{
			fprintf(stderr, "Error: Failed to create \"%s\": %s\n", workingFileName, DMG_GetErrorString());
			return 1;
		}
		printf("Created new DAT file \"%s\"\n", filename);
	}
	else
	{
		dmg = DMG_Open(filename, readOnly);
		if (dmg == NULL)
		{
			fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
			return 1;
		}
	}

	switch (action)
	{
		case ACTION_HELP:
			break;
		case ACTION_LIST:
			if (ParseEntrySelectionList(remainingArgCount, remainingArgs))
				ListSelectedEntries(dmg, verbose);
			break;
		case ACTION_DELETE:
			if (ParseEntrySelectionList(remainingArgCount, remainingArgs))
				DeleteSelectedEntries(dmg);
			break;
		case ACTION_EXTRACT:
		case ACTION_EXTRACT_PALETTES:
		case ACTION_TEST:
			if (ParseEntrySelectionList(remainingArgCount, remainingArgs))
				ExtractSelectedEntries(dmg, action != ACTION_TEST, action == ACTION_EXTRACT_PALETTES);
			break;
		case ACTION_ADD:
		case ACTION_NEW:
			if (!ParseEntryChanges(dmg, remainingArgCount, remainingArgs))
            {
                if (action == ACTION_NEW && dmg != NULL)
                {
                    DMG_Close(dmg);
                    dmg = NULL;
                    if (workingFileName != 0)
                        remove(workingFileName);
                }
                exitCode = 1;
            }
            else if (action == ACTION_NEW && !createPriorityHandledDirectly && (priorityEntryCount > 0 || HasBufferedEntries(dmg)))
            {
                remove(filename);
                if (!RebuildDAT(dmg, filename))
                {
                    exitCode = 1;
                    break;
                }
                DMG_Close(dmg);
                dmg = NULL;
                if (workingFileName != 0)
                    remove(workingFileName);
            }
            else if (action == ACTION_NEW)
            {
                DMG_Close(dmg);
                dmg = NULL;
                remove(filename);
                if (rename(workingFileName, filename) != 0)
                {
                    fprintf(stderr, "Error: Failed to rename \"%s\" to \"%s\"\n", workingFileName, filename);
                    exitCode = 1;
                    break;
                }
            }
			break;
		case ACTION_UPDATE:
        {
            const char* editTokens[CLI_MAX_ARGUMENTS];
            int editTokenCount = 0;
            bool hasExplicitOutputFile = false;
            bool replaceOriginal = false;
            ParseUpdateArguments(remainingArgCount, remainingArgs, &outputFileName, &hasExplicitOutputFile, &editTokenCount, editTokens);
            if (editTokenCount > 0 && !ParseEntryChanges(dmg, editTokenCount, editTokens))
            {
                exitCode = 1;
                break;
            }
            if (!UpdateRequiresRebuild(dmg, outputFileName, editTokenCount, editTokens))
            {
                if (editTokenCount > 0)
                    printf("%s updated in place.\n", filename);
                else
                    printf("%s already up to date.\n", filename);
                break;
            }
			if (!hasExplicitOutputFile)
            {
				outputFileName = ChangeExtension(filename, ".new");
                replaceOriginal = true;
            }
			if (RebuildDAT(dmg, outputFileName))
			{
                if (replaceOriginal)
                {
                    DMG_Close(dmg);
                    dmg = NULL;
                    remove(filename);
                    if (rename(outputFileName, filename) != 0)
                    {
                        fprintf(stderr, "Error: Failed to replace \"%s\" with rebuilt file \"%s\"\n", filename, outputFileName);
                        exitCode = 1;
                        break;
                    }
				    printf("%s updated.\n", filename);
                }
                else
                {
				    printf("%s written.\n", outputFileName);
                }
			}
            else
            {
                exitCode = 1;
            }

			break;
        }
	}
	if (dmg != NULL)
		DMG_Close(dmg);

    for (int i = 0; i < expandedArgCount; i++)
    {
        if (expandedArgOwned[i])
            free((void*)expandedArgs[i]);
    }

	return exitCode;
}
