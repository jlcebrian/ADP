#ifdef _AMIGA

#include "gcc8_c_support.h"

#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_pal.h>
#include <ddb_data.h>
#include <os_mem.h>
#include <os_lib.h>
#include <os_file.h>
#include <dmg.h>

#include "keyboard.h"
#include "timer.h"
#include "video.h"

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

#define R_VPOSR ( *(volatile uint32_t*)0xDFF004 )

extern volatile Custom *custom;

bool	 supportsOpenFileDialog = false;

bool     isPAL = false;
int      cursorX;
int      cursorY;
bool     quit = false;
bool     exitGame = false;
bool     displaySwap = true;
bool     charToBack = false;
bool     drawToBack = false;
bool     initialized = false;

uint8_t*    plane[4];
uint8_t*	frontBuffer = 0;
uint8_t*	backBuffer = 0;
uint8_t*    frontPlane[4];
uint8_t*    backPlane[4];

static DMG*       pictureOrigin;
static DMG_Entry* pictureEntry = 0;
static int        pictureIndex = 0;
static uint16_t*  pictureData = 0;
static uint32_t   pictureStride;

uint16_t  (*charsetWords)[256][8] = 0;
uint16_t* copper1;
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
	0xffff, 0xfffe // end copper list
};

INLINE uint16_t* SetVisiblePlanes(uint8_t** planes, uint16_t* copListEnd) 
{
	for (uint16_t i = 0 ; i < BITPLANES ; i++) {
		uint32_t addr = (uint32_t)planes[i];
		*copListEnd++ = offsetof(Custom, bplpt[0]) + i*sizeof(APTR);
		*copListEnd++ = (uint16_t)(addr>>16);
		*copListEnd++ = offsetof(Custom, bplpt[0]) + i*sizeof(APTR) + 2;
		*copListEnd++ = (uint16_t)addr;

		plane[i] = planes[i];
	}
	return copListEnd;
}

INLINE void RunCopperProgram(uint16_t* program, uint16_t* end)
{
	// Jump to copper2
	*end++ = offsetof(Custom, copjmp2); 
	*end++ = 0x7fff;

	custom->cop1lc  = (uint32_t)program;
	custom->cop2lc  = (uint32_t)copper2;
	custom->dmacon  = DMAF_BLITTER; // Disable blitter dma for copjmp bug
	custom->copjmp1 = 0x7fff;       // Start coppper
	custom->dmacon  = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
}

static const char* ChangeExtension(const char* fileName, const char* extension)
{
	static char newFileName[256];

	StrCopy(newFileName, 256, fileName);
	newFileName[256-1] = 0;
	char* ptr = (char *)StrRChr(newFileName, '.');
	if (ptr == 0)
		ptr = newFileName + StrLen(newFileName);
	StrCopy(ptr, newFileName+256-ptr, extension);
	return newFileName;
}

void VID_ActivateCharset()
{
	if (charsetWords == 0)
		charsetWords = (uint16_t(*)[256][8])AllocMem(8192, MEMF_CHIP | MEMF_CLEAR);

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

void VID_SetColor(uint8_t color, uint16_t pal)
{
	// char buf[16];
	// LongToChar(pal, buf, 16);
	// DebugPrintf("Setting color %ld to %s\n", (long)color, buf);

	custom->color[color] = pal;
	copper2[color*2+1] = pal;
}

uint16_t VID_GetColor(uint8_t color)
{
	return copper2[color*2+1];
}

void VID_ActivatePalette()
{
	VID_VSync();
	custom->cop2lc  = (uint32_t)copper2;
	custom->copjmp2 = 0x7fff;       // Start coppper
}

void VID_SetPalette (uint32_t* palette)
{
	for (int n = 0; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >>  8) & 0xFF;
		uint8_t b = (palette[n]      ) & 0xFF;
		VID_SetPaletteColor(n, r, g, b);
	}
	VID_UpdateInkMap();

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

	uint32_t freeMemory = AvailMem(0);
	uint32_t datSize = File_GetSize(dmg->file);
	DebugPrintf("Free memory: %u bytes\n", freeMemory);

	if (freeMemory > datSize + 32768)			// Everything fits
	{
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
		freeMemory = AvailMem(0);
		if (freeMemory >= 32768)
			DMG_SetupImageCache(dmg, freeMemory);
	}
	else if (freeMemory > datSize)				// DAT barely fits
	{
		DMG_SetupImageCache(dmg, 32768);
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x40000)				// >256K free
	{
		DMG_SetupImageCache(dmg, 0x18000);	// 96K cache
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else if (freeMemory > 0x20000)				// >128K free
	{
		DMG_SetupImageCache(dmg, 0xC000);	// 48K cache
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
	}
	else 
	{
		DMG_SetupImageCache(dmg, 0x8000);	// 32K cache
		DMG_SetupFileCache(dmg, 0, VID_ShowProgressBar);
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

void VID_Clear (int x, int y, int w, int h, uint8_t color)
{
	if (!AdjustCoordinates(x, y, w, h))
		return;
		
	if (y >= screenHeight || w < 1 || h < 1)
		return;
		
	if (w*h >= 8192)
		VID_VSync();

	BlitterRect(plane[0], x, y, w, h, color & 1);
	BlitterRect(plane[1], x, y, w, h, color & 2);
	BlitterRect(plane[2], x, y, w, h, color & 4);
	BlitterRect(plane[3], x, y, w, h, color & 8);
}

void VID_ClearBuffer (bool front)
{
	uint8_t** p = front ^ displaySwap ? frontPlane : backPlane;
	BlitterRect(p[0], 0, 0, screenWidth, screenHeight, 0);
	BlitterRect(p[1], 0, 0, screenWidth, screenHeight, 0);
	BlitterRect(p[2], 0, 0, screenWidth, screenHeight, 0);
	BlitterRect(p[3], 0, 0, screenWidth, screenHeight, 0);
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

	if (h > pictureEntry->height)
		h = pictureEntry->height;
	if (w > pictureEntry->width)
		w = pictureEntry->width;

	uint32_t* palette = DMG_GetEntryPalette(dmg, pictureIndex, ImageMode_RGBA32);
	switch (screenMode)
	{
		default:
		case ScreenMode_VGA16:
			if (pictureEntry->fixed && plane[0] == frontBuffer)
			{
				// TODO: This is a hack to fix the palette for the old version of the game
				if (dmg->version == DMG_Version1)
					pictureEntry->RGB32Palette[15] = 0xFFFFFFFF;
				VID_SetPalette(palette);
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			VID_SetPalette(pictureEntry->CGAMode == CGA_Red ? CGAPaletteRed : CGAPaletteCyan);
			break;
	}
	
	uint16_t* srcPtr = pictureData;
	uint32_t off = y*SCR_STRIDEB + (x >> 3);
	bool skipLastByte = (w & 15) != 0;

	w = (w + 15)/16;
	uint32_t  pinc = SCR_STRIDEW - w;
	uint32_t a = h;
	if (off & 1)
	{
		uint8_t* p0 = plane[0] + off;
		uint8_t* p1 = plane[1] + off;
		uint8_t* p2 = plane[2] + off;
		uint8_t* p3 = plane[3] + off;

		pinc <<= 1;
		a--;
		pinc += 2;

		uint8_t* s = (uint8_t*)srcPtr;

		// Unaligned writes
		do
		{
			uint8_t* n = s + pictureStride*2;
			for (int32_t b = w-1; b > 0; b--)
			{
				p0[0] = *s++; p0[1] = *s++; p0 += 2; 
				p1[0] = *s++; p1[1] = *s++; p1 += 2; 
				p2[0] = *s++; p2[1] = *s++; p2 += 2; 
				p3[0] = *s++; p3[1] = *s++; p3 += 2; 
			}
			if (skipLastByte)
			{
				p0[0] = s[0]; p0 += pinc;
				p1[0] = s[2]; p1 += pinc;
				p2[0] = s[4]; p2 += pinc;
				p3[0] = s[6]; p3 += pinc;
				s += 8;
			}
			else
			{
				p0[0] = *s++; p0[1] = *s++; p0 += pinc;
				p1[0] = *s++; p1[1] = *s++; p1 += pinc;
				p2[0] = *s++; p2[1] = *s++; p2 += pinc;
				p3[0] = *s++; p3[1] = *s++; p3 += pinc;
			}

			s = n;
		}
		while (a--);
	}
	else
	{
		uint16_t* p0 = (uint16_t*)(plane[0] + off);
		uint16_t* p1 = (uint16_t*)(plane[1] + off);
		uint16_t* p2 = (uint16_t*)(plane[2] + off);
		uint16_t* p3 = (uint16_t*)(plane[3] + off);

		a--;
		pinc++;

		uint16_t rightMask = ~(0xFFFF << ((x+w) & 15));

		do
		{
			uint16_t* n = srcPtr + pictureStride;

			for (int32_t b = w-1; b > 0; b--)
			{
				*p0++ = *srcPtr++;
				*p1++ = *srcPtr++;
				*p2++ = *srcPtr++;
				*p3++ = *srcPtr++;
			}
			if (skipLastByte)
			{
				*p0 = *srcPtr++ | (*p0 & 0xFF);
				*p1 = *srcPtr++ | (*p1 & 0xFF);
				*p2 = *srcPtr++ | (*p2 & 0xFF);
				*p3 = *srcPtr++ | (*p3 & 0xFF);
			}
			else
			{
				*p0 = *srcPtr++;
				*p1 = *srcPtr++;
				*p2 = *srcPtr++;
				*p3 = *srcPtr++;
			}

			p0     += pinc;
			p1     += pinc;
			p2     += pinc;
			p3     += pinc;
			srcPtr  = n;
		}
		while (a--);
	}
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	BlitterChar(charToBack ^ displaySwap ? backBuffer : frontBuffer, x, y, ch, ink, paper);
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
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = GetMilliseconds();
}

__attribute__((noinline))
void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
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
		// VID_ShowError("Driver has no DMG");
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
		// VID_ShowError(driver, DMG_GetErrorString());
		pictureEntry = 0;
		pictureOrigin = 0;
	}
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
	for (int p = 0; p < 4; p++)
	{
		BlitterCopy(plane[p], x, y + lines, plane[p], x, y, w, h, true); 
		BlitterRect(plane[p], x, y + h, w, lines, paper & 1);
		paper >>= 1;
	}
}

__attribute__((noinline))
void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t c = ((r & 0xF0) << 4) | (g & 0xF0) | (b >> 4);
	VID_SetColor(color, c);
}

void VID_SetTextInputMode (bool enabled)
{
	// Not needed
}

void VID_RestoreScreen ()
{
	DebugPrintf(displaySwap ? "Restoring screen (front to back)\n" : "Restoring screen (back to front)\n");
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(front, back, SCR_ALLOCATE);
}

void VID_SaveScreen ()
{
	DebugPrintf(displaySwap ? "Saving screen (back to front)\n" : "Saving screen (front to back)\n");
	uint8_t* front = displaySwap ? backBuffer : frontBuffer;
	uint8_t* back = displaySwap ? frontBuffer : backBuffer;
	memcpy(back, front, SCR_ALLOCATE);
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
	for (unsigned n = 0; n < BITPLANES; n++)
		plane[n] = drawToBack ? backPlane[n] : frontPlane[n];
}

void VID_SwapScreen ()
{
	displaySwap = !displaySwap;
	charToBack = !charToBack;
	drawToBack = !drawToBack;

	uint16_t* end = copper1;
	if (displaySwap)
		end = SetVisiblePlanes(backPlane, end);
	else
		end = SetVisiblePlanes(frontPlane, end);
	
	VID_VSync();
	RunCopperProgram(copper1, end);

	if (displaySwap) {
		DebugPrintf("Display swapped\n");
	} else {
		DebugPrintf("Display restored\n");
	}

	for (unsigned n = 0; n < BITPLANES; n++)
		plane[n] = drawToBack ? backPlane[n] : frontPlane[n];
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
	*copPtr++ = (1<<9) | (BITPLANES << 12);
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
	uint16_t* copPtr = copper1;
	copPtr = SetScreenLayout(copPtr);
	copPtr = SetBitPlanes(copPtr);
	copPtr = SetVisiblePlanes(frontPlane, copPtr);
	RunCopperProgram(copper1, copPtr);
}

bool VID_Initialize()
{
	if (initialized)
		return true;
	initialized = true;

	OpenTimer();

	if (SysBase->VBlankFrequency == 50)
		isPAL = true;
	else
		isPAL = false;

	screenHeight  = 200;
	screenWidth   = 320;
	lineHeight    = 8;
	columnWidth   = 6;

	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset,      DefaultCharset, 1024);
	memcpy(charset+1024, DefaultCharset, 1024);
	VID_ActivateCharset();

	frontBuffer = (uint8_t*)AllocMem(SCR_ALLOCATE, MEMF_CHIP);
	MemClear(frontBuffer, SCR_ALLOCATE);
	backBuffer = (uint8_t*)AllocMem(SCR_ALLOCATE, MEMF_CHIP);
	MemClear(backBuffer, SCR_ALLOCATE);

	copper1 = (uint16_t*)AllocMem(1024, MEMF_CHIP);
	
	TakeSystem();

	for (unsigned n = 0; n < BITPLANES; n++)
	{
		frontPlane[n] = frontBuffer + SCR_BPNEXTB * n;
		backPlane[n]  = backBuffer  + SCR_BPNEXTB * n;
	}
	displaySwap = false;

	VID_VSync();
	uint16_t* copPtr = copper1;
	copPtr = SetScreenLayout(copPtr);
	copPtr = SetBitPlanes(copPtr);
	copPtr = SetVisiblePlanes(frontPlane, copPtr);
	RunCopperProgram(copper1, copPtr);

	return true;
}

void VID_Finish ()
{
	if (!initialized)
		return;
	initialized = false;

	FreeSystem();
	
	CloseTimer();

	FreeMem(copper1, 1024);
	FreeMem(backBuffer, SCR_ALLOCATE);
	FreeMem(frontBuffer, SCR_ALLOCATE);
}

#endif