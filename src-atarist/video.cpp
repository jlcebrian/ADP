#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_pal.h>
#include <ddb_vid.h>
#include <dmg.h>
#include <os_char.h>
#include <os_mem.h>

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

static bool     quit;
static uint8_t  palette[16][3];
static uint16_t colors[16];

bool exitGame = false;
bool supportsOpenFileDialog = false;

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

static uint16_t* screen = 0;
static uint16_t* textScreen = 0;
static uint16_t* frontBuffer = 0;
static uint16_t* backBuffer = 0;
static uint8_t   backBufferMemory[32768];

static bool      displaySwap = false;
static bool      drawToFront = true;
static bool      textToFront = true;

static char newFileName[MAX_PATH];

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

static const char* ChangeExtension(const char* fileName, const char* extension)
{
	strncpy(newFileName, fileName, MAX_PATH);
	newFileName[MAX_PATH-1] = 0;
	char* ptr = strrchr(newFileName, '.');
	if (ptr == 0)
		ptr = newFileName + strlen(newFileName);
	strcpy(ptr, extension);
	return newFileName;
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

void VID_WaitForKey()
{
	while (!KeyboardHit());
	while (KeyboardHit())
		GetKey();
}

bool VID_AnyKey ()
{
	return KeyboardHit();
}

bool VID_LoadDataFile (const char* fileName)
{
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
	if (!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
	}

	uint32_t freeMemory = Malloc(-1);
	uint32_t datSize = File_GetSize(dmg->file);

	if (freeMemory > datSize + 32768)			// Everything fits
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
	uint16_t* ptr = front ^ displaySwap ? frontBuffer : backBuffer;
	MemClear(ptr, 32000);
}

void VID_Finish ()
{
	ShowCursor();
}

void VID_SetPalette (uint32_t* pal)
{
	for (int n = 0; n < 16; n++)
	{
		uint8_t r = (pal[n] >> 16) & 0xFF;
		uint8_t g = (pal[n] >>  8) & 0xFF;
		uint8_t b = (pal[n]      ) & 0xFF;
		palette[n][0] = r;
		palette[n][1] = g;
		palette[n][2] = b;
		colors[n] = ColorValue(r, g, b);
	}
	VID_UpdateInkMap();

	Vsync();
	for (int n = 0; n < 16; n++)
		Setcolor(n, colors[n]);

#if DEBUG_PALETTE
	for (int color = 0; color < 16; color++)
	{
		char buf[8];
		uint16_t v = colors[color];
		sprintf(buf, "%04X", v);
		VID_DrawCharacter(6 + 40*(color & 3), 8 + 8*(color >> 2), ' ', 15, color);
		for (int n = 0; n < 4; n++)
			VID_DrawCharacter(14 + 40*(color & 3)+6*n, 8 + 8*(color >> 2), buf[n], 15, 0);
	}
#endif
}

void VID_SetPaletteColor16 (uint8_t color, uint16_t c)
{
	uint32_t c32 = Pal2RGB(c, false);
	palette[color][0] = (c32 >> 16) & 0xFF;
	palette[color][1] = (c32 >>  8) & 0xFF;
	palette[color][2] = (c32      ) & 0xFF;
	colors[color] = c;
	Setcolor(color, c);
}

void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t v = ColorValue(r, g, b);
	
	palette[color][0] = r;
	palette[color][1] = g;
	palette[color][2] = b;
	colors[color] = v;

#if DEBUG_PALETTE
	char buf[8];
	sprintf(buf, "%04X", v);
	VID_DrawCharacter(6 + 40*(color & 3), 8 + 8*(color >> 2), ' ', 15, color);
	for (int n = 0; n < 4; n++)
		VID_DrawCharacter(14 + 40*(color & 3)+6*n, 8 + 8*(color >> 2), buf[n], 15, 0);
#endif

	Setcolor(color, v);
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target)
{
	// Special case: handle ST directly
	if (target == DDB_MACHINE_ATARIST)
	{
		uint16_t palette[16];
		File* file = File_Open(fileName);
		if (!file) return false;
		for (int n = 0; n < 16; n++) 
			VID_SetPaletteColor16(n, 0);
		Vsync();
		File_Seek(file, 2);
		File_Read(file, palette, 32);
		File_Read(file, screen, 32000);
		File_Close(file);
		for (int n = 0; n < 16; n++) 
			VID_SetPaletteColor16(n, palette[n]);
		return true;
	}

	uint32_t palette[16];
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", 320*200);
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	size_t bufferSize = 32768;

	if (SCR_GetScreen(fileName, target, buffer, bufferSize, 
	                  output, 320, 200, palette))
	{
		uint32_t *out = (uint32_t*)screen;
		uint8_t *in = output;

		for (int n = 0; n < 16; n++) 
			VID_SetPaletteColor(n, 0, 0, 0);
		Vsync();

		for (int x = 0; x < 320*200; x += 16)
		{
			uint32_t p0 = 0;
			uint32_t p1 = 0;
			uint32_t mask0 = 0x00008000;
			uint32_t mask1 = 0x80000000;

			do
			{
				const uint8_t  c = *in++;
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
		
		Vsync();
		for (int n = 0; n < 16; n++) 
		{
			uint8_t r = (palette[n] >> 16) & 0xFF;
			uint8_t g = (palette[n] >>  8) & 0xFF;
			uint8_t b = (palette[n]      ) & 0xFF;
			VID_SetPaletteColor(n, r, g, b);
		}
		
		Free(buffer);
		Free(output);
		return true;
	}

	Free(buffer);
	Free(output);
	return false;
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	uint16_t ink0   = (ink   & 0x01) ? 0xFFFF : 0x0000;
	uint16_t ink1   = (ink   & 0x02) ? 0xFFFF : 0x0000;
	uint16_t ink2   = (ink   & 0x04) ? 0xFFFF : 0x0000;
	uint16_t ink3   = (ink   & 0x08) ? 0xFFFF : 0x0000;

	uint8_t* data = charset + 8*ch;
	uint16_t* out = textScreen + 80*y + 4*(x >> 4);
	uint8_t shift = x & 0x0F;
	uint16_t mask = ~(0xFC00 >> shift);

	if (paper != 255)
	{
		uint16_t paper0 = (paper & 0x01) ? 0xFFFF : 0x0000;
		uint16_t paper1 = (paper & 0x02) ? 0xFFFF : 0x0000;
		uint16_t paper2 = (paper & 0x04) ? 0xFFFF : 0x0000;
		uint16_t paper3 = (paper & 0x08) ? 0xFFFF : 0x0000;

		for (int row = 0; row < 8; row++)
		{
			uint16_t pix = (*data << 8) >> shift;
			uint16_t neg = ~mask & ~pix;
			out[0] = (out[0] & mask) | (pix & ink0) | (neg & paper0);
			out[1] = (out[1] & mask) | (pix & ink1) | (neg & paper1);
			out[2] = (out[2] & mask) | (pix & ink2) | (neg & paper2);
			out[3] = (out[3] & mask) | (pix & ink3) | (neg & paper3);
			out += 80;
			data++;
		}
		if (shift > 10)
		{
			data -= 8;
			shift = 24-shift;
			out  -= 636;
			mask  = ~(0x00FC << shift);
	
			for (int row = 0; row < 8; row++)
			{
				uint16_t pix = *data << shift;
				uint16_t neg = ~mask & ~pix;
				out[0] = (out[0] & mask) | (pix & ink0) | (neg & paper0);
				out[1] = (out[1] & mask) | (pix & ink1) | (neg & paper1);
				out[2] = (out[2] & mask) | (pix & ink2) | (neg & paper2);
				out[3] = (out[3] & mask) | (pix & ink3) | (neg & paper3);
				out += 80;
				data++;
			}
		}
	}
	else
	{
		for (int row = 0; row < 8; row++)
		{
			uint16_t pix = (*data << 8) >> shift;
			out[0] = (out[0] & ~pix) | (pix & ink0);
			out[1] = (out[1] & ~pix) | (pix & ink1);
			out[2] = (out[2] & ~pix) | (pix & ink2);
			out[3] = (out[3] & ~pix) | (pix & ink3);
			out += 80;
			data++;
		}
		if (shift > 10)
		{
			data -= 8;
			shift = 24-shift;
			out  -= 636;
			mask  = ~(0x00FC << shift);
	
			for (int row = 0; row < 8; row++)
			{
				uint16_t pix = *data << shift;
				out[0] = (out[0] & ~pix) | (pix & ink0);
				out[1] = (out[1] & ~pix) | (pix & ink1);
				out[2] = (out[2] & ~pix) | (pix & ink2);
				out[3] = (out[3] & ~pix) | (pix & ink3);
				out += 80;
				data++;
			}
		}
	}
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* mod)
{
	uint32_t in = GetKey();
	if (key) *key = in;
	if (ext) *ext = (in >> 16);
	if (mod) *mod = GetKeyModifiers();

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
		DumpMemory(Malloc(-1));
#endif
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = Clock() * 5;
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	if (r) *r = palette[color][0];
	if (g) *g = palette[color][1];
	if (b) *b = palette[color][2];
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
			*fixed = pictureEntry->fixed;
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
	pictureData   = (uint16_t*) DMG_GetEntryDataPlanar(dmg, picno);
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

	uint32_t* palette = DMG_GetEntryPalette(dmg, pictureIndex, ImageMode_RGBA32);
	switch (mode)
	{
		default:
		case ScreenMode_VGA16:
			if (entry->fixed && drawToFront)
			{
				// TODO: This is a hack to fix the palette for the old version of the game
				if (dmg->version == DMG_Version1)
					entry->RGB32Palette[15] = 0xFFFFFFFF;
				VID_SetPalette(palette);
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			VID_SetPalette( entry->CGAMode == CGA_Red ? CGAPaletteRed : CGAPaletteCyan);
			break;
	}

	uint16_t* srcPtr = pictureData;
	uint16_t* dstPtr = screen + y * 80 + 4*(x >> 4);
	
	if (x & 15)
	{
		bool extraByte = (w & 15);
		w >>= 4;
		if (w == 0)
		{
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				d[1] = s[0];
				d[3] = s[2];
				d[5] = s[4];
				d[7] = s[6];				
				dstPtr += 80;
				srcPtr += pictureStride;
			}
		}
		else
		{
			w--;
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (uint8_t*)srcPtr;
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
				srcPtr += pictureStride;
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
				const uint8_t* s = (uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				d[0] = s[0];
				d[2] = s[2];
				d[4] = s[4];
				d[6] = s[6];				
				dstPtr += 80;
				srcPtr += pictureStride;
			}
		}
		else
		{
			for (int dy = 0; dy < h; dy++)
			{
				uint32_t* d = (uint32_t*)dstPtr;
				const uint32_t* s = (uint32_t*)srcPtr;
				for (int cx = 0; cx < w; cx++)
				{
					*d++ = *s++;
					*d++ = *s++;
				}
				if (extraByte)
				{
					const uint8_t* s8 = (uint8_t*)s;
					uint8_t* d8 = (uint8_t*)d;
					d8[0] = s8[0];
					d8[2] = d8[2];
					d8[4] = s8[4];
					d8[6] = s8[6];
				}
				dstPtr += 80;
				srcPtr += pictureStride;
			}
		}
	}
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	uint16_t* scr = screen + 80*y + 4*(x >> 4);

	h -= lines;

	// TODO: needs testing for unaligned cases

	int skip = x & 15;
	if (skip != 0)
	{
		uint16_t pix = (0x10000 >> (x & 15))-1;
		if (w < 16-skip)
			pix &= ~( (0x10000 >> ((x+w) & 15))-1 );
		uint16_t mask = ~pix;
		int32_t  max  = h*80;
		uint16_t* inp = scr + 80*lines;
		for (int32_t off = 0; off < max; off += 80)
		{
			scr[off] = (scr[off] & mask) | (inp[off] & pix);
		}
		
		int inc = 16-skip;
		x += inc;
		w -= inc;
		scr += 4;
	}

	uint16_t* ptr = scr;
	uint16_t* inp = scr + 80*lines;
	int       copy = 8*(w >> 4);
	int       n = h;
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
		uint16_t* inp = scr + 80*lines;
		uint16_t mask = ~pix;
		do
		{
			*out = (*out & mask) | (*inp & pix);
			inp += 80;
			out += 80;
		}
		while (h--);
	}

	VID_Clear(x, y+h, w, lines, paper);
}

void VID_OpenFileDialog (bool existing, char* filename, size_t bufferSize)
{
	// Not supported
}

void VID_PlaySample (uint8_t no, int* duration)
{
	// TODO
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int volume)
{
	// TODO
}

void VID_Quit ()
{
	quit = true;
}

void VID_UpdateScreenPointers()
{
	screen = (drawToFront ^ displaySwap) ? frontBuffer : backBuffer;
	textScreen = (textToFront ^ displaySwap)  ? frontBuffer : backBuffer;
}

void VID_SetTextInputMode (bool enabled)
{
	// Not supported
}

void VID_SwapScreen ()
{
	displaySwap = !displaySwap;
	VID_UpdateScreenPointers();
	Setscreen(-1, displaySwap ? backBuffer : frontBuffer, 0);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	if (op == SCR_OP_DRAWTEXT)
		textToFront = front;
	else if (op == SCR_OP_DRAWPICTURE)
		drawToFront = front;
	VID_UpdateScreenPointers();
}

void VID_RestoreScreen ()
{
	uint16_t* front = displaySwap ? backBuffer : frontBuffer;
	uint16_t* back  = displaySwap ? frontBuffer : backBuffer;
	memcpy(front, back, 32000);
}

void VID_SaveScreen ()
{
	uint16_t* front = displaySwap ? backBuffer : frontBuffer;
	uint16_t* back  = displaySwap ? frontBuffer : backBuffer;
	memcpy(back, front, 32000);
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

bool VID_Initialize()
{
	screenWidth  = 320;
	screenHeight = 200;
	lineHeight   = 8;
	columnWidth  = 6;
	screenWidth  = 320;
	screenHeight = 200;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);

	frontBuffer = (uint16_t*)Physbase();
	backBuffer = (uint16_t*) (((uint32_t)backBufferMemory + 255) & ~255);

	memset(frontBuffer, 0, 32000);
	memset(backBuffer, 0, 32000);
	VID_UpdateScreenPointers();

	for (int n = 0; n < 16; n++)
	{
		VID_SetPaletteColor(n,
		                    EGAPalette[n] >> 16,
		                    EGAPalette[n] >> 8,
		                    EGAPalette[n]);
	}
	VID_UpdateInkMap();

	HideCursor();
	VID_Clear(0, 0, 320, 200, 0);
	return true;
}

void VID_VSync()
{
	Vsync();
}

#endif