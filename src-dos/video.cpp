#ifdef _DOS

#include <ddb_vid.h>
#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_xmsg.h>
#include <dmg_font.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#include "dmg.h"
#include "sb.h"
#include "mixer.h"
#include "timer.h"
#include "vid_modex.h"

#include <i86.h>
#include <conio.h>
#include <dos.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t*   pictureData = 0;
#if HAS_PCX
static uint8_t*   pcxPictureData = 0;
static uint32_t   pcxPictureSize = 0;
static int        pcxPictureWidth = 0;
static int        pcxPictureHeight = 0;
static uint32_t   pcxPalette[256];
#endif
static uint8_t*   audioData = 0;
static DMG_Entry* bufferedEntry;
static int        bufferedEntryIndex;
static bool       bufferedPictureIndexed = false;
static bool       initialized = false;
static bool       quit;
static uint8_t    defaultCharWidth = 6;

DDB_Machine    screenMachine = DDB_MACHINE_IBMPC;
DDB_ScreenMode screenMode = ScreenMode_VGA16;

bool exitGame = false;
bool supportsOpenFileDialog = false;

void VID_SetWindowTitle(const char* title)
{
	(void)title;
}

void VID_SetWindowIcon(const char* fileName)
{
	(void)fileName;
}

#if DEBUG_ALLOCS
extern void DOS_GetStackWatermark(uint32_t* usedBytes, uint32_t* totalBytes);
#endif

// ----------------------------------------------------------------------------
//  File stuff
// ----------------------------------------------------------------------------

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

static bool LoadCharset (uint8_t* ptr, const char* filename)
{
	FILE* file = fopen(filename, "rb");
	if (file == NULL)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	fseek(file, 0, SEEK_END);
	if (ftell(file) != 2176)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		fclose(file);
		return false;
	}
	fseek(file, 128, SEEK_SET);
	fread(ptr, 1, 2048, file);
	fclose(file);
	return true;
}

static bool LoadSINTACFont(const char* filename)
{
	DMG_Font font;
	if (!DMG_ReadSINTACFont(filename, &font))
		return false;

	MemCopy(charset, font.bitmap8, sizeof(font.bitmap8));
	MemCopy(charWidth, font.width8, sizeof(font.width8));
	return true;
}

#if HAS_PCX
static bool FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == NULL)
		return false;
	File_Close(file);
	return true;
}

static void BuildFileNameWithExtension(const char* fileName, const char* extension, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, fileName);
	char* dot = (char*)StrRChr(output, '.');
	if (dot == NULL)
		dot = output + StrLen(output);
	StrCopy(dot, output + outputSize - dot, extension);
}

static bool FindNamedPCXFile(const char* fileName, char* output, size_t outputSize)
{
	BuildFileNameWithExtension(fileName, ".VGA", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".vga", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".PCX", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".pcx", output, outputSize);
	return FileExists(output);
}

static void FreeBufferedPCXPicture()
{
	if (pcxPictureData != NULL)
	{
		Free(pcxPictureData);
		pcxPictureData = NULL;
	}
	pcxPictureSize = 0;
	pcxPictureWidth = 0;
	pcxPictureHeight = 0;
}

static bool HasExternalPCXGraphics(const char* fileName)
{
	char introScreen[FILE_MAX_PATH];
	if (FindNamedPCXFile(fileName, introScreen, sizeof(introScreen)))
		return true;

	for (int picno = 0; picno < 256; picno++)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (VID_GetExternalPictureFileName((uint8_t)picno, pictureFileName, sizeof(pictureFileName)))
			return true;
	}

	return false;
}

static bool IsPCXScreenFile(const char* fileName)
{
	const char* dot = StrRChr(fileName, '.');
	if (dot != NULL && (StrIComp(dot, ".vga") == 0 || StrIComp(dot, ".pcx") == 0))
		return true;

	uint8_t header[128];
	File* file = File_Open(fileName, ReadOnly);
	if (file == NULL)
		return false;
	bool ok = File_Read(file, header, sizeof(header)) == sizeof(header);
	File_Close(file);
	return ok && header[0] == 0x0A && header[2] == 1 && header[3] == 8 && header[65] == 1;
}
#endif

// ----------------------------------------------------------------------------
//  Video related variables
// ----------------------------------------------------------------------------
static uint32_t palette[256];
static uint8_t  paletteR[256];
static uint8_t  paletteG[256];
static uint8_t  paletteB[256];
static uint8_t  fg;
static int      cx;
static int      cy;

#if defined(__386__)
struct DPMI_MemoryInfo
{
	uint32_t largestBlockAvail;
	uint32_t maxUnlockedPage;
	uint32_t largestLockablePage;
	uint32_t linearAddressSpace;
	uint32_t numFreePagesAvail;
	uint32_t numPhysicalPagesFree;
	uint32_t totalPhysicalPages;
	uint32_t freeLinearAddressSpace;
	uint32_t sizeOfPageFile;
	uint32_t reserved[3];
};

static DPMI_MemoryInfo dpmiMemInfo;
#endif

static void GetDOSFreeMemoryInfo(uint32_t* totalFree, uint32_t* largestBlock)
{
#if defined(__386__)
	union REGS regs;
	struct SREGS sregs;

	MemSet(&dpmiMemInfo, 0xFF, sizeof(dpmiMemInfo));
	MemClear(&regs, sizeof(regs));
	MemClear(&sregs, sizeof(sregs));

	regs.x.eax = 0x00000500UL;
	sregs.es = FP_SEG(&dpmiMemInfo);
	regs.x.edi = FP_OFF(&dpmiMemInfo);

	int386x(0x31, &regs, &regs, &sregs);

	if (dpmiMemInfo.largestBlockAvail != 0xFFFFFFFFUL)
	{
		if (largestBlock != NULL)
			*largestBlock = dpmiMemInfo.largestBlockAvail;

		if (totalFree != NULL)
		{
			if (dpmiMemInfo.numFreePagesAvail != 0xFFFFFFFFUL)
				*totalFree = dpmiMemInfo.numFreePagesAvail * 4096UL;
			else
				*totalFree = dpmiMemInfo.largestBlockAvail;
		}
	}
	else
	{
		if (totalFree != NULL)
			*totalFree = 0;
		if (largestBlock != NULL)
			*largestBlock = 0;
	}
#else
	unsigned segments[128];
	unsigned segmentCount = 0;
	uint32_t total = 0;
	uint32_t largest = 0;

	for (;;)
	{
		unsigned paragraphs = 0;
		if (_dos_allocmem(0xFFFFu, &paragraphs) == 0)
		{
			// Unexpected full-size success. Account for it and stop.
			segments[segmentCount++] = paragraphs;
			total += 0xFFFFUL * 16UL;
			if (largest < 0xFFFFUL * 16UL)
				largest = 0xFFFFUL * 16UL;
			break;
		}

		if (paragraphs == 0)
			break;

		uint32_t blockBytes = (uint32_t)paragraphs * 16UL;
		if (blockBytes > largest)
			largest = blockBytes;

		if (segmentCount == sizeof(segments) / sizeof(segments[0]))
			break;

		unsigned segment = 0;
		if (_dos_allocmem(paragraphs, &segment) != 0)
			break;

		segments[segmentCount++] = segment;
		total += blockBytes;
	}

	while (segmentCount > 0)
		_dos_freemem(segments[--segmentCount]);

	if (totalFree != NULL)
		*totalFree = total;
	if (largestBlock != NULL)
		*largestBlock = largest;
#endif
}

static uint32_t GetDesiredDOSImageCacheSize(uint32_t totalFree, uint32_t largestBlock)
{
	uint32_t available = largestBlock != 0 ? largestBlock : totalFree;
	if (available >= 256UL * 1024UL)
		return 128UL * 1024UL;
	if (available >= 160UL * 1024UL)
		return 96UL * 1024UL;
	if (available >= 128UL * 1024UL)
		return 64UL * 1024UL;
	if (available >= 96UL * 1024UL)
		return 48UL * 1024UL;
	if (available >= 64UL * 1024UL)
		return 32UL * 1024UL;
	if (available >= 48UL * 1024UL)
		return 16UL * 1024UL;

	DebugPrintf("Warning: not enough memory available (%lu bytes) for image cache\n", available);
	return 0;
}

#if DEBUG_ALLOCS
#endif

// ----------------------------------------------------------------------------
//  Video driver
// ----------------------------------------------------------------------------

void VID_VSync()
{
	/* Wait for vertical retrace */
	while (!(inp(0x3DA) & 0x08))			// IST1
		;		
	while (inp(0x3DA) & 0x08)				// IST1
		;		
}

static void SetHardwarePaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	outp(0x3C8, index);					// PAL WRITE
	outp(0x3C9, r >> 2);				// PAL DATA
	outp(0x3C9, g >> 2);
	outp(0x3C9, b >> 2);
}

void VID_SetPaletteColor (uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	palette[index] = 0xFF000000UL | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
	paletteR[index] = r;
	paletteG[index] = g;
	paletteB[index] = b;
	SetHardwarePaletteColor(index, r, g, b);
}

void VID_ActivatePalette()
{
	for (int n = 0; n < 256; n++)
		SetHardwarePaletteColor((uint8_t)n, paletteR[n], paletteG[n], paletteB[n]);
}

void VID_WaitForKey()
{
	while (!VID_AnyKey())
		;
}

bool VID_AnyKey ()
{
	return kbhit();
}

static void ApplyPalette256(const uint32_t* colors)
{
	for (int n = 0; n < 256; n++)
	{
		uint8_t r = (colors[n] >> 16) & 0xFF;
		uint8_t g = (colors[n] >> 8) & 0xFF;
		uint8_t b = colors[n] & 0xFF;
		VID_SetPaletteColor((uint8_t)n, r, g, b);
	}
}

bool VID_LoadDataFile (const char* fileName)
{
	#if HAS_PCX
	FreeBufferedPCXPicture();
	VID_SetExternalPictureBase(0);
	#endif

	columnWidth = 6;
	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;
	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);

	if (dmg != NULL)
	{
		DMG_Close(dmg);
		dmg = NULL;
	}

	char resolvedDataFile[FILE_MAX_PATH];
	DDB_ScreenMode resolvedDataMode = screenMode;
	bool resolved = DDB_ResolveDataFile(fileName, DDB_MACHINE_IBMPC, screenMode, resolvedDataFile, sizeof(resolvedDataFile), &resolvedDataMode, 0);
	if (!resolved && screenMode != ScreenMode_Default)
		resolved = DDB_ResolveDataFile(fileName, DDB_MACHINE_IBMPC, ScreenMode_Default, resolvedDataFile, sizeof(resolvedDataFile), &resolvedDataMode, 0);
	if (resolved)
	{
		dmg = DMG_Open(resolvedDataFile, true);
		if (dmg != NULL)
			dmg->screenMode = resolvedDataMode;
		screenMode = resolvedDataMode;
	}
	if (dmg == NULL)
	{
		#if HAS_PCX
		VID_SetExternalPictureBase(fileName);
		if (HasExternalPCXGraphics(fileName))
		{
			columnWidth = 8;
			if (!LoadSINTACFont(ChangeExtension(fileName, ".FNT")) &&
				!LoadSINTACFont(ChangeExtension(fileName, ".fnt")) &&
				!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
				!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
			{
				// Keep the default fixed-width charset restored above.
			}
			return true;
		}
		VID_SetExternalPictureBase(0);
		#endif

		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		VID_Finish();
		return false;
	}

	if (!LoadSINTACFont(ChangeExtension(fileName, ".FNT")) &&
		!LoadSINTACFont(ChangeExtension(fileName, ".fnt")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		// Keep the default fixed-width charset restored above.
	}

	#if HAS_XMSG
	DDB_InitializeXMessageCache(32768);
	#endif

	uint32_t totalFree = 0;
	uint32_t largestBlock = 0;
	GetDOSFreeMemoryInfo(&totalFree, &largestBlock);
	DebugPrintf("Free RAM: %lu bytes (%lu largest block)\n", totalFree, largestBlock);
	uint32_t imageCacheBytes = GetDesiredDOSImageCacheSize(totalFree, largestBlock);
	if (imageCacheBytes == 0)
	{
		DebugPrintf("Unable to get image cache size\n");
		VID_Finish();
		abort();
	}
	if (imageCacheBytes != 0)
		DMG_SetupImageCache(dmg, imageCacheBytes);

	return true;
}

bool VID_IsBackBufferEnabled()
{
	return true;
}

void VID_EnableBackBuffer()
{
	// Always enabled
}

void VID_Clear (int x, int y, int width, int height, uint8_t color, VID_ClearMode mode)
{
	(void)mode;
	ModeX_ClearRect(x, y, width, height, color);
}

void VID_Finish()
{
	if (!initialized)
		return;

	#if HAS_PCX
	FreeBufferedPCXPicture();
	VID_SetExternalPictureBase(0);
	#endif

	SB_Stop();
	Timer_Stop();

	VID_Clear(0, 0, screenWidth, screenHeight, 0);
	VID_VSync();

	ModeX_SetVideoMode(MODE_TEXT);
	initialized = false;
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode screenMode)
{	
	(void)screenMode;

	#if HAS_PCX
	if (pcxPictureData != NULL)
	{
		if (w > pcxPictureWidth)
			w = pcxPictureWidth;
		if (h > pcxPictureHeight)
			h = pcxPictureHeight;
		ModeX_BlitLinearPicture(pcxPictureData, pcxPictureWidth, x, y, w, h);
		ApplyPalette256(pcxPalette);
		return;
	}
	#endif

	DMG_Entry* entry = bufferedEntry;
	if (entry == NULL)
		return;
		
	bool updatePalette = (entry->flags & DMG_FLAG_FIXED) != 0;
	bool useScratchPresentation = updatePalette && ModeX_BeginFixedPicturePresentation();

	if (bufferedPictureIndexed)
		ModeX_BlitLinearPicture(pictureData, entry->width, x, y, w, h);
	else
		ModeX_BlitNativePicture(pictureData, entry->width, entry->height, x, y, w, h);

	if (updatePalette)
	{
		uint32_t* palette = DMG_GetEntryPalette(dmg, bufferedEntryIndex);
		if (dmg->version == DMG_Version1)
			palette[15] = 0xFFFFFFFF;
		VID_VSync();
		for (int n = 0; n < 16; n++) {
			uint8_t r = (palette[n] >> 16) & 0xFF;
			uint8_t g = (palette[n] >>  8) & 0xFF;
			uint8_t b = (palette[n] >>  0) & 0xFF;
			VID_SetPaletteColor(n, r, g, b);
		}
	}

	if (useScratchPresentation)
		ModeX_EndFixedPicturePresentation();
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	#if HAS_PCX
	if (IsPCXScreenFile(fileName))
	{
		uint32_t bufferSize = 65535;
		uint8_t* output = Allocate<uint8_t>("Temporary PCX buffer", bufferSize);
		if (output == NULL)
		{
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}

		int width = 0;
		int height = 0;
		if (!DMG_DecompressPCX(fileName, output, &bufferSize, &width, &height, pcxPalette))
		{
			Free(output);
			return false;
		}

		ModeX_BlitLinearPicture(output, width, 0, 0, width, height);
		ApplyPalette256(pcxPalette);
		Free(output);
		return true;
	}
	#endif

	size_t required = screenWidth * screenHeight;
	uint32_t palette[16];
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", required);
	if (output == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	if (buffer == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		Free(output);
		return false;
	}

	if (SCR_GetScreen(fileName, target, buffer, 32768, output, 
			screenWidth, screenHeight, palette))
	{
		if (fadeIn)
		{
			for (int n = 0; n < 16; n++) 
				VID_SetPaletteColor(n, 0, 0, 0);
			VID_VSync();
		}

		ModeX_BlitLinearPicture(output, screenWidth, 0, 0, screenWidth, screenHeight);
		
		if (fadeIn)
		{
			VID_VSync();
			for (int n = 0; n < 16; n++) 
			{
				uint8_t r = (palette[n] >> 16) & 0xFF;
				uint8_t g = (palette[n] >>  8) & 0xFF;
				uint8_t b = (palette[n] >>  0) & 0xFF;
				VID_SetPaletteColor(n, r, g, b);
			}
		}
	}

	Free(output);
	Free(buffer);
	return true;
}

void VID_DrawCharacter (int x, int y, uint8_t c, uint8_t ink, uint8_t paper)
{
	ModeX_DrawCharacter(x, y, c, ink, paper);
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* modifiers)
{
	union REGS regs;
	regs.h.ah = 0x02;
#if defined(__386__)
	int386(0x16, &regs, &regs);
#else
	int86(0x16, &regs, &regs);
#endif

	*key = 0;
	*ext = 0;

	if (kbhit())
	{
		*key = getch();
		if (*key == 0)
		{
			*ext = getch();

			if (*ext == 0x44 && interpreter)
			{
				if (++interpreter->keyClick == 3)
					interpreter->keyClick = 0;
			}
		}
	}

	*modifiers = 0;
	if (regs.h.al & 0x03)
		*modifiers |= SCR_KEYMOD_SHIFT;
	if (regs.h.al & 0x04)
		*modifiers |= SCR_KEYMOD_CTRL;
	if (regs.h.al & 0x08)
		*modifiers |= SCR_KEYMOD_ALT;

#if DEBUG_ALLOCS
	if (*ext == 0x3C)	// F2
	{
		uint32_t totalFree = 0;
		uint32_t largestBlock = 0;
		uint32_t stackUsed = 0;
		uint32_t stackTotal = 0;
		GetDOSFreeMemoryInfo(&totalFree, &largestBlock);
		DOS_GetStackWatermark(&stackUsed, &stackTotal);
		DumpMemory(totalFree, largestBlock, stackUsed, stackTotal);
		if (key) *key = 0;
		if (ext) *ext = 0;
		*ext = 0;
		*key = 0;
	}
#endif
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = Timer_GetMilliseconds();

	// union REGS regs;
	// regs.h.ah = 0x00;
	// int386(0x1A, &regs, &regs);
	// *time = ((regs.w.cx << 16) + regs.w.dx) * 55;
}

uint16_t VID_GetPaletteSize()
{
	return 256;
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	outp(0x3C7, color);

	if (r != NULL)
	{
		uint8_t v = inp(0x3C9);
		*r = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}

	if (g != NULL)
	{
		uint8_t v = inp(0x3C9);
		*g = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}

	if (b != NULL)
	{
		uint8_t v = inp(0x3C9);
		*b = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	#if HAS_PCX
	if (pcxPictureData != NULL)
	{
		if (fixed != NULL)
			*fixed = true;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = pcxPictureWidth;
		if (h != NULL)
			*h = pcxPictureHeight;
		return;
	}
	#endif

	if (bufferedEntry == NULL)
	{
		if (fixed != NULL)
			*fixed = false;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = 0;
		if (h != NULL)
			*h = 0;
	}
	else
	{
		if (fixed != NULL)
			*fixed = (bufferedEntry->flags & DMG_FLAG_FIXED) != 0;
		if (x != NULL)
			*x = bufferedEntry->x;
		if (y != NULL)
			*y = bufferedEntry->y;
		if (w != NULL)
			*w = bufferedEntry->width;
		if (h != NULL)
			*h = bufferedEntry->height;
	}
}

void VID_LoadPicture (uint8_t picno, DDB_ScreenMode mode)
{
	#if HAS_PCX
	FreeBufferedPCXPicture();
	bufferedEntry = NULL;
	pictureData = NULL;

	if (dmg == NULL)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (!VID_GetExternalPictureFileName(picno, pictureFileName, sizeof(pictureFileName)))
			return;

		pcxPictureSize = (uint32_t)DMG_MAX_IMAGE_WIDTH * (uint32_t)DMG_MAX_IMAGE_HEIGHT;
		pcxPictureData = Allocate<uint8_t>("PCX picture", pcxPictureSize);
		if (pcxPictureData == NULL)
			return;

		if (!DMG_DecompressPCX(pictureFileName, pcxPictureData, &pcxPictureSize, &pcxPictureWidth, &pcxPictureHeight, pcxPalette))
			FreeBufferedPCXPicture();
		return;
	}
	#else
	if (dmg == NULL)
		return;
	#endif

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Image)
		return;

	bufferedEntry = entry;
	bufferedEntryIndex = picno;
	bufferedPictureIndexed = false;
	pictureData = DMG_GetEntryDataNative(dmg, picno);
	if (pictureData == NULL)
	{
		pictureData = DMG_GetEntryData(dmg, picno, ImageMode_Indexed);
		if (pictureData != NULL)
		{
			DMG_SetError(DMG_ERROR_NONE);
			bufferedPictureIndexed = true;
		}
		else
		{
			bufferedEntry = NULL;
			bufferedPictureIndexed = false;
		}
	}
}

void VID_OpenFileDialog (bool existing, char* filename, size_t bufferSize)
{
	// Not supported
}

void VID_PlaySample (uint8_t no, int* duration)
{
	if (dmg == NULL) return;

	DMG_Entry* entry = DMG_GetEntry(dmg, no);
	if (entry == NULL || entry->type != DMGEntry_Audio)
		return;

	audioData = DMG_GetEntryDataChunky(dmg, no);
	if (audioData == false)
		return;

	int sampleHz;
	switch (entry->x)
	{
		case DMG_5KHZ:     sampleHz =  5000; break;
		case DMG_7KHZ:     sampleHz =  7000; break;
		case DMG_9_5KHZ:   sampleHz =  9500; break;
		case DMG_15KHZ:    sampleHz = 15000; break;
		case DMG_20KHZ:    sampleHz = 20000; break;
		case DMG_30KHZ:    sampleHz = 30000; break;
        case DMG_44_1KHZ:  sampleHz = 44100; break;
        case DMG_48KHZ:    sampleHz = 48000; break;
		default:           sampleHz = 11025; break;
	}
	MIX_PlaySample(audioData, entry->length, sampleHz, 256);

	if (duration != NULL)
		*duration = entry->length * 1000 / sampleHz;
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int volume)
{
	MIX_PlaySample((uint8_t*)buffer, samples, hz, volume);
}

void VID_Quit ()
{
	quit = true;
}

void VID_RestoreScreen ()
{
	ModeX_RestoreScreen();
}

void VID_SaveScreen ()
{
	ModeX_SaveScreen();
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	ModeX_Scroll(x, y, w, h, lines, paper);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	(void)op;
	ModeX_SetActiveBuffer(front);
}

void VID_ClearBuffer (bool front)
{
	ModeX_ClearBuffer(front, 0);
}

void VID_SwapScreen ()
{
	ModeX_SwapBuffers();
}

void VID_MainLoop (DDB_Interpreter* i, void (*callback)(int elapsed))
{
	uint32_t prev;
	uint32_t time;
	uint32_t elapsed = 0;

	interpreter = i;
	VID_GetMilliseconds(&prev);

	while (!quit)
	{
		VID_GetMilliseconds(&time);
		elapsed = time - prev;
		if (elapsed < 16)
			VID_VSync();

		VID_GetMilliseconds(&time);
		elapsed = time - prev;
		prev = time;

		callback(elapsed);
	}

	quit = false;
}

bool VID_Initialize(DDB_Machine machine, DDB_Version version, DDB_ScreenMode mode)
{
	(void)version;

	if (initialized)
		return false;

	screenMachine = machine;
	screenMode = mode;

	if (machine != DDB_MACHINE_IBMPC)
	{
		DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
		return false;
	}

	if (mode != ScreenMode_CGA &&
		mode != ScreenMode_EGA &&
		mode != ScreenMode_VGA16 &&
		mode != ScreenMode_VGA)
	{
		DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
		return false;
	}

	union REGS regs;
	regs.w.ax = 0x1A00;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
	if (regs.h.al != 0x1A)
	{
		DDB_SetError(DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED);
		return false;
	}

	if (SB_Init(0, 30000))
		SB_Start();

	Timer_Start();

	screenWidth   = 320;
	screenHeight  = 200;
	lineHeight    = 8;
	columnWidth   = 6;
	defaultCharWidth = 6;
	screenWidth   = 320;
	screenHeight  = 200;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);
	ModeX_SetVideoMode(MODE_320x200);

	initialized = true;
	return true;
}

void VID_SetDisplayPlanesHint(uint8_t planes)
{
	(void)planes;
}

void VID_SetTextInputMode (bool enabled)
{
	// Not supported
}

#endif
