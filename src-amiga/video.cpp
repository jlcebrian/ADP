#ifdef _AMIGA

#include "gcc8_c_support.h"

#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <ddb_data.h>
#include <ddb_xmsg.h>
#include <os_bito.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_file.h>
#include <dmg.h>
#include <dmg_font.h>

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

extern struct View* ActiView;

#define INLINE __attribute__((always_inline)) inline 

#ifndef DEBUG_MEMORY
#define DEBUG_MEMORY 0
#endif

#define R_VPOSR ( *(volatile uint32_t*)0xDFF004 )

extern volatile Custom *custom;
extern LONG os_blockCount;
extern LONG os_totalAllocated;

bool	 supportsOpenFileDialog = false;
DDB_Machine screenMachine = DDB_MACHINE_IBMPC;

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

uint8_t*    		plane[MAX_PLANES];
uint8_t*			frontBuffer = 0;
uint8_t*			backBuffer = 0;
uint8_t*    		frontPlane[MAX_PLANES];
uint8_t*    		backPlane[MAX_PLANES];

static bool         backBufferEnabled = false;
static uint8_t* 	scratchDisplayBuffer = 0;
static uint8_t* 	scratchDisplayPlane[MAX_PLANES];
static uint8_t  	displayPlanes = TEXT_PLANES;
static uint8_t  	requestedDisplayPlanes = TEXT_PLANES;
static uint32_t 	screenBytesPerPlane = SCR_BPNEXTB;
static uint32_t 	screenAllocate = SCR_BPNEXTB * TEXT_PLANES;
static uint32_t 	activePalette[256];
static uint32_t 	frontPalette[256];
static uint32_t 	backPalette[256];
static uint32_t 	scratchDisplayPalette[256];
static uint32_t 	savedPalette[256];
static uint16_t 	savedPaletteColors = 16;

static uint16_t* 	activePaletteAGAHigh = 0;
static uint16_t* 	activePaletteAGALow = 0;
static uint16_t   paletteHighR[256];
static uint16_t   paletteHighG[256];
static uint16_t   paletteHighB[256];
static uint16_t   paletteLowR[256];
static uint16_t   paletteLowG[256];
static uint16_t   paletteLowB[256];
static uint8_t    paletteNibbleExpand[16];

static DMG*       	pictureOrigin;
static DMG_Entry* 	pictureEntry = 0;
static int        	pictureIndex = 0;
static uint16_t*  	pictureData = 0;
static uint32_t   	pictureStride;
static uint32_t   	picturePlaneStride;
static uint8_t    	picturePlanes = TEXT_PLANES;
static bool       	picturePlaneMajor = false;

uint16_t rotationTable[8][256];
uint16_t* copper1;
static uint16_t* copperLists[2] = { 0, 0 };
static uint8_t activeCopperList = 0;
static uint32_t copperListBytes = 1024;

INLINE uint16_t* SetVisiblePlanes(uint8_t** planes, uint16_t* copListEnd);
INLINE void RunCopperProgram(uint16_t* program, uint16_t* end);
INLINE uint16_t* SetScreenLayout(uint16_t* copListEnd);
INLINE uint16_t* SetBitPlanes(uint16_t* copPtr);
static inline uint16_t* BeginCopperBuild();
static inline void CommitCopperBuild(uint16_t* program, uint16_t* end);
static void UpdateDrawPlanes();
static void SetPlanePointers(uint8_t* buffer, uint8_t** out);
static void RefreshBackPlanePointers();
static void ProgramDisplay();
static void InitializePaletteEncodeTables();
static void VID_CommitPalette(bool waitForVBlank);
static void VID_StagePaletteEntries(const uint32_t* palette, uint16_t count, uint16_t firstColor = 0, bool clearOutside = true, bool immediate = true);
static uint16_t* AppendCopperPalette(uint16_t* copListEnd);

static uint16_t GetDisplayPaletteCapacity()
{
	if (displayPlanes == 8 && isAGA)
		return 256;
	if (displayPlanes > 4)
		return 32;
	return 16;
}

static uint32_t* GetVisiblePaletteStore()
{
	return displaySwap ? backPalette : frontPalette;
}

static uint32_t* GetHiddenPaletteStore()
{
	return displaySwap ? frontPalette : backPalette;
}

static uint32_t* GetPlaneTargetPaletteStore()
{
	if (plane[0] == frontBuffer)
		return frontPalette;
	if (plane[0] == backBuffer)
		return backPalette;
	return GetVisiblePaletteStore();
}

static void CopyPaletteStore(uint32_t* dst, const uint32_t* src)
{
	MemCopy(dst, src, sizeof(activePalette));
}

static bool PaletteStoreNeedsUpdate(const uint32_t* store, const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside)
{
	uint16_t limit = GetDisplayPaletteCapacity();
	if (firstColor > limit)
		return false;
	if (count > limit - firstColor)
		count = limit - firstColor;

	for (uint16_t n = 0; n < count; n++)
	{
		if (store[firstColor + n] != palette[n])
			return true;
	}

	if (clearOutside)
	{
		for (uint16_t n = 0; n < firstColor; n++)
		{
			if (store[n] != 0)
				return true;
		}
		for (uint16_t n = firstColor + count; n < limit; n++)
		{
			if (store[n] != 0)
				return true;
		}
	}

	return false;
}

static void UpdatePaletteStore(uint32_t* store, const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside)
{
	uint16_t limit = GetDisplayPaletteCapacity();
	if (firstColor > limit)
		return;
	if (count > limit - firstColor)
		count = limit - firstColor;

	if (clearOutside)
	{
		for (uint16_t n = 0; n < firstColor; n++)
			store[n] = 0;
		for (uint16_t n = firstColor + count; n < limit; n++)
			store[n] = 0;
	}

	for (uint16_t n = 0; n < count; n++)
		store[firstColor + n] = palette[n];
}

static void ApplyPaletteStore(const uint32_t* store, bool waitForVBlank)
{
	CopyPaletteStore(activePalette, store);
	VID_StagePaletteEntries(activePalette, GetDisplayPaletteCapacity(), 0, false, false);
	VID_CommitPalette(waitForVBlank);
}

void VID_SetPaletteRangeFast(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool waitForVBlank)
{
	uint32_t* store = GetVisiblePaletteStore();
	UpdatePaletteStore(activePalette, palette, count, firstColor, clearOutside);
	UpdatePaletteStore(store, palette, count, firstColor, clearOutside);
	VID_StagePaletteEntries(activePalette, GetDisplayPaletteCapacity(), 0, false, false);
	VID_CommitPalette(waitForVBlank);
}

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

static DMG_Cache* FindImageCacheItem(uint8_t index, const void* payload)
{
	if (dmg == 0 || dmg->cache == 0)
		return 0;
	if ((dmg->cacheBitmap[index >> 3] & (1 << (index & 7))) == 0)
		return 0;

	for (DMG_Cache* item = dmg->cache; item != dmg->cacheTail; item = item->next)
	{
		if (item->index == index && (const void*)(item + 1) == payload)
			return item;
	}

	return 0;
}

static void ClearScratchDisplayBuffer(uint8_t color)
{
	for (uint8_t p = 0; p < displayPlanes; p++)
		BlitterRect(scratchDisplayPlane[p], 0, 0, screenWidth, screenHeight, (color & (1 << p)) != 0);
	WaitBlit();
}

static void PresentScratchDisplayBuffer()
{
	CopyPaletteStore(activePalette, scratchDisplayPalette);
	VID_StagePaletteEntries(activePalette, GetDisplayPaletteCapacity(), 0, false, false);

	if (isAGA && displayPlanes == 8)
	{
		VID_VSync();
		VID_CommitPalette(false);
		for (uint16_t i = 0; i < MAX_PLANES; i++)
			custom->bplpt[i] = (APTR)scratchDisplayPlane[i];

		uint16_t* program = copperLists[activeCopperList];
		uint16_t* end = program;
		end = SetScreenLayout(end);
		end = SetBitPlanes(end);
		end = SetVisiblePlanes(scratchDisplayPlane, end);
		*end++ = 0xffff;
		*end++ = 0xfffe;
		copper1 = program;
	}
	else
	{
		uint16_t* program = BeginCopperBuild();
		uint16_t* end = program;
		end = SetScreenLayout(end);
		end = SetBitPlanes(end);
		end = SetVisiblePlanes(scratchDisplayPlane, end);
		VID_VSync();
		end = AppendCopperPalette(end);
		RunCopperProgram(program, end);
		copper1 = program;
		activeCopperList = (program == copperLists[1]) ? 1 : 0;
	}

	if (displaySwap)
	{
		uint8_t* oldVisible = backBuffer;
		uint32_t oldPalette[256];
		CopyPaletteStore(oldPalette, backPalette);
		backBuffer = scratchDisplayBuffer;
		scratchDisplayBuffer = oldVisible;
		RefreshBackPlanePointers();
		CopyPaletteStore(backPalette, scratchDisplayPalette);
		CopyPaletteStore(scratchDisplayPalette, oldPalette);
	}
	else
	{
		uint8_t* oldVisible = frontBuffer;
		uint32_t oldPalette[256];
		CopyPaletteStore(oldPalette, frontPalette);
		frontBuffer = scratchDisplayBuffer;
		scratchDisplayBuffer = oldVisible;
		SetPlanePointers(frontBuffer, frontPlane);
		RefreshBackPlanePointers();
		CopyPaletteStore(frontPalette, scratchDisplayPalette);
		CopyPaletteStore(scratchDisplayPalette, oldPalette);
	}
	SetPlanePointers(scratchDisplayBuffer, scratchDisplayPlane);
	CopyPaletteStore(activePalette, GetVisiblePaletteStore());
	if (dmg != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchDisplayBuffer, screenAllocate, false);
	UpdateDrawPlanes();
}

static void PresentScratchDisplayPlanesOnly()
{
	if (isAGA && displayPlanes == 8)
	{
		VID_VSync();
		for (uint16_t i = 0; i < MAX_PLANES; i++)
			custom->bplpt[i] = (APTR)scratchDisplayPlane[i];

		uint16_t* program = copperLists[activeCopperList];
		uint16_t* end = program;
		end = SetScreenLayout(end);
		end = SetBitPlanes(end);
		end = SetVisiblePlanes(scratchDisplayPlane, end);
		*end++ = 0xffff;
		*end++ = 0xfffe;
		copper1 = program;
	}
	else
	{
		uint16_t* program = BeginCopperBuild();
		uint16_t* end = program;
		end = SetScreenLayout(end);
		end = SetBitPlanes(end);
		end = SetVisiblePlanes(scratchDisplayPlane, end);
		CommitCopperBuild(program, end);
	}

	if (displaySwap)
	{
		uint8_t* oldVisible = backBuffer;
		backBuffer = scratchDisplayBuffer;
		scratchDisplayBuffer = oldVisible;
		RefreshBackPlanePointers();
	}
	else
	{
		uint8_t* oldVisible = frontBuffer;
		frontBuffer = scratchDisplayBuffer;
		scratchDisplayBuffer = oldVisible;
		SetPlanePointers(frontBuffer, frontPlane);
		RefreshBackPlanePointers();
	}

	SetPlanePointers(scratchDisplayBuffer, scratchDisplayPlane);
	CopyPaletteStore(scratchDisplayPalette, GetVisiblePaletteStore());
	CopyPaletteStore(activePalette, GetVisiblePaletteStore());
	if (dmg != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchDisplayBuffer, screenAllocate, false);
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
	return (uint16_t)(paletteHighR[r] | paletteHighG[g] | paletteHighB[b]);
}

static inline uint16_t EncodeColor24High(uint32_t rgb)
{
	return (uint16_t)(paletteHighR[(rgb >> 16) & 0xFF] |
		paletteHighG[(rgb >> 8) & 0xFF] |
		paletteHighB[rgb & 0xFF]);
}

static inline uint16_t EncodeColor24Low(uint32_t rgb)
{
	return (uint16_t)(paletteLowR[(rgb >> 16) & 0xFF] |
		paletteLowG[(rgb >> 8) & 0xFF] |
		paletteLowB[rgb & 0xFF]);
}

static inline void SetEncodedAGAPalette(uint8_t color, uint32_t rgb)
{
	if (activePaletteAGAHigh == 0 || activePaletteAGALow == 0)
		return;
	uint8_t r = (uint8_t)(rgb >> 16);
	uint8_t g = (uint8_t)(rgb >> 8);
	uint8_t b = (uint8_t)rgb;
	activePaletteAGAHigh[color] = (uint16_t)(paletteHighR[r] | paletteHighG[g] | paletteHighB[b]);
	activePaletteAGALow[color] = (uint16_t)(paletteLowR[r] | paletteLowG[g] | paletteLowB[b]);
}

static void InitializePaletteEncodeTables()
{
	for (uint16_t value = 0; value < 256; value++)
	{
		paletteHighR[value] = (uint16_t)((value & 0xF0) << 4);
		paletteHighG[value] = (uint16_t)(value & 0xF0);
		paletteHighB[value] = (uint16_t)(value >> 4);
		paletteLowR[value] = (uint16_t)((value & 0x0F) << 8);
		paletteLowG[value] = (uint16_t)((value & 0x0F) << 4);
		paletteLowB[value] = (uint16_t)(value & 0x0F);
	}

	for (uint16_t value = 0; value < 16; value++)
		paletteNibbleExpand[value] = (uint8_t)((value << 4) | value);
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

static void RefreshBackPlanePointers()
{
	if (backBufferEnabled && backBuffer != 0)
		SetPlanePointers(backBuffer, backPlane);
	else
		SetPlanePointers(frontBuffer, backPlane);
}

static void UploadAGAPalette(uint16_t count)
{
	if (activePaletteAGAHigh == 0 || activePaletteAGALow == 0)
		return;

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

bool VID_IsAGAAvailable()
{
	if (GfxBase == 0 || GfxBase->LibNode.lib_Version < 39)
		return false;

	uint8_t chipRevBits0 = GfxBase->ChipRevBits0;
	return (chipRevBits0 & (GFXF_AA_ALICE | GFXF_AA_LISA | GFXF_AA_MLISA)) != 0;
}

void VID_ActivateCharset()
{
	for (int shift = 0; shift < 8; shift++)
	{
		uint16_t* table = rotationTable[shift];
		for (int value = 0; value < 256; value++)
			table[value] = (uint16_t)(((uint16_t)value << 8) >> shift);
	}
}

void VID_SetCharset(const uint8_t* newCharset)
{
	MemCopy(charset, newCharset, 2048);
	VID_ActivateCharset();
}

void VID_SetCharsetWidth(uint8_t width)
{
	for (int n = 0; n < 256; n++)
		charWidth[n] = width;
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

static bool LoadSINTACFont(const char* filename)
{
	DMG_Font* font = Allocate<DMG_Font>("SINTAC Font");
	if (font == 0)
		return false;

	if (!DMG_ReadSINTACFont(filename, font))
	{
		Free(font);
		return false;
	}

	MemCopy(charset, font->bitmap8, sizeof(font->bitmap8));
	MemCopy(charWidth, font->width8, sizeof(font->width8));
	Free(font);
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

static uint32_t GetRecommendedClassicImageCacheSize(DMG* file)
{
	// Classic DAT entries don't expose image dimensions until their per-entry
	// headers are processed. Avoid forcing that work during startup and reserve
	// enough space for at least one full-screen 4-plane image plus cache metadata.
	uint32_t fullScreenImage = ((320 + 15) & ~15) * 200 / 2;
	uint32_t itemOverhead = ((sizeof(DMG_Cache) + 31) & ~31) + 32;
	uint32_t recommended = fullScreenImage + itemOverhead;
	recommended = (recommended + 0x0FFF) & ~0x0FFF;
	return recommended;
}

static uint32_t GetEstimatedClassicTotalPlanarSize(DMG* file)
{
	if (file == 0)
		return 0;

	// For classic DATs, use the total file size as a cheap proxy and assume the
	// compressed image payload averages 25% of the planar output size.
	uint32_t estimated = file->fileSize * 4;
	return (estimated + 0x0FFF) & ~0x0FFF;
}

static uint32_t GetDesiredImageCacheSize(DMG* file, uint32_t freeMemory, uint32_t minimumCache)
{
	uint32_t usefulCache =
		file != 0 && file->version == DMG_Version5 ?
		GetTotalPlanarImageSize(file) :
		GetEstimatedClassicTotalPlanarSize(file);
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

static const char* DescribeScreenMode(DDB_ScreenMode screenMode)
{
	switch (screenMode)
	{
		case ScreenMode_CGA: return "CGA";
		case ScreenMode_EGA: return "EGA";
		case ScreenMode_VGA16: return "VGA16";
		case ScreenMode_VGA: return "VGA256";
		case ScreenMode_HiRes: return "HiRes";
		case ScreenMode_SHiRes: return "SuperHiRes";
		default: return "Text";
	}
}

static uint8_t GetRequiredDisplayPlanes(DMG* file)
{
	if (file == 0 || file->version != DMG_Version5)
		return TEXT_PLANES;

	switch (file->colorMode)
	{
		case DMG_DAT5_COLORMODE_PLANAR4:
			return TEXT_PLANES;
		case DMG_DAT5_COLORMODE_PLANAR5:
			return 5;
		case DMG_DAT5_COLORMODE_PLANAR8:
			return 8;
		default:
			return TEXT_PLANES;
	}
}

static bool IsSupportedDAT5ColorMode(uint8_t colorMode)
{
	switch ((DMG_DAT5ColorMode)colorMode)
	{
		case DMG_DAT5_COLORMODE_PLANAR4:
		case DMG_DAT5_COLORMODE_PLANAR5:
		case DMG_DAT5_COLORMODE_PLANAR8:
			return true;
		default:
			return false;
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
			uint8_t planeCount = DMG_DAT5ModePlaneCount(header[0x0E]);
			*requiredPlanes = planeCount == 0 ? 4 : planeCount;
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
	if (displayPlanes == planes && frontBuffer != 0)
		return true;

	uint32_t bytesPerPlane = SCR_STRIDEB * SCR_HEIGHTPX;
	uint32_t allocate = bytesPerPlane * planes;
	uint8_t* newFront = AllocateInPool<uint8_t>("VID Front buffer", OSMemoryPool_Chip, allocate, false);
	uint8_t* newScratch = AllocateInPool<uint8_t>("VID Scratch buffer", OSMemoryPool_Chip, allocate, false);
	if (newFront == 0 || newScratch == 0)
	{
		if (newFront != 0) Free(newFront);
		if (newScratch != 0) Free(newScratch);
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}

	MemClear(newFront, allocate);
	MemClear(newScratch, allocate);

	if (backBufferEnabled)
	{
		uint8_t* newBack = AllocateInPool<uint8_t>("VID Back buffer", OSMemoryPool_Chip, allocate, false);
		if (newBack == 0)
		{
			Free(newFront);
			Free(newScratch);
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}
		
		uint8_t* oldBack = backBuffer;
		MemClear(newBack, allocate);
		backBuffer = newBack;
		RefreshBackPlanePointers();
		if (oldBack != 0)
			Free(oldBack);
	}

	uint8_t* oldFront = frontBuffer;
	uint8_t* oldScratch = scratchDisplayBuffer;

	displayPlanes = planes;
	screenBytesPerPlane = bytesPerPlane;
	screenAllocate = allocate;
	frontBuffer = newFront;
	scratchDisplayBuffer = newScratch;
	displaySwap = false;
	charToBack = false;
	drawToBack = false;

	SetPlanePointers(frontBuffer, frontPlane);
	RefreshBackPlanePointers();
	SetPlanePointers(scratchDisplayBuffer, scratchDisplayPlane);
	if (dmg != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchDisplayBuffer, screenAllocate, false);
	UpdateDrawPlanes();
	ProgramDisplay();

	if (oldFront != 0)
		Free(oldFront);
	if (oldScratch != 0)
		Free(oldScratch);
	return true;
}

void VID_EnableBackBuffer()
{
	backBufferEnabled = true;

	if (frontBuffer != 0)
	{
		uint32_t bytesPerPlane = SCR_STRIDEB * SCR_HEIGHTPX;
		uint32_t allocate = bytesPerPlane * displayPlanes;
		backBuffer = AllocateInPool<uint8_t>("VID Back buffer", OSMemoryPool_Chip, allocate, false);
		if (backBuffer == 0)
		{
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			backBufferEnabled = false;
			return;
		}
		
		MemClear(backBuffer, allocate);
		RefreshBackPlanePointers();
	}
}

bool VID_IsBackBufferEnabled()
{
	return backBufferEnabled;
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

	uint8_t r = paletteNibbleExpand[(pal >> 8) & 0x0F];
	uint8_t g = paletteNibbleExpand[(pal >> 4) & 0x0F];
	uint8_t b = paletteNibbleExpand[pal & 0x0F];
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
	CopyPaletteStore(GetVisiblePaletteStore(), activePalette);
	VID_CommitPalette(true);
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

static void VID_StagePaletteEntries(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool immediate)
{
	uint16_t maxColors = (displayPlanes == 8 && isAGA) ? 256 : 32;
	if (firstColor >= maxColors)
		return;
	if (count > maxColors - firstColor)
		count = maxColors - firstColor;

	if (immediate)
	{
		for (uint16_t n = 0; n < count; n++)
		{
			uint32_t rgb = palette[n];
			VID_SetPaletteColor(firstColor + n,
				(rgb >> 16) & 0xFF,
				(rgb >> 8) & 0xFF,
				rgb & 0xFF);
		}
		if (clearOutside)
		{
			for (uint16_t n = 0; n < firstColor; n++)
				VID_SetPaletteColor(n, 0, 0, 0);
			for (uint16_t n = firstColor + count; n < maxColors; n++)
				VID_SetPaletteColor(n, 0, 0, 0);
		}
	}
	else
	{
		bool updateCopper = !isAGA || displayPlanes != 8;
		for (uint16_t n = 0; n < count; n++)
		{
			uint16_t color = firstColor + n;
			uint32_t rgb = palette[n];
			uint8_t r = (uint8_t)(rgb >> 16);
			uint8_t g = (uint8_t)(rgb >> 8);
			uint8_t b = (uint8_t)rgb;
			activePalette[color] = rgb;
			if (activePaletteAGAHigh != 0 && activePaletteAGALow != 0)
			{
				activePaletteAGAHigh[color] = (uint16_t)(paletteHighR[r] | paletteHighG[g] | paletteHighB[b]);
				activePaletteAGALow[color] = (uint16_t)(paletteLowR[r] | paletteLowG[g] | paletteLowB[b]);
			}
			if (updateCopper || color < 32)
				copper2[color * 2 + 1] = (uint16_t)(paletteHighR[r] | paletteHighG[g] | paletteHighB[b]);
		}
		if (clearOutside)
		{
			for (uint16_t color = 0; color < firstColor; color++)
			{
				activePalette[color] = 0;
				SetEncodedAGAPalette((uint8_t)color, 0);
				if (updateCopper || color < 32)
					copper2[color * 2 + 1] = 0;
			}
			for (uint16_t color = firstColor + count; color < maxColors; color++)
			{
				activePalette[color] = 0;
				SetEncodedAGAPalette((uint8_t)color, 0);
				if (updateCopper || color < 32)
					copper2[color * 2 + 1] = 0;
			}
		}
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

void VID_SetPaletteEntries(const uint32_t* palette, uint16_t count, uint16_t firstColor, bool clearOutside, bool waitForVBlank)
{
	VID_StagePaletteEntries(palette, count, firstColor, clearOutside, false);
	VID_CommitPalette(waitForVBlank);
}

bool VID_AnyKey ()
{
	return InputBufferHead != InputBufferTail;
}

void VID_WaitForKey ()
{
	while (VID_AnyKey())
	{
		uint8_t key;
		VID_GetKey(&key, 0, 0);
	}

	while (!VID_AnyKey())
		;
}

bool VID_LoadDataFile (const char* fileName)
{
	pictureOrigin = 0;
	pictureEntry = 0;
	pictureIndex = 0;
	pictureData = 0;
	pictureStride = 0;
	picturePlaneStride = 0;
	picturePlanes = TEXT_PLANES;
	picturePlaneMajor = false;

	if (dmg != 0)
	{
		DMG_Close(dmg);
		dmg = 0;
	}

	uint8_t probePlanes = TEXT_PLANES;
	uint16_t probeWidth = 0;
	uint16_t probeHeight = 0;
	uint8_t probeColorMode = 0;
	if (ProbeDataFileHeader(fileName, &probePlanes, &probeWidth, &probeHeight, &probeColorMode))
	{
		if (probeColorMode != 0)
		{
			if (!IsSupportedDAT5ColorMode(probeColorMode))
			{
				DebugPrintf("Rejecting DAT5 mode %u on Amiga target\n", (unsigned)probeColorMode);
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
			if (probeWidth != 320 || probeHeight != 200)
			{
				DebugPrintf("Rejecting DAT5 from header probe due to unsupported target size %ux%u\n",
					(unsigned)probeWidth, (unsigned)probeHeight);
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
			if (probePlanes == 8 && !isAGA)
			{
				DebugPrintf("Rejecting DAT5 Planar8 from header probe on non-AGA machine\n");
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
			}
		}
	}

	const char* datName = ChangeExtension(fileName, ".dat");
	const char* loadedName = datName;
	uint32_t tOpen = GetMilliseconds();
	dmg = DMG_Open(datName, true);
	if (dmg == 0)
	{
		const char* egaName = ChangeExtension(fileName, ".ega");
		loadedName = egaName;
		dmg = DMG_Open(egaName, true);
	}
	if (dmg == 0)
	{
		const char* cgaName = ChangeExtension(fileName, ".cga");
		loadedName = cgaName;
		dmg = DMG_Open(cgaName, true);
	}
	if (dmg == 0)
	{
		DebugPrintf("No data file found for %s\n", fileName);
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	DebugPrintf("Loaded %s in %lu ms\n", loadedName, (unsigned long)(GetMilliseconds() - tOpen));

	#if HAS_PSG
	if (screenMachine == DDB_MACHINE_ATARIST || screenMachine == DDB_MACHINE_AMIGA)
	{
		if (!DDB_InitializePSGPlayback())
		{
			DMG_Close(dmg);
			dmg = 0;
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}
	}
	#endif

	uint8_t requiredPlanes = GetRequiredDisplayPlanes(dmg);
	if (dmg->version == DMG_Version5 && (dmg->targetWidth != 320 || dmg->targetHeight != 200))
	{
		DebugPrintf("Unsupported DAT5 target size %ux%u\n", (unsigned)dmg->targetWidth, (unsigned)dmg->targetHeight);
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	if (dmg->version == DMG_Version5 && !IsSupportedDAT5ColorMode(dmg->colorMode))
	{
		DebugPrintf("Rejecting DAT5 mode %u on Amiga target\n", (unsigned)dmg->colorMode);
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	if (requiredPlanes == 8 && !isAGA)
	{
		DebugPrintf("Rejecting Planar8 DAT5 on non-AGA machine\n");
		DMG_Close(dmg);
		dmg = 0;
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}
	if (requiredPlanes != displayPlanes)
	{
		DebugPrintf("Reconfiguring display planes for data file (required=%u active=%u)\n",
			(unsigned)requiredPlanes, (unsigned)displayPlanes);
		if (!ConfigureDisplayPlanes(requiredPlanes))
		{
			DMG_Close(dmg);
			dmg = 0;
			return false;
		}
	}

	if (scratchDisplayBuffer != 0)
		DMG_SetZX0ScratchBuffer(dmg, scratchDisplayBuffer, screenAllocate, false);
	uint32_t requiredImageCache = 0;
	if (dmg->version == DMG_Version5)
		requiredImageCache = GetRecommendedDAT5ImageCacheSize(dmg);
	uint32_t imageCacheReserve = requiredImageCache;
	if (imageCacheReserve == 0)
		imageCacheReserve = GetRecommendedClassicImageCacheSize(dmg);

	bool fontLoaded = LoadSINTACFont(ChangeExtension(fileName, ".FNT")) ||
		LoadSINTACFont(ChangeExtension(fileName, ".fnt"));

	if (!fontLoaded && !LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
		VID_ActivateCharset();
	}

	uint32_t datSize = File_GetSize(dmg->file);
	uint32_t freeMemory = AvailMem(0);
	DebugPrintf("Free memory: %u bytes (datSize: %u)\n", freeMemory, datSize);
	uint32_t desiredImageCache = GetDesiredImageCacheSize(dmg, freeMemory, imageCacheReserve);
	DebugPrintf("Desired image cache: %u bytes\n", (unsigned)desiredImageCache);

	#if HAS_XMSG
	if (xmsgFilePresent)
	{
		if (freeMemory > datSize + 65536)
			DDB_InitializeXMessageCache(16384);
		else
			DDB_InitializeXMessageCache(4096);
	}
	#endif

	if (freeMemory > datSize + 65536)			// Everything fits
	{
		uint32_t fileCacheBudget = 0;
		if (freeMemory > imageCacheReserve)
			fileCacheBudget = freeMemory - imageCacheReserve;
		DMG_SetupFileCache(dmg, fileCacheBudget, VID_ShowProgressBar);
		freeMemory = AvailMem(0);
		if (freeMemory >= imageCacheReserve)
		{
			uint32_t cacheBytes = GetDesiredImageCacheSize(dmg, freeMemory, imageCacheReserve);
			DMG_SetupImageCache(dmg, cacheBytes);
		}
	}
	else if (freeMemory > datSize + 32768)		// DAT barely fits
	{
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x40000)				// >256K free
	{
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x20000)				// >128K free
	{
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else 
	{
		uint32_t cacheBytes = desiredImageCache;
		DMG_SetupImageCache(dmg, cacheBytes);
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
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

void VID_Clear (int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	int originalX = x;
	int originalY = y;
	int originalW = w;
	int originalH = h;

	if (!AdjustCoordinates(x, y, w, h))
		return;
		
	if (y >= screenHeight || w < 1 || h < 1)
		return;

	bool fullScreenClear =
		originalX <= 0 && originalY <= 0 &&
		originalW >= screenWidth && originalH >= screenHeight;

	if (fullScreenClear && IsVisiblePlaneTarget() && scratchDisplayBuffer != 0)
	{
		ClearScratchDisplayBuffer(color);
		CopyPaletteStore(scratchDisplayPalette, GetVisiblePaletteStore());
		PresentScratchDisplayBuffer();
		return;
	}
		
	if (w*h >= 8192)
		VID_VSync();

	uint8_t planesToClear = mode == Clear_All ? displayPlanes : TEXT_PLANES;
	if (mode != Clear_All)
	{
		// Keep text/window clears on the low planes so picture data survives.
		if (fullScreenClear || ((color >> TEXT_PLANES) != 0))
			planesToClear = displayPlanes;
	}

	for (uint8_t p = 0; p < planesToClear; p++)
		BlitterRect(plane[p], x, y, w, h, (color & (1 << p)) != 0);
}

void VID_ClearBuffer (bool front)
{
	uint8_t** p = front ^ displaySwap ? frontPlane : backPlane;
	for (uint8_t n = 0; n < displayPlanes; n++)
		BlitterRect(p[n], 0, 0, screenWidth, screenHeight, 0);
}

void VID_PresentDefaultScreen()
{
	if (scratchDisplayBuffer == 0)
	{
		VID_ClearBuffer(true);
		VID_ClearBuffer(false);
		VID_SetDefaultPalette();
		VID_ActivatePalette();
		CopyPaletteStore(frontPalette, activePalette);
		CopyPaletteStore(backPalette, activePalette);
		return;
	}

	for (uint8_t n = 0; n < displayPlanes; n++)
		BlitterRect(scratchDisplayPlane[n], 0, 0, screenWidth, screenHeight, 0);
	WaitBlit();

	VID_StagePaletteEntries(DefaultPalette, 16, 0, true, false);
	UpdatePaletteStore(scratchDisplayPalette, DefaultPalette, 16, 0, true);
	PresentScratchDisplayBuffer();
	CopyPaletteStore(GetHiddenPaletteStore(), GetVisiblePaletteStore());

	uint8_t** hidden = displaySwap ? frontPlane : backPlane;
	for (uint8_t n = 0; n < displayPlanes; n++)
	{
		BlitterRect(hidden[n], 0, 0, screenWidth, screenHeight, 0);
		BlitterRect(scratchDisplayPlane[n], 0, 0, screenWidth, screenHeight, 0);
	}
	WaitBlit();
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode screenMode)
{
	if (pictureEntry == 0)
		return;

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
	if (x >= screenWidth || y >= screenHeight)
		return;
	if (x + w > screenWidth)
		w = screenWidth - x;
	if (y + h > screenHeight)
		h = screenHeight - y;
	if (w <= 0 || h <= 0)
		return;

	if (h > pictureEntry->height)
		h = pictureEntry->height;
	if (w > pictureEntry->width)
		w = pictureEntry->width;
	if (w <= 0 || h <= 0)
		return;
    uint16_t copyPixelWidth = (uint16_t)w;
    uint16_t copyPixelHeight = (uint16_t)h;

	uint32_t* palette = DMG_GetEntryPalette(dmg, pictureIndex);
	uint16_t paletteSize = DMG_GetEntryPaletteSize(dmg, pictureIndex);
    uint8_t paletteFirst = DMG_GetEntryFirstColor(dmg, pictureIndex);
	bool presentingScratch = false;
	bool paletteChangeNeeded = false;
	bool shouldUpdatePalette = false;
	bool clearOutside = true;
	bool visibleTarget = false;
	uint8_t** targetPlanes = presentingScratch ? scratchDisplayPlane : plane;
	uint32_t* targetPaletteStore = GetPlaneTargetPaletteStore();
	switch (screenMode)
	{
		default:
		case ScreenMode_VGA16:
		case ScreenMode_VGA:
		case ScreenMode_HiRes:
		case ScreenMode_SHiRes:
			if (pictureEntry->flags & DMG_FLAG_FIXED)
			{
				shouldUpdatePalette = true;
				clearOutside = paletteFirst == 0;
				// TODO: This is a hack to fix the palette for V1
				if (dmg->version == DMG_Version1)
					pictureEntry->RGB32Palette[15] = 0xFFFFFFFF;
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			if (pictureEntry->flags & DMG_FLAG_FIXED)
			{
				palette = DMG_GetCGAMode(pictureEntry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
				paletteSize = 4;
				paletteFirst = 0;
				shouldUpdatePalette = true;
				clearOutside = true;
			}
			break;
	}

	if (shouldUpdatePalette)
	{
		paletteChangeNeeded = PaletteStoreNeedsUpdate(targetPaletteStore, palette, paletteSize, paletteFirst, clearOutside);
		visibleTarget = IsVisiblePlaneTarget();
		presentingScratch = visibleTarget && paletteChangeNeeded;
		if (presentingScratch)
		{
			CopyPaletteStore(scratchDisplayPalette, GetVisiblePaletteStore());
			UpdatePaletteStore(scratchDisplayPalette, palette, paletteSize, paletteFirst, clearOutside);
			targetPaletteStore = scratchDisplayPalette;
			targetPlanes = scratchDisplayPlane;
		}
		else
		{
			UpdatePaletteStore(targetPaletteStore, palette, paletteSize, paletteFirst, clearOutside);
			if (IsVisiblePlaneTarget() && paletteChangeNeeded)
				ApplyPaletteStore(targetPaletteStore, false);
		}
	}

	if (!picturePlaneMajor)
	{
		DebugPrintf("WARNING: Picture is not in native plane-major format");
		return;
	}

	if (picturePlanes > displayPlanes)
		picturePlanes = displayPlanes;

	if (presentingScratch)
	{
		// DebugPrintf("Using scratch display buffer flip to present picture");
		CopyScreenBuffer(GetVisiblePlanes(), scratchDisplayPlane);
	}

	uint16_t pictureStrideBytes = (uint16_t)(pictureStride * 2);
	for (uint8_t i = 0; i < picturePlanes; i++)
	{
		uint8_t* srcPlane = (uint8_t*)pictureData + i * picturePlaneStride * 2;
		BlitterCopyStride(srcPlane, pictureStrideBytes, 0, 0, targetPlanes[i], SCR_STRIDEB, (uint16_t)x, (uint16_t)y, copyPixelWidth, copyPixelHeight, true);
	}
	WaitBlit();
	if (presentingScratch)
	{
		PresentScratchDisplayBuffer();
		CopyScreenBuffer(GetVisiblePlanes(), displaySwap ? frontPlane : backPlane);
		CopyPaletteStore(GetHiddenPaletteStore(), GetVisiblePaletteStore());
	}
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
		DumpMemory(AvailMem(MEMF_CHIP), AvailMem(MEMF_CHIP | MEMF_LARGEST));
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

uint16_t VID_GetPaletteSize()
{
	if (displayPlanes == 8 && isAGA)
		return 256;
	if (displayPlanes > 4)
		return 32;
	return 16;
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

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == 0 || entry->type != DMGEntry_Image)
		return;

	if (pictureOrigin == dmg && pictureEntry == entry && pictureIndex == picno && pictureData != 0)
		return;

	pictureOrigin = dmg;
	pictureEntry  = entry;
	pictureIndex  = picno;
	picturePlanes = entry->bitDepth ? entry->bitDepth : TEXT_PLANES;
	uint32_t widthWords = (uint32_t)(entry->width + 15) >> 4;
	pictureData   = (uint16_t*) DMG_GetEntryDataNative(dmg, picno);
	if (pictureData != 0)
	{
		picturePlaneMajor = true;
		pictureStride = widthWords;
		picturePlaneStride = pictureStride * entry->height;
	}
	if (pictureData == 0 || picturePlanes > displayPlanes)
	{
		pictureEntry = 0;
		pictureOrigin = 0;
		pictureIndex = 0;
		picturePlanes = TEXT_PLANES;
		picturePlaneMajor = false;
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
	int originalX = x;
	int originalY = y;
	int originalW = w;
	int originalH = h;

	if (lines >= h)
	{
		VID_Clear(x, y, w, lines, paper);
		return;
	}
	if (!AdjustCoordinates(x, y, w, h))
		return;

	bool fullScreenScroll =
		originalX <= 0 && originalY <= 0 &&
		originalW >= screenWidth && originalH >= screenHeight;
	bool presentingScratch = fullScreenScroll && IsVisiblePlaneTarget() && scratchDisplayBuffer != 0;

	h -= lines;

	if (!presentingScratch && w*h > 8192)
		VID_VSync();

	if (presentingScratch)
	{
		uint8_t** visiblePlanes = GetVisiblePlanes();

		for (uint8_t p = 0; p < displayPlanes; p++)
		{
			if (p < TEXT_PLANES)
			{
				BlitterCopy(visiblePlanes[p], x, y + lines, scratchDisplayPlane[p], x, y, w, h, true);
				BlitterRect(scratchDisplayPlane[p], x, y + h, w, lines, paper & 1);
				paper >>= 1;
			}
			else
			{
				BlitterCopy(visiblePlanes[p], 0, 0, scratchDisplayPlane[p], 0, 0, screenWidth, screenHeight, true);
			}
		}

		WaitBlit();
		PresentScratchDisplayPlanesOnly();
		return;
	}

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
	GetVisiblePaletteStore()[color] = activePalette[color];
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
	if (backBuffer == 0)
	{
		DebugPrintf("WARNING: RestoreScreen called with no back buffer enabled");
		return;
	}

	DebugPrintf(displaySwap ? "Restoring screen (front to back)\n" : "Restoring screen (back to front)\n");
	WaitBlit();
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(front, back, screenAllocate);
	CopyPaletteStore(GetVisiblePaletteStore(), GetHiddenPaletteStore());
	ApplyPaletteStore(GetVisiblePaletteStore(), true);
}

void VID_SaveScreen ()
{
	if (backBuffer == 0)
	{
		DebugPrintf("WARNING: SaveScreen called with no back buffer enabled");
		return;
	}
	
	DebugPrintf(displaySwap ? "Saving screen (back to front)\n" : "Saving screen (front to back)\n");
	WaitBlit();
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(back, front, screenAllocate);
	CopyPaletteStore(GetHiddenPaletteStore(), GetVisiblePaletteStore());
	MemCopy(savedPalette, activePalette, sizeof(activePalette));
	savedPaletteColors = (displayPlanes == 8 && isAGA) ? 256 : (displayPlanes > 4 ? 32 : 16);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	if (backBuffer == 0)
	{
		DebugPrintf("WARNING: SetOpBuffer called with no back buffer enabled");
		return;
	}
	
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
	if (backBuffer == 0)
	{
		DebugPrintf("WARNING: SwapScreen called with no back buffer enabled");
		return;
	}
	
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
	ApplyPaletteStore(GetVisiblePaletteStore(), false);

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

	ActiView = GfxBase->ActiView;
	CloseWorkBench();
	OpenTimer();

	if (SysBase->VBlankFrequency == 50)
		isPAL = true;
	else
		isPAL = false;

	isAGA = VID_IsAGAAvailable();
	copperListBytes = isAGA ? 4096 : 1024;
	screenMachine = machine;
	DebugPrintf("Video: %s %u plane%s%s\n",
		DescribeScreenMode(screenMode),
		(unsigned)requestedDisplayPlanes,
		requestedDisplayPlanes == 1 ? "" : "s",
		isAGA ? " AGA" : "");

	screenHeight  = 200;
	screenWidth   = 320;
	lineHeight    = 8;
	columnWidth   = 6;

	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset,      DefaultCharset, 1024);
	memcpy(charset+1024, DefaultCharset, 1024);
	InitializePaletteEncodeTables();
	if (isAGA)
	{
		activePaletteAGAHigh = Allocate<uint16_t>("AGA palette high", 256);
		activePaletteAGALow = Allocate<uint16_t>("AGA palette low", 256);
		if (activePaletteAGAHigh == 0 || activePaletteAGALow == 0)
		{
			VID_Finish();
			return false;
		}
	}
	if (!VID_InitializeTextDraw())
	{
		VID_Finish();
		return false;
	}
	VID_ActivateCharset();

	copperLists[0] = (uint16_t*)AllocateBlockInPool("VID Copper list 0", copperListBytes, false, OSMemoryPool_Chip);
	copperLists[1] = (uint16_t*)AllocateBlockInPool("VID Copper list 1", copperListBytes, false, OSMemoryPool_Chip);
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

	VID_SetDefaultPalette();
	VID_ActivatePalette();

	return true;
}

void VID_Finish ()
{
	if (!initialized)
		return;
	initialized = false;

	FreeSystem();
	OpenWorkBench();
	
	CloseTimer();
	VID_FinishTextDraw();
	if (activePaletteAGAHigh)
	{
		Free(activePaletteAGAHigh);
		activePaletteAGAHigh = 0;
	}
	if (activePaletteAGALow)
	{
		Free(activePaletteAGALow);
		activePaletteAGALow = 0;
	}

	if (copperLists[0])
	{
		Free(copperLists[0]);
		copperLists[0] = 0;
	}
	if (copperLists[1])
	{
		Free(copperLists[1]);
		copperLists[1] = 0;
	}
	copper1 = 0;
	activeCopperList = 0;
	copperListBytes = 1024;
	if (backBuffer)
	{
		Free(backBuffer);
		backBuffer = 0;
	}
	if (scratchDisplayBuffer)
	{
		Free(scratchDisplayBuffer);
		scratchDisplayBuffer = 0;
	}
	if (frontBuffer)
	{
		Free(frontBuffer);
		frontBuffer = 0;
	}

	displayPlanes = TEXT_PLANES;
	requestedDisplayPlanes = TEXT_PLANES;
	screenBytesPerPlane = SCR_BPNEXTB;
	screenAllocate = SCR_BPNEXTB * TEXT_PLANES;
}

#endif
