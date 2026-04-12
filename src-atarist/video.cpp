#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_pal.h>
#include <ddb_vid.h>
#include <ddb_xmsg.h>
#include <dmg.h>
#include <dmg_font.h>
#include <os_char.h>
#include <os_mem.h>

#include "textdraw.h"

#ifdef _ATARIST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include <time.h>

#ifndef DEBUG_MEMORY
#define DEBUG_MEMORY 0
#endif

#ifndef DEBUG_PALERRE
#define DEBUG_PALETTE 0
#endif

#ifndef DEBUG_KEYCODES
#define DEBUG_KEYCODES 0
#endif

extern "C" void PlaySample (const uint8_t* data, uint32_t dataSize, uint32_t hzMode);

static bool     quit;
static uint8_t  palette[16][3];
static uint16_t colors[16];

struct STPaletteState
{
	uint8_t  palette[16][3];
	uint16_t colors[16];
};

void VID_SetDisplayPlanesHint(uint8_t planes)
{
	(void)planes;
}

bool exitGame = false;
DDB_Machine screenMachine = DDB_MACHINE_ATARIST;
bool supportsOpenFileDialog = false;

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

uint16_t* textScreen = 0;

static uint16_t* screen = 0;
static uint16_t* frontBuffer = 0;
static uint16_t* backBuffer = 0;
static uint8_t*  backBufferMemory = 0;
static bool      backBufferEnabled = false;
static STPaletteState frontPaletteState;
static STPaletteState backPaletteState;

static bool      displaySwap = false;
static bool      drawToFront = true;
static bool      textToFront = true;
static bool      paletteToFront = true;

static DMG*       pictureOrigin;
static DMG_Entry* pictureEntry = 0;
static int        pictureIndex = 0;
static uint16_t*  pictureData = 0;
static uint32_t   pictureStride;

#if 0
/* Replacement of special DAAD spanish characters -not used- */
uint8_t DDB_Char2AtariST[16] =
{
	0xA6,	// ª
	0xAD,	// ¡
	0xA8,	// ¿
	0xAE,	// «
	0xAF,	// »
	0xA0,	// á
	0x82,	// é
	0xA1,	// í
	0xA2,	// ó
	0xA3,	// ú
	0xA4,	// ñ
	0xA5,	// Ñ
	0x87,	// ç
	0x80,	// Ç
	0x81,	// ü
	0x9A,	// Ü
};
#endif

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

#define CONSOLE 0x02

static int KeyboardHit()
{
	return Bconstat(CONSOLE) == -1;
}

static int GetKey()
{
	return Bconin(CONSOLE);
}

static uint32_t _getClock(void)
{
	return *((volatile unsigned long *) 0x4baL);
}

static uint32_t _writePaletteRegisters(void)
{
	volatile uint16_t* hwPalette = (volatile uint16_t*)0xFFFF8240L;
	for (int n = 0; n < 16; n++)
		hwPalette[n] = colors[n];
	return 0;
}

clock_t Clock(void)
{
	return Supexec(_getClock);
}

static int GetKeyModifiers()
{
	uint32_t mod = Kbshift(-1);
	uint32_t r = 0;
	if (mod & 0x03) r |= SCR_KEYMOD_SHIFT;
	if (mod & 0x04) r |= SCR_KEYMOD_CTRL;
	if (mod & 0x08) r |= SCR_KEYMOD_ALT;
	return r;
}

static void HideCursor()
{
	asm(".byte 0xA0, 0x0A");
}

static void ShowCursor()
{
	asm(".byte 0xA0, 0x09");
}

static uint16_t ColorValue(uint8_t r, uint8_t g, uint8_t b)
{
	r = ((r >> 5) & 0x07) | ((r & 0x10) >> 1);
	g = ((g >> 5) & 0x07) | ((g & 0x10) >> 1);
	b = ((b >> 5) & 0x07) | ((b & 0x10) >> 1);
	return (uint16_t)b | ((uint16_t)g << 4) | ((uint16_t)r << 8);
}

static void VID_CopyPaletteState(STPaletteState* dst, const STPaletteState* src)
{
	MemCopy(dst, src, sizeof(*dst));
}

static void VID_LoadHardwarePaletteFromState(const STPaletteState* state)
{
	for (int n = 0; n < 16; n++)
	{
		palette[n][0] = state->palette[n][0];
		palette[n][1] = state->palette[n][1];
		palette[n][2] = state->palette[n][2];
		colors[n] = state->colors[n];
	}
}

static void VID_SetPaletteEntry(STPaletteState* state, uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	state->palette[color][0] = r;
	state->palette[color][1] = g;
	state->palette[color][2] = b;
	state->colors[color] = ColorValue(r, g, b);
}

static void VID_SetPaletteEntry16(STPaletteState* state, uint8_t color, uint16_t c)
{
	uint32_t c32 = Pal2RGB(c, false);
	state->palette[color][0] = (c32 >> 16) & 0xFF;
	state->palette[color][1] = (c32 >>  8) & 0xFF;
	state->palette[color][2] = (c32      ) & 0xFF;
	state->colors[color] = c;
}

static void VID_LoadPaletteFromRGB32(STPaletteState* state, const uint32_t* pal)
{
	for (int n = 0; n < 16; n++)
	{
		uint8_t r = (pal[n] >> 16) & 0xFF;
		uint8_t g = (pal[n] >>  8) & 0xFF;
		uint8_t b = (pal[n]      ) & 0xFF;
		VID_SetPaletteEntry(state, (uint8_t)n, r, g, b);
	}
}

static void VID_ApplyPalette(bool waitForVsync)
{
	if (waitForVsync)
		Vsync();

	Supexec(_writePaletteRegisters);
}

static bool VID_PaletteMatches(const uint32_t* pal)
{
	if (pal == 0)
		return false;

	STPaletteState* state = &frontPaletteState;
	if (backBufferEnabled)
		state = paletteToFront ?
			(displaySwap ? &backPaletteState : &frontPaletteState) :
			(displaySwap ? &frontPaletteState : &backPaletteState);

	for (int n = 0; n < 16; n++)
	{
		if (state->palette[n][0] != ((pal[n] >> 16) & 0xFF) ||
			state->palette[n][1] != ((pal[n] >> 8) & 0xFF) ||
			state->palette[n][2] != (pal[n] & 0xFF))
			return false;
	}

	return true;
}

static void VID_SetPaletteInternal(uint32_t* pal, bool waitForVsync)
{
	STPaletteState* visibleState = displaySwap ? &backPaletteState : &frontPaletteState;
	STPaletteState* state = &frontPaletteState;
	if (backBufferEnabled)
		state = paletteToFront ? visibleState : (displaySwap ? &frontPaletteState : &backPaletteState);
	VID_LoadPaletteFromRGB32(state, pal);
	if (state == visibleState)
	{
		VID_LoadHardwarePaletteFromState(state);
		VID_ApplyPalette(waitForVsync);
	}
}

static void VID_BlitPictureTo(uint16_t* dstBase, const uint16_t* srcBase, uint32_t srcStride, int x, int y, int w, int h)
{
	const uint16_t* srcPtr = srcBase;
	uint16_t* dstPtr = dstBase + y * 80 + 4*(x >> 4);

	if (x & 15)
	{
		bool extraByte = (w & 15);
		w >>= 4;
		if (w == 0)
		{
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (const uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				d[1] = s[0];
				d[3] = s[2];
				d[5] = s[4];
				d[7] = s[6];
				dstPtr += 80;
				srcPtr += srcStride;
			}
		}
		else
		{
			w--;
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (const uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				d[1] = s[0];
				d[3] = s[2];
				d[5] = s[4];
				d[7] = s[6];
				d += 8;
				for (int cx = 0; cx < w; cx++)
				{
					d[0] = s[1];
					d[2] = s[3];
					d[4] = s[5];
					d[6] = s[7];
					s += 8;
					d[1] = s[0];
					d[3] = s[2];
					d[5] = s[4];
					d[7] = s[6];
					d += 8;
				}
				d[0] = s[1];
				d[2] = s[3];
				d[4] = s[5];
				d[6] = s[7];
				if (extraByte)
				{
					s += 8;
					d[1] = s[0];
					d[3] = s[2];
					d[5] = s[4];
					d[7] = s[6];
				}

				dstPtr += 80;
				srcPtr += srcStride;
			}
		}
	}
	else
	{
		bool extraByte = (w & 15);
		w >>= 4;
		if (w == 0)
		{
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (const uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				d[0] = s[0];
				d[2] = s[2];
				d[4] = s[4];
				d[6] = s[6];
				dstPtr += 80;
				srcPtr += srcStride;
			}
		}
		else
		{
			uint32_t rowBytes = (uint32_t)w * 8;
			if (!extraByte && rowBytes == 160)
			{
				memcpy(dstPtr, srcPtr, rowBytes * h);
			}
			else
			{
				for (int dy = 0; dy < h; dy++)
				{
					memcpy(dstPtr, srcPtr, rowBytes);
					if (extraByte)
					{
						const uint8_t* s8 = (const uint8_t*)srcPtr + rowBytes;
						uint8_t* d8 = (uint8_t*)dstPtr + rowBytes;
						d8[0] = s8[0];
						d8[2] = s8[2];
						d8[4] = s8[4];
						d8[6] = s8[6];
					}
					dstPtr += 80;
					srcPtr += srcStride;
				}
			}
		}
		}
}

static bool VID_PresentPictureAtomicallyWithTemporaryBuffer(uint32_t* pal, bool paletteUpdated, int x, int y, int w, int h)
{
	if (!drawToFront) {
		// DebugPrintf("Atomic picture present not available: drawing to back buffer\n");
		return false;
	}
	if (!paletteUpdated) {
		// DebugPrintf("Atomic picture present not available: picture contains no palette changes\n");
		return false;
	}
	if (pictureData == 0) {
		// DebugPrintf("Atomic picture present not available: picture data is null\n");
		return false;
	}

	uint8_t* tempBase = DMG_GetTemporaryBufferBase();
	if (tempBase == 0) {
		// DebugPrintf("Atomic picture present not available: no temporary buffer\n");
		return false;
	}
	if (tempBase == 0 || DMG_GetTemporaryBufferSize() < 32000) {
		// DebugPrintf("Atomic picture present not available: buffer too small (%u bytes)\n", DMG_GetTemporaryBufferSize());
		return false;
	}

	uint16_t* visibleBuffer = displaySwap ? backBuffer : frontBuffer;
	STPaletteState* visiblePalette = displaySwap ? &backPaletteState : &frontPaletteState;
	uint16_t* tempScreen = (uint16_t*)tempBase;
	const uint16_t* sourcePicture = pictureData;
	uint32_t sourceStride = pictureStride;

	if (DMG_IsTemporaryBufferPointer(pictureData)) {
		// DebugPrintf("Atomic picture present not available: picture comes from temporary buffer\n", DMG_GetTemporaryBufferSize());
		return false;
	}

	memcpy(tempScreen, visibleBuffer, 32000);
	VID_BlitPictureTo(tempScreen, sourcePicture, sourceStride, x, y, w, h);
	VID_LoadPaletteFromRGB32(visiblePalette, pal);
	VID_LoadHardwarePaletteFromState(visiblePalette);

	// Setscreen becomes active on the next VBL, so switch first, wait once, then latch the palette.
	Setscreen(-1, tempScreen, -1);
	Vsync();
	VID_ApplyPalette(false);

	memcpy(visibleBuffer, tempScreen, 32000);
	Setscreen(-1, visibleBuffer, -1);
	return true;
}

void VID_WaitForKey()
{
	while (KeyboardHit())
		GetKey();
	while (!KeyboardHit());
}

bool VID_AnyKey ()
{
	return KeyboardHit();
}

bool VID_LoadDataFile (const char* fileName)
{
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;
	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);

	if (dmg != 0)
	{
		DMG_Close(dmg);
		dmg = 0;
	}

	dmg = DMG_Open(ChangeExtension(fileName, ".dat"), true);
	if (dmg == 0)
		dmg = DMG_Open(ChangeExtension(fileName, ".ega"), true);
	if (dmg == 0)
		dmg = DMG_Open(ChangeExtension(fileName, ".cga"), true);
	if (dmg == 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	if (!LoadSINTACFont(ChangeExtension(fileName, ".FNT")) &&
		!LoadSINTACFont(ChangeExtension(fileName, ".fnt")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		// Keep the default fixed-width charset restored above.
	}

	uint32_t freeMemory = (uint32_t)OSGetFree();
	if (freeMemory == 0)
		freeMemory = Malloc(-1);
	uint32_t datSize = File_GetSize(dmg->file);

	#if HAS_XMSG
	if (xmsgFilePresent)
	{
		if (freeMemory > datSize + 65536)
			DDB_InitializeXMessageCache(16384);
		else
			DDB_InitializeXMessageCache(4096);
	}
	#endif

	if (OSGetFree() != 0)
	{
		// After interpreter creation there are no further permanent allocations,
		// so on ST we can give the image cache all arena space left after file cache setup.
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		freeMemory = (uint32_t)GetMaxAllocatableBlockSize();
		DMG_SetupImageCache(dmg, freeMemory);
	}
	else if (freeMemory > datSize + 32768)			// Everything fits
	{
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		freeMemory = Malloc(-1);
		if (freeMemory >= 32768)
			DMG_SetupImageCache(dmg, freeMemory);
	}
	else if (freeMemory > datSize)				// DAT barely fits
	{
		DMG_SetupImageCache(dmg, 32768);
		freeMemory = Malloc(-1);
		DMG_SetupFileCache(dmg, freeMemory, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x40000)				// >256K free
	{
		DMG_SetupImageCache(dmg, 0x18000);	// 96K cache
		freeMemory = Malloc(-1);
		DMG_SetupFileCache(dmg, freeMemory, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x20000)				// >128K free
	{
		DMG_SetupImageCache(dmg, 0xC000);	// 48K cache
		freeMemory = Malloc(-1);
		DMG_SetupFileCache(dmg, freeMemory, VID_ShowProgressBar);
	}
	else 
	{
		DMG_SetupImageCache(dmg, 0x8000);	// 32K cache
		freeMemory = Malloc(-1);
		DMG_SetupFileCache(dmg, freeMemory, VID_ShowProgressBar);
	}

	return true;
}

void VID_Clear (int x, int y, int w, int h, uint8_t color)
{
	uint16_t* scr = screen + 80*y + 4*(x >> 4);

	uint16_t p0 = (color & 0x01) ? 0xFFFF : 0x0000;
	uint16_t p1 = (color & 0x02) ? 0xFFFF : 0x0000;
	uint16_t p2 = (color & 0x04) ? 0xFFFF : 0x0000;
	uint16_t p3 = (color & 0x08) ? 0xFFFF : 0x0000;

	int skip = x & 15;
	if (skip != 0)
	{
		uint16_t pix = (0x10000 >> (x & 15))-1;
		if (w < 16-skip)
			pix &= ~( (0x10000 >> ((x+w) & 15))-1 );
		uint16_t mask = ~pix;
		uint16_t* out = scr;
		for (int n = 0; n < h; n++)
		{
			out[0] = (out[0] & mask) | (pix & p0);
			out[1] = (out[1] & mask) | (pix & p1);
			out[2] = (out[2] & mask) | (pix & p2);
			out[3] = (out[3] & mask) | (pix & p3);
			out += 80;
		}

		int inc = 16-skip;
		x += inc;
		w -= inc;
		scr += 4;
		if (w < 1) return;
	}

	int r = w >> 4;
	int ch = h;
	while (ch > 0)
	{
		uint16_t* out = scr;
		for (int n = 0; n < r; n++)
		{
			out[0] = p0;
			out[1] = p1;
			out[2] = p2;
			out[3] = p3;
			out += 4;
		}
		scr += 80;
		ch--;
	}

	if (w & 15)
	{
		uint16_t pix  = ~( (0x10000 >> (w & 15))-1 );
		uint16_t* out = scr + 4*r - 80*h;
		uint16_t mask = ~pix;
		for (int n = 0; n < h; n++)
		{
			out[0] = (out[0] & mask) | (pix & p0);
			out[1] = (out[1] & mask) | (pix & p1);
			out[2] = (out[2] & mask) | (pix & p2);
			out[3] = (out[3] & mask) | (pix & p3);
			out += 80;
		}
	}
}

void VID_ClearBuffer (bool front)
{
	if (!front && !backBufferEnabled)
		return;
		
	uint16_t* ptr = frontBuffer;
	if (backBufferEnabled)
		ptr = front ?
			(displaySwap ? backBuffer : frontBuffer) :
			(displaySwap ? frontBuffer : backBuffer);
	MemClear(ptr, 32000);
}

void VID_ClearAllPlanes (int x, int y, int w, int h, uint8_t color)
{
	VID_Clear(x, y, w, h, color);
}

void VID_Finish ()
{
	VID_FinishTextDraw();
	ShowCursor();

	if (backBufferMemory != 0)
	{
		Free(backBufferMemory);
		backBufferMemory = 0;
		backBuffer = 0;
	}
}

void VID_SetPalette (uint32_t* pal)
{
	VID_SetPaletteInternal(pal, true);

#if DEBUG_PALETTE
	for (int color = 0; color < 16; color++)
	{
		char buf[8];
		STPaletteState* state = &frontPaletteState;
		if (backBufferEnabled)
			state = paletteToFront ?
				(displaySwap ? &backPaletteState : &frontPaletteState) :
				(displaySwap ? &frontPaletteState : &backPaletteState);
		uint16_t v = state->colors[color];
		sprintf(buf, "%04X", v);
		VID_DrawCharacter(6 + 40*(color & 3), 8 + 8*(color >> 2), ' ', 15, color);
		for (int n = 0; n < 4; n++)
			VID_DrawCharacter(14 + 40*(color & 3)+6*n, 8 + 8*(color >> 2), buf[n], 15, 0);
	}
#endif
}

void VID_SetPaletteColor16 (uint8_t color, uint16_t c)
{
	STPaletteState* visibleState = displaySwap ? &backPaletteState : &frontPaletteState;
	STPaletteState* state = &frontPaletteState;
	if (backBufferEnabled)
		state = paletteToFront ? visibleState : (displaySwap ? &frontPaletteState : &backPaletteState);
	VID_SetPaletteEntry16(state, color, c);
	if (state == visibleState)
	{
		VID_LoadHardwarePaletteFromState(state);
		Setcolor(color, c);
	}
}

void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t v = ColorValue(r, g, b);
	STPaletteState* visibleState = displaySwap ? &backPaletteState : &frontPaletteState;
	STPaletteState* state = &frontPaletteState;
	if (backBufferEnabled)
		state = paletteToFront ? visibleState : (displaySwap ? &frontPaletteState : &backPaletteState);
	VID_SetPaletteEntry(state, color, r, g, b);

#if DEBUG_PALETTE
	char buf[8];
	sprintf(buf, "%04X", v);
	VID_DrawCharacter(6 + 40*(color & 3), 8 + 8*(color >> 2), ' ', 15, color);
	for (int n = 0; n < 4; n++)
		VID_DrawCharacter(14 + 40*(color & 3)+6*n, 8 + 8*(color >> 2), buf[n], 15, 0);
#endif

	if (state == visibleState)
	{
		VID_LoadHardwarePaletteFromState(state);
		Setcolor(color, v);
	}
}

void VID_ActivatePalette()
{
	STPaletteState* visibleState = displaySwap ? &backPaletteState : &frontPaletteState;
	VID_LoadHardwarePaletteFromState(visibleState);
	VID_ApplyPalette(true);
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	if (target == DDB_MACHINE_ATARIST)
	{
		uint16_t palette[16];
		File* file = File_Open(fileName);
		if (!file) return false;

		if (fadeIn)
		{
			for (int n = 0; n < 16; n++)
				VID_SetPaletteColor16(n, 0);
			Vsync();
		}

		File_Seek(file, 2);
		File_Read(file, palette, 32);
		File_Read(file, screen, 32000);
		File_Close(file);

		if (fadeIn)
		{
			for (int n = 0; n < 16; n++)
				VID_SetPaletteColor16(n, palette[n]);
		}
		return true;
	}

	uint32_t palette[16];
	size_t bufferSize = 32768;

	DMG_ReserveTemporaryBuffer(bufferSize);
	uint8_t* buffer = DMG_GetTemporaryBufferBase();

	if (SCR_GetScreen(fileName, target, buffer + 160, bufferSize - 160,
	                  buffer, 320, 200, palette))
	{
		uint32_t *out = (uint32_t*)screen;
		uint8_t *in = buffer;

		if (fadeIn)
		{
			for (int n = 0; n < 16; n++)
				VID_SetPaletteColor(n, 0, 0, 0);
			Vsync();
		}

		for (int x = 0; x < 320*200; x += 16)
		{
			uint32_t p0 = 0;
			uint32_t p1 = 0;
			uint32_t mask0 = 0x00008000;
			uint32_t mask1 = 0x80000000;

			do
			{
				const uint8_t c = *in++;
				if (c & 0x01) p0 |= mask1;
				if (c & 0x02) p0 |= mask0;
				if (c & 0x04) p1 |= mask1;
				if (c & 0x08) p1 |= mask0;
				mask0 >>= 1;
				mask1 >>= 1;
			}
			while (mask0);

			*out++ = p0;
			*out++ = p1;
		}

		if (fadeIn)
		{
			Vsync();
			for (int n = 0; n < 16; n++)
			{
				uint8_t r = (palette[n] >> 16) & 0xFF;
				uint8_t g = (palette[n] >>  8) & 0xFF;
				uint8_t b = (palette[n]      ) & 0xFF;
				VID_SetPaletteColor(n, r, g, b);
			}
		}

		return true;
	}
	return false;
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* mod)
{
	uint32_t in = GetKey();
	if (key) *key = in;
	if (ext) *ext = (in >> 16);
	if (mod) *mod = GetKeyModifiers();

	if (interpreter && (in >> 16) == 0x44) // F10
	{
		if (++interpreter->keyClick == 3)
			interpreter->keyClick = 0;
	}

#if DEBUG_KEYCODES
	char buf[16];
	sprintf(buf, "%08X %04X", in, GetKeyModifiers());
	for (int n =0; n < 13; n++)
		VID_DrawCharacter(6*n, 0, buf[n], 0, 15);
#endif

#if DEBUG_MEMORY
	if (ext && *ext == 0x3B)
	{
		char buf[64];
		void *allocs[256];
		int n;
		for (n = 0; n < 256; n++)
		{
			allocs[n] = AllocateBlock("MEMTEST", 1024);
			if (allocs[n] == 0)
				break;
		}
		for (int i = 0; i < n; i++)
			Free(allocs[i]);
		buf[0] = ' ';
		LongToChar(n, buf+1, 10);
		strcat(buf, "K free ");
		for (int n = 0; buf[n]; n++)
			VID_DrawCharacter(8+6*n, 8, buf[n], 0, 15);
	
		for (n = 1 ; n < 256; n++)
		{
			void* ptr = AllocateBlock("MEMTEST", n * 1024);
			if (ptr == 0) break;
			Free(ptr);
		}
		buf[0] = ' ';
		LongToChar(n, buf+1, 10);
		strcat(buf, "K maxb ");
		for (int n = 0; buf[n]; n++)
			VID_DrawCharacter(8+6*n, 16, buf[n], 15, 0);

		if (key) *key = 0;
		if (ext) *ext = 0;
	}
#endif

#if DEBUG_ALLOCS
	if (ext && *ext == 0x3C)	// F2
	{
		DebugPrintf("Arena total=%lu bytes, arena free=%lu bytes, OS free=%lu bytes\n",
			(unsigned long)OSGetArenaSize(),
			(unsigned long)OSGetFree(),
			(unsigned long)Malloc(-1));
		DumpMemory(0, Malloc(-1));
	}
#endif
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = Clock() * 5;
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	STPaletteState* state = &frontPaletteState;
	if (backBufferEnabled)
		state = paletteToFront ?
			(displaySwap ? &backPaletteState : &frontPaletteState) :
			(displaySwap ? &frontPaletteState : &backPaletteState);
	if (r) *r = state->palette[color][0];
	if (g) *g = state->palette[color][1];
	if (b) *b = state->palette[color][2];
}

uint16_t VID_GetPaletteSize()
{
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
	{
		VID_ShowError("Driver has no DMG");
		return;
	}

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == 0 || entry->type != DMGEntry_Image)
		return;

	pictureOrigin = dmg;
	pictureEntry  = entry;
	pictureIndex  = picno;
	pictureStride = 4 * ((entry->width+15) / 16);
	pictureData   = (uint16_t*) DMG_GetEntryDataNative(dmg, picno);
	if (pictureData == 0)
	{
		VID_ShowError(DMG_GetErrorString());
		pictureEntry = 0;
		pictureOrigin = 0;
	}
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode mode)
{
	DMG_Entry* entry = pictureEntry;
	if (entry == 0)
		return;
	bool paletteUpdated = false;

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

	if (h > entry->height)
		h = entry->height;
	if (w > entry->width)
		w = entry->width;

	uint32_t* palette = DMG_GetEntryPalette(dmg, pictureIndex);
	switch (mode)
	{
		default:
		case ScreenMode_VGA16:
			if (entry->flags & DMG_FLAG_FIXED)
			{
				// TODO: This is a hack to fix the palette for the old version of the game
				if (dmg->version == DMG_Version1)
					entry->RGB32Palette[15] = 0xFFFFFFFF;
				paletteUpdated = !VID_PaletteMatches(palette);
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			if (entry->flags & DMG_FLAG_FIXED)
			{
				palette = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
				paletteUpdated = !VID_PaletteMatches(palette);
			}
			break;
	}

	if (VID_PresentPictureAtomicallyWithTemporaryBuffer(palette, paletteUpdated, x, y, w, h))
		return;

	if (drawToFront)
	{
		Vsync();
		if (paletteUpdated)
			VID_SetPaletteInternal(palette, false);
	}
	else if (paletteUpdated)
		VID_SetPaletteInternal(palette, false);

	VID_BlitPictureTo(screen, pictureData, pictureStride, x, y, w, h);
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	if (lines <= 0 || w <= 0 || h <= 0)
		return;

	if (lines >= h)
	{
		VID_Clear(x, y, w, h, paper);
		return;
	}

	int clearX = x;
	int clearY = y + h - lines;
	int clearW = w;
	int scrollHeight = h - lines;
	uint16_t* scr = screen + 80*y + 4*(x >> 4);

	// TODO: needs testing for unaligned cases

	int skip = x & 15;
	if (skip != 0)
	{
		uint16_t pix = (0x10000 >> (x & 15))-1;
		if (w < 16-skip)
			pix &= ~( (0x10000 >> ((x+w) & 15))-1 );
		uint16_t mask = ~pix;
		int32_t  max  = scrollHeight*80;
		uint16_t* inp = scr + 80*lines;
		for (int32_t off = 0; off < max; off += 80)
		{
			uint16_t* out = scr + off;
			uint16_t* in = inp + off;
			out[0] = (out[0] & mask) | (in[0] & pix);
			out[1] = (out[1] & mask) | (in[1] & pix);
			out[2] = (out[2] & mask) | (in[2] & pix);
			out[3] = (out[3] & mask) | (in[3] & pix);
		}
		
		int inc = 16-skip;
		x += inc;
		w -= inc;
		scr += 4;
		if (w <= 0)
		{
			VID_Clear(clearX, clearY, clearW, lines, paper);
			return;
		}
	}

	uint16_t* ptr = scr;
	uint16_t* inp = scr + 80*lines;
	int       copy = 8*(w >> 4);
	int       n = scrollHeight;
	while (n > 0)
	{
		memcpy(ptr, inp, copy);
		ptr += 80;
		inp += 80;
		n--;
	}

	if (w & 15)
	{
		uint16_t pix  = ~( (0x10000 >> (w & 15))-1 );
		uint16_t* out = scr + (copy >> 1);
		uint16_t* inp = scr + 80*lines + (copy >> 1);
		uint16_t mask = ~pix;
		for (int row = 0; row < scrollHeight; row++)
		{
			out[0] = (out[0] & mask) | (inp[0] & pix);
			out[1] = (out[1] & mask) | (inp[1] & pix);
			out[2] = (out[2] & mask) | (inp[2] & pix);
			out[3] = (out[3] & mask) | (inp[3] & pix);
			inp += 80;
			out += 80;
		}
	}

	VID_Clear(clearX, clearY, clearW, lines, paper);
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
	uint8_t* audioData = DMG_GetEntryDataNative(dmg, no);
	if (audioData == 0)
		return;

	if (entry->x > 5 || entry->x < 0)
		return;
	PlaySample(audioData, entry->length, entry->x);

	if (duration != NULL)
	{
		int sampleHz = 30000;
		switch (entry->x)
		{
			case DMG_5KHZ:   sampleHz = 5000; break;
			case DMG_7KHZ:   sampleHz = 7000; break;
			case DMG_9_5KHZ: sampleHz = 9500; break;
			case DMG_15KHZ:  sampleHz = 15000; break;
			case DMG_20KHZ:  sampleHz = 20000; break;
			case DMG_30KHZ:  sampleHz = 30000; break;
		}
		*duration = entry->length * 1000 / sampleHz;
	}
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int volume)
{
	int hzMode = DMG_5KHZ;

	     if (hz <  5000) hzMode = DMG_5KHZ;
	else if (hz <  8500) hzMode = DMG_7KHZ;
	else if (hz < 12250) hzMode = DMG_9_5KHZ;
	else if (hz < 17500) hzMode = DMG_15KHZ;
	else if (hz < 25000) hzMode = DMG_20KHZ;
	else hzMode = DMG_30KHZ;
	
	PlaySample((uint8_t*)buffer, samples, hzMode);
}

void VID_Quit ()
{
	quit = true;
}

void VID_UpdateScreenPointers()
{
	if (!backBufferEnabled)
	{
		screen = frontBuffer;
		textScreen = frontBuffer;
		return;
	}

	screen = (drawToFront ^ displaySwap) ? frontBuffer : backBuffer;
	textScreen = (textToFront ^ displaySwap) ? frontBuffer : backBuffer;
}

void VID_SetTextInputMode (bool enabled)
{
	// Not supported
}

void VID_SwapScreen ()
{
	if (!backBufferEnabled)
		return;

	displaySwap = !displaySwap;
	VID_UpdateScreenPointers();

	uint16_t* visibleBuffer = displaySwap ? backBuffer : frontBuffer;
	STPaletteState* visibleState = displaySwap ? &backPaletteState : &frontPaletteState;
	VID_LoadHardwarePaletteFromState(visibleState);
	Setscreen(-1, visibleBuffer, -1);
	VID_ApplyPalette(true);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	if (!backBufferEnabled)
		return;
		
	if (op == SCR_OP_DRAWTEXT)
		textToFront = front;
	else if (op == SCR_OP_DRAWPICTURE)
	{
		drawToFront = front;
		paletteToFront = front;
	}
	VID_UpdateScreenPointers();
}

void VID_RestoreScreen ()
{
	if (!backBufferEnabled)
		return;

	VID_SwapScreen();

	uint16_t* front = displaySwap ? backBuffer : frontBuffer;
	uint16_t* back = displaySwap ? frontBuffer : backBuffer;
	STPaletteState* frontState = displaySwap ? &backPaletteState : &frontPaletteState;
	STPaletteState* backState = displaySwap ? &frontPaletteState : &backPaletteState;
	memcpy(back, front, 32000);
	VID_CopyPaletteState(backState, frontState);
}

void VID_SaveScreen ()
{
	if (!backBufferEnabled)
		return;
		
	uint16_t* front = displaySwap ? backBuffer : frontBuffer;
	uint16_t* back  = displaySwap ? frontBuffer : backBuffer;
	STPaletteState* frontState = displaySwap ? &backPaletteState : &frontPaletteState;
	STPaletteState* backState = displaySwap ? &frontPaletteState : &backPaletteState;
	memcpy(back, front, 32000);
	VID_CopyPaletteState(backState, frontState);
}

void VID_MainLoop (DDB_Interpreter* i, void (*callback)(int elapsed))
{
	interpreter = i;
	
	while (!quit)
	{
		callback(0);
	}

	quit = false;
}

void VID_EnableBackBuffer()
{
	backBufferEnabled = true;

	if (backBuffer == 0)
	{
		backBufferMemory = Allocate<uint8_t>("ST Back Buffer", 32768, false);
		if (backBufferMemory == 0)
		{
			backBufferEnabled = false;
			return;
		}

		backBuffer = (uint16_t*)((((unsigned long)backBufferMemory) + 255u) & ~255u);
		memset(backBuffer, 0, 32000);
	}

	VID_CopyPaletteState(&backPaletteState, &frontPaletteState);
}

bool VID_IsBackBufferEnabled()
{
	return backBufferEnabled;
}

bool VID_Initialize(DDB_Machine machine, DDB_Version version, DDB_ScreenMode screenMode)
{
	(void)machine;
	(void)version;
	(void)screenMode;
	screenWidth  = 320;
	screenHeight = 200;
	lineHeight   = 8;
	columnWidth  = 6;
	screenMachine = DDB_MACHINE_ATARIST;
	screenWidth  = 320;
	screenHeight = 200;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);
	displaySwap = false;
	drawToFront = true;
	textToFront = true;
	paletteToFront = true;
	MemClear(&frontPaletteState, sizeof(frontPaletteState));
	MemClear(&backPaletteState, sizeof(backPaletteState));

	frontBuffer = (uint16_t*)Physbase();
	memset(frontBuffer, 0, 32000);

	if (backBufferEnabled)
	{
		if (backBufferMemory == 0)
			backBufferMemory = Allocate<uint8_t>("ST Back Buffer", 32768, false);
		if (backBufferMemory == 0)
			return false;
		backBuffer = (uint16_t*)((((unsigned long)backBufferMemory) + 255u) & ~255u);
		memset(backBuffer, 0, 32000);
		VID_CopyPaletteState(&backPaletteState, &frontPaletteState);
	}

	VID_UpdateScreenPointers();
	if (!VID_InitializeTextDraw())
		return false;
	VID_SetDefaultPalette();
	VID_CopyPaletteState(&backPaletteState, &frontPaletteState);

	HideCursor();
	VID_Clear(0, 0, 320, 200, 0);
	return true;
}

void VID_VSync()
{
	Vsync();
}

#endif
