#ifdef _AMIGA

#include "gcc8_c_support.h"

#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <ddb_data.h>
#include <os_bito.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_file.h>
#include <dmg.h>

#include "keyboard.h"
#include "timer.h"
#include "textdraw.h"
#include "video.h"
#include "audio.h"

#include <exec/execbase.h>
#include <exec/ports.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define INLINE __attribute__((always_inline)) inline 

#ifndef DEBUG_MEMORY
#define DEBUG_MEMORY 0
#endif

#define R_VPOSR ( *(volatile uint32_t*)0xDFF004 )

extern volatile Custom *custom;
extern LONG os_blockCount;
extern LONG os_totalAllocated;

bool	 supportsOpenFileDialog = false;

bool     isPAL = false;
static bool isAGA = false;
int      cursorX;
int      cursorY;
bool     quit = false;
bool     exitGame = false;
bool     displaySwap = true;
bool     charToBack = false;
bool     drawToBack = false;
bool     initialized = false;

uint8_t*    plane[MAX_PLANES];
uint8_t*	frontBuffer = 0;
uint8_t*	backBuffer = 0;
static uint8_t* scratchBuffer = 0;
uint8_t*    frontPlane[MAX_PLANES];
uint8_t*    backPlane[MAX_PLANES];
static uint8_t* scratchPlane[MAX_PLANES];
static uint8_t  displayPlanes = TEXT_PLANES;
static uint8_t  requestedDisplayPlanes = TEXT_PLANES;
static uint32_t screenBytesPerPlane = SCR_BPNEXTB;
static uint32_t screenAllocate = SCR_BPNEXTB * TEXT_PLANES;
static uint32_t activePalette[256];
static uint16_t activePaletteAGAHigh[256];
static uint16_t activePaletteAGALow[256];
static uint32_t savedPalette[256];
static uint16_t savedPaletteColors = 16;

static DMG*       pictureOrigin;
static DMG_Entry* pictureEntry = 0;
static int        pictureIndex = 0;
static uint16_t*  pictureData = 0;
static uint32_t   pictureStride;
static uint32_t   picturePlaneStride;
static uint8_t    picturePlanes = TEXT_PLANES;

uint16_t  (*charsetWords)[256][8] = 0;
static uint16_t* rotationTable = 0;
uint16_t* copper1;
static uint16_t* copperLists[2] = { 0, 0 };
static uint8_t activeCopperList = 0;
static uint32_t copperListBytes = 1024;

INLINE uint16_t* SetVisiblePlanes(uint8_t** planes, uint16_t* copListEnd);
INLINE void RunCopperProgram(uint16_t* program, uint16_t* end);
INLINE uint16_t* SetScreenLayout(uint16_t* copListEnd);
INLINE uint16_t* SetBitPlanes(uint16_t* copPtr);
static inline uint16_t* BeginCopperBuild();
static void UpdateDrawPlanes();
static void SetPlanePointers(uint8_t* buffer, uint8_t** out);
static void ProgramDisplay();
static void VID_CommitPalette(bool waitForVBlank);
static void VID_StagePaletteColor(uint8_t color, uint8_t r, uint8_t g, uint8_t b);
static uint16_t* AppendCopperPalette(uint16_t* copListEnd);

static bool IsVisiblePlaneTarget()
{
	return plane[0] == (displaySwap ? backBuffer : frontBuffer);
}

static uint8_t** GetVisiblePlanes()
{
	return displaySwap ? backPlane : frontPlane;
}

static uint8_t* GetVisibleBuffer()
{
	return displaySwap ? backBuffer : frontBuffer;
}

static void CopyScreenBuffer(uint8_t** src, uint8_t** dst)
{
	for (uint8_t p = 0; p < displayPlanes; p++)
		BlitterCopy(src[p], 0, 0, dst[p], 0, 0, screenWidth, screenHeight, true);
	WaitBlit();
}

static void PresentScratchBuffer()
{
	if (isAGA && displayPlanes == 8)
	{
		VID_VSync();
		VID_CommitPalette(false);
		for (uint16_t i = 0; i < MAX_PLANES; i++)
			custom->bplpt[i] = (APTR)scratchPlane[i];

		uint16_t* program = copperLists[activeCopperList];
		uint16_t* end = program;
		end = SetScreenLayout(end);
		end = SetBitPlanes(end);
		end = SetVisiblePlanes(scratchPlane, end);
		*end++ = 0xffff;
		*end++ = 0xfffe;
		copper1 = program;
	}
	else
	{
		uint16_t* program = BeginCopperBuild();
		uint16_t* end = SetVisiblePlanes(scratchPlane, program);
		VID_VSync();
		end = AppendCopperPalette(end);
		RunCopperProgram(program, end);
	}

	if (displaySwap)
	{
		uint8_t* oldVisible = backBuffer;
		backBuffer = scratchBuffer;
		scratchBuffer = oldVisible;
		SetPlanePointers(backBuffer, backPlane);
	}
	else
	{
		uint8_t* oldVisible = frontBuffer;
		frontBuffer = scratchBuffer;
		scratchBuffer = oldVisible;
		SetPlanePointers(frontBuffer, frontPlane);
	}
	SetPlanePointers(scratchBuffer, scratchPlane);
	if (dmg != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchBuffer, screenAllocate, false);
	UpdateDrawPlanes();
}

#if DEBUG_MEMORY
static void DrawMemoryHUD()
{
	static uint32_t lastUpdate = 0;
	uint32_t now = GetMilliseconds();
	if (now - lastUpdate < 250)
		return;
	lastUpdate = now;

	char line[64];
	char num[16];
	char* ptr = line;
	char* end = line + sizeof(line) - 1;

	*ptr++ = 'M';
	*ptr++ = ' ';
	LongToChar((long)(AvailMem(MEMF_CHIP) / 1024), num, 10);
	for (int i = 0; num[i] != 0 && ptr < end; i++) *ptr++ = num[i];
	if (ptr < end) *ptr++ = 'K';
	if (ptr < end) *ptr++ = ' ';
	if (ptr < end) *ptr++ = 'A';
	if (ptr < end) *ptr++ = ' ';
	LongToChar((long)(os_totalAllocated / 1024), num, 10);
	for (int i = 0; num[i] != 0 && ptr < end; i++) *ptr++ = num[i];
	if (ptr < end) *ptr++ = 'K';
	if (ptr < end) *ptr++ = ' ';
	if (ptr < end) *ptr++ = 'B';
	if (ptr < end) *ptr++ = ' ';
	LongToChar((long)os_blockCount, num, 10);
	for (int i = 0; num[i] != 0 && ptr < end; i++) *ptr++ = num[i];
	*ptr = 0;

	VID_Clear(0, 0, 6 * 26, 8, 0);
	for (int n = 0; line[n] != 0; n++)
		VID_DrawCharacter(n * 6, 0, line[n], 15, 0);
}
#endif

uint16_t  copper2[] __attribute__((section (".MEMF_CHIP"))) = 
{
	offsetof(Custom, color[ 0]), 0x0000,
	offsetof(Custom, color[ 1]), 0x0005,
	offsetof(Custom, color[ 2]), 0x0050,
	offsetof(Custom, color[ 3]), 0x0055,
	offsetof(Custom, color[ 4]), 0x0500,
	offsetof(Custom, color[ 5]), 0x0505,
	offsetof(Custom, color[ 6]), 0x0520,
	offsetof(Custom, color[ 7]), 0x0555,
	offsetof(Custom, color[ 8]), 0x0222,
	offsetof(Custom, color[ 9]), 0x0227,
	offsetof(Custom, color[10]), 0x0272,
	offsetof(Custom, color[11]), 0x0277,
	offsetof(Custom, color[12]), 0x0722,
	offsetof(Custom, color[13]), 0x0727,
	offsetof(Custom, color[14]), 0x0772,
	offsetof(Custom, color[15]), 0x0777,
	offsetof(Custom, color[16]), 0x0000,
	offsetof(Custom, color[17]), 0x0000,
	offsetof(Custom, color[18]), 0x0000,
	offsetof(Custom, color[19]), 0x0000,
	offsetof(Custom, color[20]), 0x0000,
	offsetof(Custom, color[21]), 0x0000,
	offsetof(Custom, color[22]), 0x0000,
	offsetof(Custom, color[23]), 0x0000,
	offsetof(Custom, color[24]), 0x0000,
	offsetof(Custom, color[25]), 0x0000,
	offsetof(Custom, color[26]), 0x0000,
	offsetof(Custom, color[27]), 0x0000,
	offsetof(Custom, color[28]), 0x0000,
	offsetof(Custom, color[29]), 0x0000,
	offsetof(Custom, color[30]), 0x0000,
	offsetof(Custom, color[31]), 0x0000,
	0xffff, 0xfffe // end copper list
};

INLINE uint16_t* SetVisiblePlanes(uint8_t** planes, uint16_t* copListEnd) 
{
	for (uint16_t i = 0 ; i < MAX_PLANES ; i++) {
		uint32_t addr = (uint32_t)planes[i];
		*copListEnd++ = offsetof(Custom, bplpt[0]) + i*sizeof(APTR);
		*copListEnd++ = (uint16_t)(addr>>16);
		*copListEnd++ = offsetof(Custom, bplpt[0]) + i*sizeof(APTR) + 2;
		*copListEnd++ = (uint16_t)addr;

		plane[i] = (i < displayPlanes) ? planes[i] : 0;
	}
	return copListEnd;
}

static inline uint16_t EncodePlaneCount(uint8_t planes)
{
	return ((planes & 0x07) << 12) | ((planes & 0x08) ? (1 << 4) : 0);
}

static inline uint16_t EncodeColor12(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r & 0xF0) << 4) | (g & 0xF0) | (b >> 4));
}

static inline uint16_t EncodeColor24High(uint32_t rgb)
{
	return (uint16_t)((((rgb >> 20) & 0x0F) << 8) |
		(((rgb >> 12) & 0x0F) << 4) |
		((rgb >> 4) & 0x0F));
}

static inline uint16_t EncodeColor24Low(uint32_t rgb)
{
	return (uint16_t)((((rgb >> 16) & 0x0F) << 8) |
		(((rgb >> 8) & 0x0F) << 4) |
		(rgb & 0x0F));
}

static inline void SetEncodedAGAPalette(uint8_t color, uint32_t rgb)
{
	activePaletteAGAHigh[color] = EncodeColor24High(rgb);
	activePaletteAGALow[color] = EncodeColor24Low(rgb);
}

static void UpdateDrawPlanes()
{
	for (unsigned n = 0; n < MAX_PLANES; n++)
	{
		if (n < displayPlanes)
			plane[n] = drawToBack ? backPlane[n] : frontPlane[n];
		else
			plane[n] = 0;
	}
}

static void SetPlanePointers(uint8_t* buffer, uint8_t** out)
{
	for (unsigned n = 0; n < MAX_PLANES; n++)
		out[n] = (n < displayPlanes && buffer != 0) ? buffer + screenBytesPerPlane * n : 0;
}

static void UploadAGAPalette(uint16_t count)
{
	uint16_t colors = count > 256 ? 256 : count;
	uint16_t banks = (colors + 31) >> 5;
	const uint16_t* srcHigh = activePaletteAGAHigh;
	const uint16_t* srcLow = activePaletteAGALow;

	for (uint16_t bank = 0; bank < banks; bank++)
	{
		volatile uint16_t* hwColor = &custom->color[0];
		custom->bplcon3 = bank << 13;
		for (uint16_t color = 0; color < 32; color++)
			*hwColor++ = *srcHigh++;

		hwColor = &custom->color[0];
		custom->bplcon3 = 0x0200 | (bank << 13);
		for (uint16_t color = 0; color < 32; color++)
			*hwColor++ = *srcLow++;
	}

	custom->bplcon3 = 0;
}

INLINE void RunCopperProgram(uint16_t* program, uint16_t* end)
{
	*end++ = 0xffff;
	*end++ = 0xfffe;

	custom->cop1lc  = (uint32_t)program;
	custom->dmacon  = DMAF_BLITTER; // Disable blitter dma for copjmp bug
	custom->copjmp1 = 0x7fff;       // Start coppper
	custom->dmacon  = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
}

static inline uint16_t* BeginCopperBuild()
{
	uint8_t next = activeCopperList ^ 1;
	return copperLists[next];
}

static inline void CommitCopperBuild(uint16_t* program, uint16_t* end)
{
	VID_VSync();
	RunCopperProgram(program, end);
	copper1 = program;
	activeCopperList = (program == copperLists[1]) ? 1 : 0;
}

void VID_ActivateCharset()
{
	if (charsetWords == 0 || rotationTable == 0)
		return;

	for (int shift = 0; shift < 8; shift++)
	{
		uint16_t* table = rotationTable + (shift << 8);
		for (int value = 0; value < 256; value++)
			table[value] = (uint16_t)(((uint16_t)value << 8) >> shift);
	}

	uint8_t* chr = charset;
	for (int c = 0; c < 256; c++)
	{
		for (int y = 0; y < 8; y++)
			(*charsetWords)[c][y] = *chr++ << 8;
	}
}

static bool LoadCharset (uint8_t* ptr, const char* filename)
{
	File* file = File_Open(filename);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	if (File_GetSize(file) != 2176)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		File_Close(file);
		return false;
	}
	File_Seek(file, 128);
	File_Read(file, ptr, 2048);
	File_Close(file);
	VID_ActivateCharset();
	return true;
}

static uint32_t GetMaxPlanarImageSize(DMG* file)
{
	uint32_t maxSize = 0;
	if (file == 0)
		return 0;

	for (int n = file->firstEntry; n <= file->lastEntry; n++)
	{
		DMG_Entry* entry = file->entries[n];
		if (entry == 0 || entry->type != DMGEntry_Image)
			continue;

		uint32_t size = DMG_CalculateRequiredSize(entry, ImageMode_PlanarST);
		if (size > maxSize)
			maxSize = size;
	}
	return maxSize;
}

static uint32_t GetTotalPlanarImageSize(DMG* file)
{
	uint32_t totalSize = 0;
	if (file == 0)
		return 0;

	for (int n = file->firstEntry; n <= file->lastEntry; n++)
	{
		DMG_Entry* entry = file->entries[n];
		if (entry == 0 || entry->type != DMGEntry_Image)
			continue;

		totalSize += DMG_CalculateRequiredSize(entry, ImageMode_PlanarST);
	}
	return totalSize;
}

static uint32_t GetRecommendedDAT5ImageCacheSize(DMG* file)
{
	uint32_t maxSize = GetMaxPlanarImageSize(file);
	if (maxSize == 0)
		return 0;

	uint32_t itemOverhead = ((sizeof(DMG_Cache) + 31) & ~31) + 32;
	uint32_t recommended = maxSize + itemOverhead;

	// If half-height images are common, leave enough headroom for two of them.
	if (maxSize >= 2)
	{
		uint32_t halfSize = maxSize / 2;
		uint32_t dualHalf = 2 * (((halfSize + 31) & ~31) + sizeof(DMG_Cache));
		if (dualHalf > recommended)
			recommended = dualHalf;
	}

	// Round up to a friendly chunk so we don't fail just because of cache metadata.
	recommended = (recommended + 0x0FFF) & ~0x0FFF;
	return recommended;
}

static uint32_t GetDesiredImageCacheSize(DMG* file, uint32_t freeMemory, uint32_t minimumCache)
{
	uint32_t usefulCache = GetTotalPlanarImageSize(file);
	if (usefulCache < minimumCache)
		usefulCache = minimumCache;

	uint32_t reserveFree = (displayPlanes == 8) ? 0x20000 : 0x8000;
	if (freeMemory <= reserveFree)
		return minimumCache;

	uint32_t desired = freeMemory - reserveFree;
	if (desired > usefulCache)
		desired = usefulCache;
	if (desired < minimumCache)
		desired = minimumCache;

	desired = (desired + 0x0FFF) & ~0x0FFF;
	return desired;
}

static uint8_t GetRequiredDisplayPlanes(DMG* file)
{
	if (file == 0 || file->version != DMG_Version5)
		return TEXT_PLANES;

	switch (file->colorMode)
	{
		case DMG_DAT5_COLORMODE_CGA:
		case DMG_DAT5_COLORMODE_EGA:
		case DMG_DAT5_COLORMODE_I16:
			return TEXT_PLANES;
		case DMG_DAT5_COLORMODE_I32:
			return 5;
		case DMG_DAT5_COLORMODE_I256:
			return 8;
		default:
			return TEXT_PLANES;
	}
}

static bool ProbeDataFileHeader(const char* fileName, uint8_t* requiredPlanes, uint16_t* width, uint16_t* height, uint8_t* colorMode)
{
	File* file = File_Open(ChangeExtension(fileName, ".dat"), ReadOnly);
	if (file == 0)
		return false;

	uint8_t header[16];
	bool ok = File_Read(file, header, sizeof(header)) == sizeof(header);
	File_Close(file);
	if (!ok)
		return false;

	if (header[0] == 'D' && header[1] == 'A' && header[2] == 'T' && header[3] == 0 &&
		header[4] == 0 && header[5] == 5)
	{
		if (width) *width = read16BE(header + 0x06);
		if (height) *height = read16BE(header + 0x08);
		if (colorMode) *colorMode = header[0x0E];
		if (requiredPlanes)
		{
			switch (header[0x0E])
			{
				case DMG_DAT5_COLORMODE_I32: *requiredPlanes = 5; break;
				case DMG_DAT5_COLORMODE_I256: *requiredPlanes = 8; break;
				default: *requiredPlanes = 4; break;
			}
		}
		return true;
	}

	if (requiredPlanes) *requiredPlanes = 4;
	if (width) *width = 0;
	if (height) *height = 0;
	if (colorMode) *colorMode = 0;
	return true;
}

static bool ConfigureDisplayPlanes(uint8_t planes)
{
	if (planes < TEXT_PLANES || planes > MAX_PLANES)
		return false;
	if (displayPlanes == planes && frontBuffer != 0 && backBuffer != 0)
		return true;

	uint32_t bytesPerPlane = SCR_STRIDEB * SCR_HEIGHTPX;
	uint32_t allocate = bytesPerPlane * planes;
	uint8_t* newFront = (uint8_t*)AllocMem(allocate, MEMF_CHIP);
	uint8_t* newBack = (uint8_t*)AllocMem(allocate, MEMF_CHIP);
	uint8_t* newScratch = (uint8_t*)AllocMem(allocate, MEMF_CHIP);
	if (newFront == 0 || newBack == 0 || newScratch == 0)
	{
		if (newFront != 0) FreeMem(newFront, allocate);
		if (newBack != 0) FreeMem(newBack, allocate);
		if (newScratch != 0) FreeMem(newScratch, allocate);
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}

	MemClear(newFront, allocate);
	MemClear(newBack, allocate);
	MemClear(newScratch, allocate);

	uint8_t* oldFront = frontBuffer;
	uint8_t* oldBack = backBuffer;
	uint8_t* oldScratch = scratchBuffer;
	uint32_t oldAllocate = screenAllocate;

	displayPlanes = planes;
	screenBytesPerPlane = bytesPerPlane;
	screenAllocate = allocate;
	frontBuffer = newFront;
	backBuffer = newBack;
	scratchBuffer = newScratch;
	displaySwap = false;
	charToBack = false;
	drawToBack = false;

	SetPlanePointers(frontBuffer, frontPlane);
	SetPlanePointers(backBuffer, backPlane);
	SetPlanePointers(scratchBuffer, scratchPlane);
	if (dmg != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchBuffer, screenAllocate, false);
	UpdateDrawPlanes();
	ProgramDisplay();

	if (oldFront != 0)
		FreeMem(oldFront, oldAllocate);
	if (oldBack != 0)
		FreeMem(oldBack, oldAllocate);
	if (oldScratch != 0)
		FreeMem(oldScratch, oldAllocate);
	return true;
}

void VID_SetDisplayPlanesHint(uint8_t planes)
{
	if (planes < TEXT_PLANES || planes > MAX_PLANES)
		planes = TEXT_PLANES;
	requestedDisplayPlanes = planes;
}

void VID_SetColor(uint8_t color, uint16_t pal)
{
	if (color >= 32)
		return;

	uint8_t r = (pal >> 4) & 0xF0;
	uint8_t g = pal & 0xF0;
	uint8_t b = (pal & 0x0F) << 4;
	r |= r >> 4;
	g |= g >> 4;
	b |= b >> 4;
	activePalette[color] = (r << 16) | (g << 8) | b;
	SetEncodedAGAPalette(color, activePalette[color]);

	// char buf[16];
	// LongToChar(pal, buf, 16);
	// DebugPrintf("Setting color %ld to %s\n", (long)color, buf);

	custom->color[color] = pal;
	copper2[color*2+1] = pal;
}

uint16_t VID_GetColor(uint8_t color)
{
	if (color >= 32)
		return 0;
	return copper2[color*2+1];
}

static void VID_CommitPalette(bool waitForVBlank)
{
	if (waitForVBlank)
		VID_VSync();

	if (isAGA && displayPlanes == 8)
	{
		UploadAGAPalette(256);
	}
	else
	{
		for (uint16_t color = 0; color < 32; color++)
			custom->color[color] = copper2[color * 2 + 1];
	}
}

void VID_ActivatePalette()
{
	VID_CommitPalette(true);
}

static void VID_StagePaletteColor(uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	activePalette[color] = (r << 16) | (g << 8) | b;
	SetEncodedAGAPalette(color, activePalette[color]);
	if (!isAGA || displayPlanes != 8 || color < 32)
		copper2[color * 2 + 1] = EncodeColor12(r, g, b);
}

static uint16_t* AppendCopperPalette(uint16_t* copListEnd)
{
	for (uint16_t i = 0; i < 32 * 2; i += 2)
	{
		*copListEnd++ = copper2[i + 0];
		*copListEnd++ = copper2[i + 1];
	}
	return copListEnd;
}

static void VID_SetPaletteEntries(uint32_t* palette, uint16_t count, uint16_t firstColor = 0, bool clearOutside = true, bool immediate = true)
{
	uint16_t maxColors = (displayPlanes == 8 && isAGA) ? 256 : 32;
	if (firstColor >= maxColors)
		return;
	if (count > maxColors - firstColor)
		count = maxColors - firstColor;

	for (uint16_t n = 0; n < count; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >>  8) & 0xFF;
		uint8_t b = (palette[n]      ) & 0xFF;
		if (immediate)
			VID_SetPaletteColor(firstColor + n, r, g, b);
		else
			VID_StagePaletteColor(firstColor + n, r, g, b);
	}
	if (clearOutside)
	{
		for (uint16_t n = 0; n < firstColor; n++)
			if (immediate)
				VID_SetPaletteColor(n, 0, 0, 0);
			else
				VID_StagePaletteColor(n, 0, 0, 0);
		for (uint16_t n = firstColor + count; n < maxColors; n++)
			if (immediate)
				VID_SetPaletteColor(n, 0, 0, 0);
			else
				VID_StagePaletteColor(n, 0, 0, 0);
	}

#if DEBUG_PALETTE
	for (int color = 0; color < 16; color++)
	{
		char buf[8];
		uint16_t v = VID_GetColor(color);
		LongToChar(v, buf, 16);
		VID_DrawCharacter(6 + 40*(color & 3), 8 + 8*(color >> 2), ' ', 15, color);
		for (int n = 0; n < 4; n++)
			VID_DrawCharacter(14 + 40*(color & 3)+6*n, 8 + 8*(color >> 2), buf[n], 15, 0);
	}
#endif
}

void VID_SetPalette (uint32_t* palette)
{
	VID_SetPaletteEntries(palette, 16);
}

void VID_WaitForKey()
{
	DebugPrintf("Waiting for key\n");
	InputBufferHead = InputBufferTail = 0;
	while (InputBufferHead == InputBufferTail)
		;
	DebugPrintf("Key pressed\n");
}

bool VID_AnyKey ()
{
	return InputBufferHead != InputBufferTail;
}

bool VID_LoadDataFile (const char* fileName)
{
	uint32_t t0 = GetMilliseconds();
	pictureOrigin = 0;
	pictureEntry = 0;
	pictureIndex = 0;
	pictureData = 0;
	pictureStride = 0;
	picturePlaneStride = 0;
	picturePlanes = TEXT_PLANES;

	DebugPrintf("VID_LoadDataFile(%s)\n", fileName);

	if (dmg != 0)
	{
		DebugPrintf("Closing previous data file\n");
		DMG_Close(dmg);
		dmg = 0;
	}

	uint8_t probePlanes = TEXT_PLANES;
	uint16_t probeWidth = 0;
	uint16_t probeHeight = 0;
	uint8_t probeColorMode = 0;
	if (ProbeDataFileHeader(fileName, &probePlanes, &probeWidth, &probeHeight, &probeColorMode))
	{
		DebugPrintf("Header probe: mode=%u target=%ux%u planes=%u isAGA=%d activePlanes=%u\n",
			(unsigned)probeColorMode, (unsigned)probeWidth, (unsigned)probeHeight,
			(unsigned)probePlanes, isAGA ? 1 : 0, (unsigned)displayPlanes);

		if (probeColorMode != 0)
		{
			if (probeWidth != 320 || probeHeight != 200)
			{
				DebugPrintf("Rejecting DAT5 from header probe due to unsupported target size %ux%u\n",
					(unsigned)probeWidth, (unsigned)probeHeight);
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
			if (probePlanes == 8 && !isAGA)
			{
				DebugPrintf("Rejecting DAT5 I256 from header probe on non-AGA machine\n");
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
			if (probePlanes != displayPlanes)
			{
				DebugPrintf("Rejecting DAT5 from header probe due to plane mismatch (required=%u active=%u)\n",
					(unsigned)probePlanes, (unsigned)displayPlanes);
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
		}
	}

	DebugPrintf("Trying %s\n", ChangeExtension(fileName, ".dat"));
	uint32_t tOpen = GetMilliseconds();
	dmg = DMG_Open(ChangeExtension(fileName, ".dat"), true);
	if (dmg == 0)
	{
		DebugPrintf("Trying %s\n", ChangeExtension(fileName, ".ega"));
		dmg = DMG_Open(ChangeExtension(fileName, ".ega"), true);
	}
	if (dmg == 0)
	{
		DebugPrintf("Trying %s\n", ChangeExtension(fileName, ".cga"));
		dmg = DMG_Open(ChangeExtension(fileName, ".cga"), true);
	}
	if (dmg == 0)
	{
		DebugPrintf("No data file found for %s\n", fileName);
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	DebugPrintf("DMG_Open completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tOpen));

	DebugPrintf("Opened data file: version=%d mode=%d target=%ux%u\n",
		(int)dmg->version, (int)dmg->colorMode, (unsigned)dmg->targetWidth, (unsigned)dmg->targetHeight);

	if (scratchBuffer != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchBuffer, screenAllocate, false);

	uint8_t requiredPlanes = GetRequiredDisplayPlanes(dmg);
	DebugPrintf("Display check: requiredPlanes=%u displayPlanes=%u isAGA=%d\n",
		(unsigned)requiredPlanes, (unsigned)displayPlanes, isAGA ? 1 : 0);
	if (dmg->version == DMG_Version5 && (dmg->targetWidth != 320 || dmg->targetHeight != 200))
	{
		DebugPrintf("Unsupported DAT5 target size %ux%u\n", (unsigned)dmg->targetWidth, (unsigned)dmg->targetHeight);
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	if (requiredPlanes == 8 && !isAGA)
	{
		DebugPrintf("Rejecting I256 DAT5 on non-AGA machine\n");
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	if (requiredPlanes != displayPlanes)
	{
		DebugPrintf("Rejecting data file due to plane mismatch (required=%u active=%u)\n",
			(unsigned)requiredPlanes, (unsigned)displayPlanes);
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	uint32_t requiredImageCache = 0;
	if (dmg->version == DMG_Version5)
	{
		requiredImageCache = GetRecommendedDAT5ImageCacheSize(dmg);
		DebugPrintf("DAT5 planar cache requirement: %u bytes\n", (unsigned)requiredImageCache);
	}

	if (!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
	}
	DebugPrintf("Charsets ready after %lu ms\n", (unsigned long)(GetMilliseconds() - t0));

	uint32_t datSize = File_GetSize(dmg->file);
	uint32_t freeMemory = AvailMem(0);
	DebugPrintf("Free memory: %u bytes (datSize: %u)\n", freeMemory, datSize);
	uint32_t desiredImageCache = GetDesiredImageCacheSize(dmg, freeMemory, requiredImageCache);
	DebugPrintf("Desired image cache: %u bytes\n", (unsigned)desiredImageCache);

	if (freeMemory > datSize + 65536)			// Everything fits
	{
		uint32_t tCache = GetMilliseconds();
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		DebugPrintf("File cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		freeMemory = AvailMem(0);
		if (freeMemory >= 32768)
		{
			tCache = GetMilliseconds();
			uint32_t cacheBytes = GetDesiredImageCacheSize(dmg, freeMemory, requiredImageCache);
			DMG_SetupImageCache(dmg, cacheBytes);
			DebugPrintf("Image cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		}
	}
	else if (freeMemory > datSize + 32768)		// DAT barely fits
	{
		uint32_t tCache = GetMilliseconds();
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DebugPrintf("Image cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		tCache = GetMilliseconds();
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		DebugPrintf("File cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
	}
	else if (freeMemory > 0x40000)				// >256K free
	{
		uint32_t tCache = GetMilliseconds();
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DebugPrintf("Image cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		tCache = GetMilliseconds();
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		DebugPrintf("File cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
	}
	else if (freeMemory > 0x20000)				// >128K free
	{
		uint32_t tCache = GetMilliseconds();
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DebugPrintf("Image cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		tCache = GetMilliseconds();
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		DebugPrintf("File cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
	}
	else 
	{
		uint32_t tCache = GetMilliseconds();
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DebugPrintf("Image cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
		tCache = GetMilliseconds();
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		DebugPrintf("File cache setup completed in %lu ms\n", (unsigned long)(GetMilliseconds() - tCache));
	}

	if (requiredImageCache > 0 && dmg->cacheSize < requiredImageCache)
	{
		DebugPrintf("Image cache too small for DAT5 planar image (have=%u need=%u)\n",
			(unsigned)dmg->cacheSize, (unsigned)requiredImageCache);
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}

	DebugPrintf("Data file ready after %lu ms\n", (unsigned long)(GetMilliseconds() - t0));
	return true;
}

static inline bool AdjustCoordinates(int &x, int &y, int &w, int &h)
{
	if (x + w > screenWidth)
		w = screenWidth - x;
	if (y+h > screenHeight)
		h = screenHeight - y;
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	return !(w < 1 || h < 1);
}

void VID_Clear (int x, int y, int w, int h, uint8_t color)
{
	int originalX = x;
	int originalY = y;
	int originalW = w;
	int originalH = h;

	if (!AdjustCoordinates(x, y, w, h))
		return;
		
	if (y >= screenHeight || w < 1 || h < 1)
		return;
		
	if (w*h >= 8192)
		VID_VSync();

	uint8_t planesToClear = TEXT_PLANES;
	// Keep text/window clears on the low 4 planes so upper picture planes survive.
	// Only full-screen clears or colors that explicitly use upper bits clear every plane.
	if ((originalX <= 0 && originalY <= 0 && originalW >= screenWidth && originalH >= screenHeight) ||
		((color >> TEXT_PLANES) != 0))
		planesToClear = displayPlanes;

	for (uint8_t p = 0; p < planesToClear; p++)
		BlitterRect(plane[p], x, y, w, h, (color & (1 << p)) != 0);
}

void VID_ClearAllPlanes(int x, int y, int w, int h, uint8_t color)
{
	if (!AdjustCoordinates(x, y, w, h))
		return;

	if (y >= screenHeight || w < 1 || h < 1)
		return;

	if (w * h >= 8192)
		VID_VSync();

	for (uint8_t p = 0; p < displayPlanes; p++)
		BlitterRect(plane[p], x, y, w, h, (color & (1 << p)) != 0);
}

void VID_ClearBuffer (bool front)
{
	uint8_t** p = front ^ displaySwap ? frontPlane : backPlane;
	for (uint8_t n = 0; n < displayPlanes; n++)
		BlitterRect(p[n], 0, 0, screenWidth, screenHeight, 0);
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode screenMode)
{
	if (pictureEntry == 0)
		return;
    uint32_t t0 = GetMilliseconds();

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0)
		return;

	if (h > pictureEntry->height)
		h = pictureEntry->height;
	if (w > pictureEntry->width)
		w = pictureEntry->width;
    uint16_t copyPixelWidth = (uint16_t)w;
    uint16_t copyPixelHeight = (uint16_t)h;

	uint32_t* palette = DMG_GetEntryPalette(dmg, pictureIndex, ImageMode_RGBA32);
	uint16_t paletteSize = DMG_GetEntryPaletteSize(dmg, pictureIndex);
    uint8_t paletteFirst = DMG_GetEntryFirstColor(dmg, pictureIndex);
    uint32_t tPalette = GetMilliseconds();
	bool presentingScratch = IsVisiblePlaneTarget();
	uint8_t** targetPlanes = presentingScratch ? scratchPlane : plane;
	switch (screenMode)
	{
		default:
		case ScreenMode_VGA16:
		case ScreenMode_VGA:
		case ScreenMode_HiRes:
		case ScreenMode_SHiRes:
			if (dmg->version == DMG_Version5 || ((pictureEntry->flags & DMG_FLAG_FIXED) && plane[0] == frontBuffer))
			{
				// TODO: This is a hack to fix the palette for V1
				if (dmg->version == DMG_Version1)
					pictureEntry->RGB32Palette[15] = 0xFFFFFFFF;
				VID_SetPaletteEntries(palette, paletteSize, paletteFirst, paletteFirst == 0, !presentingScratch);
				if (!presentingScratch)
					VID_CommitPalette(false);
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			VID_SetPaletteEntries(DMG_GetCGAMode(pictureEntry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan,
				4, 0, true, !presentingScratch);
			if (!presentingScratch)
				VID_CommitPalette(false);
			break;
	}
    DebugPrintf("VID_DisplayPicture(%u): palette phase %lu ms, planes=%u size=%ux%u at %d,%d\n",
        (unsigned)pictureIndex,
        (unsigned long)(GetMilliseconds() - tPalette),
        (unsigned)picturePlanes, (unsigned)w, (unsigned)h, x, y);
	
	uint16_t* srcPtr = pictureData;
	uint32_t off = y*SCR_STRIDEB + (x >> 3);
	bool skipLastByte = (w & 15) != 0;
    bool canUseBlitter =
        pictureData != 0 &&
        pictureStride == SCR_STRIDEW &&
        x >= 0 &&
        y >= 0 &&
        w > 0 &&
        h > 0;

	w = (w + 15)/16;
	uint32_t  pinc = SCR_STRIDEW - w;
	uint32_t a = h;
	if (picturePlanes > displayPlanes)
		return;

    if (canUseBlitter)
    {
        if (presentingScratch)
            CopyScreenBuffer(GetVisiblePlanes(), scratchPlane);
        for (uint8_t i = 0; i < picturePlanes; i++)
        {
            uint8_t* srcPlane = (uint8_t*)pictureData + i * picturePlaneStride * 2;
            BlitterCopy(srcPlane, 0, 0, targetPlanes[i], (uint16_t)x, (uint16_t)y, copyPixelWidth, copyPixelHeight, true);
        }
        WaitBlit();
        if (presentingScratch)
            PresentScratchBuffer();
        DebugPrintf("VID_DisplayPicture(%u): blit phase %lu ms, total %lu ms\n",
            (unsigned)pictureIndex,
            (unsigned long)(GetMilliseconds() - tPalette),
            (unsigned long)(GetMilliseconds() - t0));
        return;
    }

	if (off & 1)
	{
		uint8_t* p[MAX_PLANES];
		for (uint8_t i = 0; i < picturePlanes; i++)
			p[i] = targetPlanes[i] + off;

		pinc <<= 1;
		a--;
		pinc += 2;

		uint8_t* srcBase = (uint8_t*)srcPtr;

		// Unaligned writes
		do
		{
			for (int32_t b = 0; b < w - 1; b++)
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
				{
					uint8_t* s = srcBase + i * picturePlaneStride * 2 + b * 2;
					p[i][0] = *s++;
					p[i][1] = *s++;
					p[i] += 2;
				}
			}
			if (skipLastByte)
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
				{
					uint8_t* s = srcBase + i * picturePlaneStride * 2 + (w - 1) * 2;
					p[i][0] = *s;
					p[i] += pinc;
				}
			}
			else
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
				{
					uint8_t* s = srcBase + i * picturePlaneStride * 2 + (w - 1) * 2;
					p[i][0] = *s++;
					p[i][1] = *s++;
					p[i] += pinc;
				}
			}

			srcBase += pictureStride * 2;
		}
		while (a--);
	}
	else
	{
		uint16_t* p[MAX_PLANES];
		for (uint8_t i = 0; i < picturePlanes; i++)
			p[i] = (uint16_t*)(targetPlanes[i] + off);

		a--;
		pinc++;

		do
		{
			for (int32_t b = 0; b < w - 1; b++)
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
					*p[i]++ = srcPtr[i * picturePlaneStride + b];
			}
			if (skipLastByte)
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
					*p[i] = srcPtr[i * picturePlaneStride + (w - 1)] | (*p[i] & 0x00FF);
			}
			else
			{
				for (uint8_t i = 0; i < picturePlanes; i++)
					*p[i] = srcPtr[i * picturePlaneStride + (w - 1)];
			}

			for (uint8_t i = 0; i < picturePlanes; i++)
				p[i] += pinc;
			srcPtr  += pictureStride;
		}
		while (a--);
	}

	if (presentingScratch)
		PresentScratchBuffer();

    DebugPrintf("VID_DisplayPicture(%u): blit phase %lu ms, total %lu ms\n",
        (unsigned)pictureIndex,
        (unsigned long)(GetMilliseconds() - tPalette),
        (unsigned long)(GetMilliseconds() - t0));
}

static void VID_DrawCharacterNewFF (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	uint8_t width = charWidth[ch];
	if (width == 0)
		return;
	if (width > 8)
		width = 8;

	if (rotationTable == 0 || charsetWords == 0)
		return;

	uint8_t* dst = charToBack ^ displaySwap ? backBuffer : frontBuffer;
	uint8_t* out = dst + y * SCR_STRIDEB + (x >> 3);
	
	const uint16_t shift = (uint16_t)(x & 0x07);
	const uint16_t* rotation = rotationTable + (shift << 8);
	const uint8_t* glyph = charset + 8 * ch;
	const uint8_t widthMask8 = (uint8_t)(0xFF << (8 - width));
	const uint16_t cover = rotation[widthMask8];
	uint8_t maskedGlyph[8];
	ink &= 0x0F;

	for (int row = 0; row < 8; row++)
		maskedGlyph[row] = glyph[row] & widthMask8;

	if (paper == 255)
	{
		VID_DrawCharacterTransparentPatched(out, rotation, maskedGlyph, ink);
		return;
	}

	paper &= 0x0F;
	VID_DrawCharacterSolidPatched(out, rotation, maskedGlyph, cover, ink, paper);
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_DrawCharacterNewFF(x, y, ch, ink, paper);
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* modifiers)
{
	uint16_t v = 0;
	if (InputBufferHead != InputBufferTail)
	{
		v = InputBuffer[InputBufferHead];
		InputBufferHead = (InputBufferHead+1) & 63;
	}
	
	if (key) *key = v & 0xFF;
	if (ext) *ext = v >> 8;
	if (modifiers) *modifiers = GetModifiers();

#if DEBUG_ALLOCS
	if ((v >> 8) == 0x3C)	// F2
	{
		DumpMemory(AvailMem(MEMF_CHIP | MEMF_LARGEST));
		if (key) *key = 0;
		if (ext) *ext = 0;
	}
#endif

	if ((v >> 8) == 0x44 && interpreter)	// F10
	{
		if (++interpreter->keyClick == 3)
			interpreter->keyClick = 0;
	}
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = GetMilliseconds();
}

__attribute__((noinline))
void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	if (displayPlanes == 8)
	{
		uint32_t v = activePalette[color];
		if (r) *r = (v >> 16) & 0xFF;
		if (g) *g = (v >> 8) & 0xFF;
		if (b) *b = v & 0xFF;
		return;
	}

	uint16_t v = VID_GetColor(color);
	if (r) 
	{
		*r = (v >> 4) & 0xF0;
		*r |= *r >> 4;
	}
	if (g) 
	{
		*g = v & 0xF0;
		*g |= *g >> 4;
	}
	if (b) 
	{
		*b = v & 0x0F;
		*b |= *b << 4;
	}
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	if (pictureEntry == 0 || pictureOrigin != dmg)
	{
		if (fixed != 0)
			*fixed = false;
		if (x != 0)
			*x = 0;
		if (y != 0)
			*y = 0;
		if (w != 0)
			*w = 0;
		if (h != 0)
			*h = 0;
	}
	else
	{
		if (fixed != 0)
			*fixed = (pictureEntry->flags & DMG_FLAG_FIXED) ? 1 : 0;
		if (x != 0)
			*x = pictureEntry->x;
		if (y != 0)
			*y = pictureEntry->y;
		if (w != 0)
			*w = pictureEntry->width;
		if (h != 0)
			*h = pictureEntry->height;
	}
}

void VID_LoadPicture (uint8_t picno, DDB_ScreenMode screenMode)
{
	if (dmg == 0) 
		return;
    uint32_t t0 = GetMilliseconds();

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == 0 || entry->type != DMGEntry_Image)
		return;

	pictureOrigin = dmg;
	pictureEntry  = entry;
	pictureIndex  = picno;
	picturePlanes = entry->bitDepth ? entry->bitDepth : TEXT_PLANES;
	pictureStride = (entry->width + 15) / 16;
	picturePlaneStride = pictureStride * entry->height;
	pictureData   = (uint16_t*) DMG_GetEntryDataPlanar(dmg, picno);
	if (pictureData == 0 || picturePlanes > displayPlanes)
	{
		pictureEntry = 0;
		pictureOrigin = 0;
		pictureIndex = 0;
		picturePlanes = TEXT_PLANES;
	}
    else
    {
        DebugPrintf("VID_LoadPicture(%u): %ux%u %u plane(s), fixed=%d, compressed=%d, load %lu ms\n",
            (unsigned)picno,
            (unsigned)entry->width, (unsigned)entry->height,
            (unsigned)picturePlanes,
            (entry->flags & DMG_FLAG_FIXED) ? 1 : 0,
            (entry->flags & DMG_FLAG_COMPRESSED) ? 1 : 0,
            (unsigned long)(GetMilliseconds() - t0));
    }
}

void VID_OpenFileDialog (bool existing, char* filename, size_t bufferSize)
{
	// Not supported
}

void VID_PlaySample (uint8_t no, int* duration)
{
	DMG_Entry* entry = DMG_GetEntry(dmg, no);
	if (entry == NULL || entry->type != DMGEntry_Audio)
		return;

	uint8_t* audioDataFromFile = DMG_GetEntryData(dmg, no, ImageMode_Audio);
	if (audioDataFromFile == 0)
		return;

	uint8_t* buffer = audioDataFromFile;
	DMG_Cache* cache = DMG_GetImageCache(dmg, no, entry, entry->length);
	if (cache != 0)
	{
		buffer = (uint8_t*)(cache + 1);
		if (cache->populated == false)
		{
			MemCopy(buffer, audioDataFromFile, entry->length);
			ConvertSample(buffer, entry->length);
		}
	}
	else
	{
		// We could play the data from the file cache instead, but
		// - We may end up converting the sample twice
		// - It may not be in chip RAM in the future
		//
		// So, we're aborting for now. This case should never happen anyway,
		// since we should have enough image cache for any audio sample.
		return;
	}

	uint16_t inputHz;
	switch (entry->x)
	{
		case DMG_5KHZ:    inputHz =  5000; break;
		case DMG_7KHZ:    inputHz =  7000; break;
		case DMG_9_5KHZ:  inputHz =  9500; break;
		case DMG_15KHZ:   inputHz = 15000; break;
		case DMG_20KHZ:   inputHz = 20000; break;
		case DMG_30KHZ:   inputHz = 30000; break;
        case DMG_44_1KHZ: inputHz = 44100; break;
        case DMG_48KHZ:   inputHz = 48000; break;
		default:          inputHz = 11025; break;
	}

	if (duration != NULL)
		*duration = entry->length * 1000 / inputHz;

	PlaySample(buffer, entry->length, inputHz, 64);
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int volume)
{
	PlaySample((uint8_t*)buffer, samples, hz, volume);
}

void VID_Quit ()
{
	quit = true;
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	if (lines >= h)
	{
		VID_Clear(x, y, w, lines, paper);
		return;
	}
	if (!AdjustCoordinates(x, y, w, h))
		return;

	h -= lines;

	if (w*h > 8192)
		VID_VSync();
	for (int p = 0; p < (int)TEXT_PLANES; p++)
	{
		BlitterCopy(plane[p], x, y + lines, plane[p], x, y, w, h, true); 
		BlitterRect(plane[p], x, y + h, w, lines, paper & 1);
		paper >>= 1;
	}
}

__attribute__((noinline))
void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	activePalette[color] = (r << 16) | (g << 8) | b;
	SetEncodedAGAPalette(color, activePalette[color]);
	if (!isAGA || displayPlanes != 8 || color < 32)
		VID_SetColor(color, EncodeColor12(r, g, b));
}

void VID_SetTextInputMode (bool enabled)
{
	// Not needed
}

void VID_RestoreScreen ()
{
	DebugPrintf(displaySwap ? "Restoring screen (front to back)\n" : "Restoring screen (back to front)\n");
	WaitBlit();
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(front, back, screenAllocate);
	MemCopy(activePalette, savedPalette, sizeof(activePalette));
	VID_SetPaletteEntries(activePalette, savedPaletteColors);
	VID_ActivatePalette();
}

void VID_SaveScreen ()
{
	DebugPrintf(displaySwap ? "Saving screen (back to front)\n" : "Saving screen (front to back)\n");
	WaitBlit();
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(back, front, screenAllocate);
	MemCopy(savedPalette, activePalette, sizeof(activePalette));
	savedPaletteColors = (displayPlanes == 8 && isAGA) ? 256 : (displayPlanes > 4 ? 32 : 16);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	if (displaySwap)
		front = !front;
	switch (op)
	{
		case SCR_OP_DRAWTEXT:
			charToBack = !front ^ displaySwap;
			return;
		case SCR_OP_DRAWPICTURE:
			drawToBack = !front ^ displaySwap;
			if (front) {
				DebugPrintf("Drawing set to front buffer");
			} else {
				DebugPrintf("Drawing set to back buffer");
			}
			break;
	}
	UpdateDrawPlanes();
}

void VID_SwapScreen ()
{
	displaySwap = !displaySwap;
	charToBack = !charToBack;
	drawToBack = !drawToBack;

	uint16_t* program = BeginCopperBuild();
	uint16_t* end = program;
	if (displaySwap)
		end = SetVisiblePlanes(backPlane, end);
	else
		end = SetVisiblePlanes(frontPlane, end);

	CommitCopperBuild(program, end);

	if (displaySwap) {
		DebugPrintf("Display swapped\n");
	} else {
		DebugPrintf("Display restored\n");
	}

	UpdateDrawPlanes();
}

void VID_MainLoop (DDB_Interpreter* i, void (*callback)(int elapsed))
{
	interpreter = i;
	
	while (!quit)
	{
		callback(0);
#if DEBUG_MEMORY
		DrawMemoryHUD();
#endif
	}

	quit = false;
}

void VID_VSync() 
{
	debug_start_idle();

	// vblank begins at vpos 312 hpos 1 
	//          ends at vpos 25 hpos 1
    // vsync  begins at line 2 hpos 132 
	//          ends at vpos 5 hpos 18 

	while (1) 
	{
		volatile uint32_t vpos = R_VPOSR;
		vpos &= 0x1ff00;
		if (vpos != (311<<8))
			break;
	}
	while (1) 
	{
		volatile uint32_t vpos = R_VPOSR;
		vpos &= 0x1ff00;
		if (vpos == (311<<8))
			break;
	}
	
	debug_stop_idle();
}

// set up a 320x256 lowres display
INLINE uint16_t* SetScreenLayout(uint16_t* copListEnd) 
{
	const uint16_t x      = 128;
	const uint16_t width  = 320;
	const uint16_t height = 200;
	const uint16_t y      = isPAL ? 64  : 44;
	const uint16_t RES    = 8; 			//8=lowres,4=hires
	uint16_t       xstop  = x+width;
	uint16_t       ystop  = y+height;
	uint16_t       fw     = (x>>1)-RES;

	*copListEnd++ = offsetof(Custom, ddfstrt);
	*copListEnd++ = fw;
	*copListEnd++ = offsetof(Custom, ddfstop);
	*copListEnd++ = fw + (((width >> 4) - 1) << 3);
	*copListEnd++ = offsetof(Custom, diwstrt);
	*copListEnd++ = x + (y << 8);
	*copListEnd++ = offsetof(Custom, diwstop);
	*copListEnd++ = (xstop - 256) + ((ystop - 256) << 8);
	return copListEnd;
}

INLINE uint16_t* SetBitPlanes(uint16_t* copPtr)
{
	// Enable bitplanes	
	*copPtr++ = offsetof(Custom, bplcon0);
	*copPtr++ = (1<<9) | EncodePlaneCount(displayPlanes);
	*copPtr++ = offsetof(Custom, bplcon1);	// Scrolling
	*copPtr++ = 0;
	*copPtr++ = offsetof(Custom, bplcon2);	// playfied priority
	*copPtr++ = 1<<6;//0x24;			    // Sprites have priority over playfields
	
	// Set bitplane modulo
	*copPtr++ = offsetof(Custom, bpl1mod); //odd planes   1,3
	*copPtr++ = SCR_STRIDEB - SCR_WIDTHB;
	*copPtr++ = offsetof(Custom, bpl2mod); //even  planes 2,4
	*copPtr++ = SCR_STRIDEB - SCR_WIDTHB;

	return copPtr;
}

void ProgramDisplay()
{
	uint16_t* program = BeginCopperBuild();
	uint16_t* copPtr = program;
	copPtr = SetScreenLayout(copPtr);
	copPtr = SetBitPlanes(copPtr);
	copPtr = SetVisiblePlanes(frontPlane, copPtr);
	CommitCopperBuild(program, copPtr);
}

bool VID_Initialize(DDB_Machine machine, DDB_Version version, DDB_ScreenMode screenMode)
{
	if (initialized)
		return true;
	initialized = true;

	CloseWorkBench();
	OpenTimer();

	if (SysBase->VBlankFrequency == 50)
		isPAL = true;
	else
		isPAL = false;

	uint8_t chipRevBits0 = 0;
	if (GfxBase->LibNode.lib_Version >= 39)
		chipRevBits0 = GfxBase->ChipRevBits0;
	isAGA = (chipRevBits0 & (GFXF_AA_ALICE | GFXF_AA_LISA | GFXF_AA_MLISA)) != 0;
	copperListBytes = isAGA ? 4096 : 1024;
	DebugPrintf("VID_Initialize: machine=%d version=%d screenMode=%d requestedPlanes=%u isAGA=%d chipRevBits0=%02x\n",
		(int)machine, (int)version, (int)screenMode, (unsigned)requestedDisplayPlanes,
		isAGA ? 1 : 0, (unsigned)chipRevBits0);

	screenHeight  = 200;
	screenWidth   = 320;
	lineHeight    = 8;
	columnWidth   = 6;

	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset,      DefaultCharset, 1024);
	memcpy(charset+1024, DefaultCharset, 1024);
	charsetWords = (uint16_t(*)[256][8])AllocMem(8192, MEMF_CHIP | MEMF_CLEAR);
	rotationTable = (uint16_t*)AllocMem(8 * 256 * sizeof(uint16_t), MEMF_CLEAR);
	if (charsetWords == 0 || rotationTable == 0)
	{
		VID_Finish();
		return false;
	}
	if (!VID_InitializeTextDraw())
	{
		VID_Finish();
		return false;
	}
	VID_ActivateCharset();

	copperLists[0] = (uint16_t*)AllocMem(copperListBytes, MEMF_CHIP);
	copperLists[1] = (uint16_t*)AllocMem(copperListBytes, MEMF_CHIP);
	copper1 = copperLists[0];
	activeCopperList = 0;
	if (copperLists[0] == 0 || copperLists[1] == 0)
	{
		VID_Finish();
		return false;
	}
	
	TakeSystem();
	if (!ConfigureDisplayPlanes(requestedDisplayPlanes))
	{
		VID_Finish();
		return false;
	}

	VID_SetPalette(DefaultPalette);
	VID_ActivatePalette();

	return true;
}

void VID_Finish ()
{
	if (!initialized)
		return;
	initialized = false;

	FreeSystem();
	
	CloseTimer();
	VID_FinishTextDraw();

	if (charsetWords)
	{
		FreeMem(charsetWords, 8192);
		charsetWords = 0;
	}
	if (rotationTable)
	{
		FreeMem(rotationTable, 8 * 256 * sizeof(uint16_t));
		rotationTable = 0;
	}

	if (copperLists[0])
	{
		FreeMem(copperLists[0], copperListBytes);
		copperLists[0] = 0;
	}
	if (copperLists[1])
	{
		FreeMem(copperLists[1], copperListBytes);
		copperLists[1] = 0;
	}
	copper1 = 0;
	activeCopperList = 0;
	copperListBytes = 1024;
	if (backBuffer)
	{
		FreeMem(backBuffer, screenAllocate);
		backBuffer = 0;
	}
	if (scratchBuffer)
	{
		FreeMem(scratchBuffer, screenAllocate);
		scratchBuffer = 0;
	}
	if (frontBuffer)
	{
		FreeMem(frontBuffer, screenAllocate);
		frontBuffer = 0;
	}

	displayPlanes = TEXT_PLANES;
	requestedDisplayPlanes = TEXT_PLANES;
	screenBytesPerPlane = SCR_BPNEXTB;
	screenAllocate = SCR_BPNEXTB * TEXT_PLANES;

	OpenWorkBench();
}

#endif
