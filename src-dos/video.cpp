#ifdef _DOS

#include <ddb_vid.h>
#include <ddb_data.h>
#include <ddb_scr.h>
#include <os_mem.h>

#include "dmg.h"
#include "sb.h"
#include "mixer.h"
#include "timer.h"

#include <i86.h>
#include <conio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t*   pictureData = 0;
static uint8_t*   audioData = 0;
static DMG_Entry* bufferedEntry;
static int        bufferedEntryIndex;
static bool       initialized = false;
static bool       quit;

bool exitGame = false;
bool supportsOpenFileDialog = false;

// ----------------------------------------------------------------------------
//  File stuff
// ----------------------------------------------------------------------------

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

static char newFileName[MAX_PATH];

static const char* ChangeExtension(const char* fileName, const char* extension)
{
	strncpy(newFileName, fileName, MAX_PATH);
	newFileName[MAX_PATH-1] = 0;
	char* ptr = strrchr(newFileName, '.');
	if (ptr == NULL)
		ptr = newFileName + strlen(newFileName);
	strcpy(ptr, extension);
	return newFileName;
}

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

// ----------------------------------------------------------------------------
//  Video related variables
// ----------------------------------------------------------------------------

static uint8_t* offset;
static uint32_t pageCount;
static uint32_t previousVideoMode;
static uint32_t activePage;
static uint32_t visiblePage;
static uint32_t pageSize;
static uint32_t lineSize;
static uint32_t palette[256];
static uint32_t width;
static uint32_t height;
static uint8_t  fg;
static int32_t  cx;
static int32_t  cy;
static int32_t  minx;
static int32_t  maxx;
static int32_t  miny;
static int32_t  maxy;

// ----------------------------------------------------------------------------
//  Setting the video mode 
// ----------------------------------------------------------------------------

enum VideoMode
{
	MODE_TEXT,
	MODE_320x200,
	MODE_320x240,
	MODE_320x400,
	MODE_360x270,
	MODE_400x300
};

typedef struct 
{
	uint16_t port;
	uint8_t  index;
	uint8_t  value;
}
VideoModeReg;

static VideoModeReg regs320x200[] =
{
	{ 0x3C2, 0x00, 0x63 },
	{ 0x3D4, 0x00, 0x5F },
	{ 0x3D4, 0x01, 0x4F },
	{ 0x3D4, 0x02, 0x50 },
	{ 0x3D4, 0x03, 0x82 },
	{ 0x3D4, 0x04, 0x54 },
	{ 0x3D4, 0x05, 0x80 },
	{ 0x3D4, 0x06, 0xBF },
	{ 0x3D4, 0x07, 0x1F },
	{ 0x3D4, 0x08, 0x00 },
	{ 0x3D4, 0x09, 0x41 },
	{ 0x3D4, 0x10, 0x9C },
	{ 0x3D4, 0x11, 0x8E },
	{ 0x3D4, 0x12, 0x8F },
	{ 0x3D4, 0x13, 0x28 },
	{ 0x3D4, 0x14, 0x00 },
	{ 0x3D4, 0x15, 0x96 },
	{ 0x3D4, 0x16, 0xB9 },
	{ 0x3D4, 0x17, 0xE3 },
	{ 0x3C4, 0x01, 0x01 },
	{ 0x3C4, 0x04, 0x06 },
	{ 0x3CE, 0x05, 0x40 },
	{ 0x3CE, 0x06, 0x05 },
	{ 0x3C0, 0x10, 0x41 },
	{ 0x3C0, 0x13, 0x00 },
	{ 0 }
};

static VideoModeReg regs320x240[] =
{
	{ 0x3C2, 0x00, 0xE3 },
	{ 0x3D4, 0x00, 0x5F },
	{ 0x3D4, 0x01, 0x4F },
	{ 0x3D4, 0x02, 0x50 },
	{ 0x3D4, 0x03, 0x82 },
	{ 0x3D4, 0x04, 0x54 },
	{ 0x3D4, 0x05, 0x80 },
	{ 0x3D4, 0x06, 0x0D },
	{ 0x3D4, 0x07, 0x3E },
	{ 0x3D4, 0x08, 0x00 },
	{ 0x3D4, 0x09, 0x41 },
	{ 0x3D4, 0x10, 0xEA },
	{ 0x3D4, 0x11, 0xAC },
	{ 0x3D4, 0x12, 0xDF },
	{ 0x3D4, 0x13, 0x28 },
	{ 0x3D4, 0x14, 0x00 },
	{ 0x3D4, 0x15, 0xE7 },
	{ 0x3D4, 0x16, 0x06 },
	{ 0x3D4, 0x17, 0xE3 },
	{ 0x3C4, 0x01, 0x01 },
	{ 0x3C4, 0x04, 0x06 },
	{ 0x3CE, 0x05, 0x40 },
	{ 0x3CE, 0x06, 0x05 },
	{ 0x3C0, 0x10, 0x41 },
	{ 0x3C0, 0x13, 0x00 },
	{ 0 }
};

static VideoModeReg regs360x270[] =
{
	{ 0x3C2, 0x00, 0xE7 },
	{ 0x3D4, 0x00, 0x6B },
	{ 0x3D4, 0x01, 0x59 },
	{ 0x3D4, 0x02, 0x5A },
	{ 0x3D4, 0x03, 0x8E },
	{ 0x3D4, 0x04, 0x5E },
	{ 0x3D4, 0x05, 0x8A },
	{ 0x3D4, 0x06, 0x30 },
	{ 0x3D4, 0x07, 0xF0 },
	{ 0x3D4, 0x08, 0x00 },
	{ 0x3D4, 0x09, 0x61 },
	{ 0x3D4, 0x10, 0x20 },
	{ 0x3D4, 0x11, 0xA9 },
	{ 0x3D4, 0x12, 0x1B },
	{ 0x3D4, 0x13, 0x2D },
	{ 0x3D4, 0x14, 0x00 },
	{ 0x3D4, 0x15, 0x1F },
	{ 0x3D4, 0x16, 0x2F },
	{ 0x3D4, 0x17, 0xE3 },
	{ 0x3C4, 0x01, 0x01 },
	{ 0x3C4, 0x04, 0x06 },
	{ 0x3CE, 0x05, 0x40 },
	{ 0x3CE, 0x06, 0x05 },
	{ 0x3C0, 0x10, 0x41 },
	{ 0x3C0, 0x13, 0x00 },
	{ 0 }
};

static VideoModeReg regs400x300[] =
{
	{ 0x3C2, 0x00, 0xE7 },
	{ 0x3D4, 0x00, 0x71 },
	{ 0x3D4, 0x01, 0x63 },
	{ 0x3D4, 0x02, 0x64 },
	{ 0x3D4, 0x03, 0x92 },
	{ 0x3D4, 0x04, 0x67 },
	{ 0x3D4, 0x05, 0x82 },
	{ 0x3D4, 0x06, 0x46 },
	{ 0x3D4, 0x07, 0x1F },
	{ 0x3D4, 0x08, 0x00 },
	{ 0x3D4, 0x09, 0x40 },
	{ 0x3D4, 0x10, 0x31 },
	{ 0x3D4, 0x11, 0x80 },
	{ 0x3D4, 0x12, 0x2B },
	{ 0x3D4, 0x13, 0x32 },
	{ 0x3D4, 0x14, 0x00 },
	{ 0x3D4, 0x15, 0x2F },
	{ 0x3D4, 0x16, 0x44 },
	{ 0x3D4, 0x17, 0xE3 },
	{ 0x3C4, 0x01, 0x01 },
	{ 0x3C4, 0x02, 0x0F },
	{ 0x3C4, 0x04, 0x06 },
	{ 0x3CE, 0x05, 0x40 },
	{ 0x3CE, 0x06, 0x05 },
	{ 0x3C0, 0x10, 0x41 },
	{ 0x3C0, 0x13, 0x00 },
	{ 0 }
};

void SetBIOSMode (int mode)
{
	union REGS regs;
	regs.w.ax = mode;
	int386(0x10, &regs, &regs);
}

void SetVideoMode (int mode)
{
	VideoModeReg* regs;

	switch (mode)
	{
		case MODE_TEXT:
			SetBIOSMode(0x02);
			return;

		case MODE_320x200:
			width  = 320;
			height = 200;
			regs   = regs320x200;
			break;

		case MODE_320x240:
			width  = 320;
			height = 240;
			regs   = regs320x240;
			break;

		case MODE_360x270:
			width  = 360;
			height = 270;
			regs   = regs360x270;
			break;

		case MODE_400x300:
			width  = 400;
			height = 300;
			regs   = regs400x300;
			break;

		default:
			return;
	}

	/* Set video mode 13h, takes care of the palette */
	SetBIOSMode(0x13);

	/* Clear bit 7 of CRTC controller register 11h
	 * to enable writing to CRTC registers 0 to 7 */

	outp(0x3D4, 0x11);			// CRT Controller
	uint8_t v = inp(0x3D5);

	outp(0x3D4, 0x11);
	outp(0x3D5, v & 0x7F);

	/* Program VGA registers from the mode table.
	 * Consider special handling for several ports */

	while (regs->port != 0)
	{
		switch (regs->port)
		{
			case 0x3C0:			// ATTR_CON
				inp(0x3DA);		// STATUS
				outp(0x3C0, regs->index | 0x20);
				outp(0x3C0, regs->value);
				break;

			case 0x3C2:			// MISC
			case 0x3C3:			// VGAENABLE
				outp(regs->port, regs->value);
				break;

			default:
				outp(regs->port,   regs->index);
				outp(regs->port+1, regs->value);
				break;
		}
		regs++;
	}

	/* Enable planar mode */

	outpw(0x3C4, 0x0F02);

	/* Fill other internal variables */
	
	pageSize    = (width*height) >> 2;
	pageCount   = 65536 / pageSize;
	lineSize    = width >> 2;
	offset      = (uint8_t*)0xA0000;
	activePage  = 0;
	visiblePage = 0;
	minx        = 0;
	miny        = 0;
	maxx        = width-1;
	maxy        = height-1;
}

// ----------------------------------------------------------------------------
//  Video driver
// ----------------------------------------------------------------------------

static int32_t middleMask[4][4] = 
{
    {0x102, 0x302, 0x702, 0xf02},
    {0x002, 0x202, 0x602, 0xe02},
    {0x002, 0x002, 0x402, 0xc02},
    {0x002, 0x002, 0x002, 0x802},
};
static int32_t leftMask[4]  = {0xf02, 0xe02, 0xc02, 0x802};
static int32_t rightMask[4] = {0x102, 0x302, 0x702, 0xf02};

static void memset32 (void* addr, uint32_t value, int32_t count);
#pragma aux memset32 = 		\
	"cld" 					\
	"rep stosd" 			\
	parm [edi] [eax] [ecx];

static void memset8 (void* addr, uint8_t value, int32_t count);
#pragma aux memset8 =		\
	"cld" 					\
	"rep stosb" 			\
	parm [edi] [al] [ecx];

void VID_VSync()
{
	/* Wait for vertical retrace */
	while (!(inp(0x3DA) & 0x08))			// IST1
		;		
	while (inp(0x3DA) & 0x08)				// IST1
		;		
}

void VID_SetPaletteColor (uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	outp(0x3C8, index);					// PAL WRITE
	outp(0x3C9, r >> 2);				// PAL DATA
	outp(0x3C9, g >> 2);
	outp(0x3C9, b >> 2);
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

bool VID_LoadDataFile (const char* fileName)
{
	if (dmg != NULL)
	{
		DMG_Close(dmg);
		dmg = NULL;
	}

	dmg = DMG_Open(ChangeExtension(fileName, ".dat"), true);
	if (dmg == NULL)
		dmg = DMG_Open(ChangeExtension(fileName, ".ega"), true);
	if (dmg == NULL)
		dmg = DMG_Open(ChangeExtension(fileName, ".cga"), true);
	if (dmg == NULL)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		VID_Finish();
		return false;
	}

	if (!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
	}

	return true;
}

void VID_Clear (int x, int y, int width, int height, uint8_t color)
{
    int32_t i;
    int32_t x1;
    int32_t x2;
    int32_t y1;
    int32_t y2;

    int32_t leftBand;
    int32_t rightBand;
    int32_t leftBit;
    int32_t rightBit;
    int32_t mask;
    int32_t bands;
    int32_t bands32;
    uint32_t color32;

    uint8_t *top;
    uint8_t *where;

    x1 = x;
    x2 = x + width - 1;
    y1 = y;
    y2 = y + height - 1;

    if (x1 < minx) 	x1 = minx;
    if (x2 > maxx)	x2 = maxx;
    if (y1 < miny)  y1 = miny;
    if (y2 > maxy)  y2 = maxy;

    if (y2 < y1 || x2 < x1)
        return;

	leftBand  = x1 >> 2; 
	rightBand = x2 >> 2;
	leftBit   = x1 & 3; 
	rightBit  = x2 & 3;

    if (leftBand == rightBand)
    {
        mask = middleMask[leftBit][rightBit];
        outpw(0x3C4, mask);

        top = offset + (lineSize * y1) + leftBand;
        for (i = y1; i <= y2; i++)
        {
            *top = color;
            top += lineSize;
        }
    }
    else
    {
        mask = leftMask[leftBit];
        outpw(0x3C4, mask);

        top = offset + (lineSize * y1) + leftBand;
        where = top;
        for (i = y1; i <= y2; i++)          // fill the left edge
        {
            *where = color;
            where += lineSize;
        }
        top++;

        bands = rightBand - (leftBand + 1);
        if (bands > 0)
        {
	        outpw(0x3C4, 0xF02);					// SEQ

        	bands32 = bands >> 2;
        	bands   = bands & 0x03;

        	if (bands32 > 0)
        	{
        		color32 =  (color | (color << 8));
        		color32 |= (color32 << 16);

	            where = top;
	            for (i = y1; i <= y2; i++)      // fill the middle
	            {
	                memset32(where, color32, bands32);
	                where += lineSize;
	            }
	            top += bands32*4;
	        }
	        if (bands > 0)
	        {
	            where = top;
	            for (i = y1; i <= y2; i++)      // fill the middle
	            {
	                memset8(where, color, bands);
	                where += lineSize;
	            }
	            top += bands;
	        }
        }

        mask = rightMask[rightBit];
        outpw(0x3C4, mask);					// SEQ

        where = top;
        for (i = y1; i <= y2; i++)          // fill the right edge
        {
            *where = color;
            where += lineSize;
        }
    }
}

void VID_Finish()
{
	if (!initialized)
		return;

	SB_Stop();
	Timer_Stop();

	VID_Clear(0, 0, screenWidth, screenHeight, 0);
	VID_VSync();

	SetVideoMode(MODE_TEXT);
	initialized = false;
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode screenMode)
{	
	DMG_Entry* entry = bufferedEntry;
	if (entry == NULL)
		return;
		
	int32_t        sx  = 0;
	int32_t        sy  = 0;
	int32_t        bw  = entry->width;
	int32_t        bh  = entry->height;
	int32_t        bl  = bh;
	const uint8_t* in  = pictureData;
	uint8_t*       out = offset;
		
	if (bw > w)
		bw = w;
	if (bh > h)
		bh = h;
	if (x < minx)
	{
		sx  = minx-x;
		bw -= sx;
		x   = minx;
	}
	if (y < miny)
	{
		sy  = miny-y;
		bh -= sy;
		y   = miny;
	}
	if (x+bw > maxx)
		bw = maxx-x+1;
	if (y+bh > maxy)
		bh = maxy-y+1;
	if (bw <= 0 || bh <= 0)
		return;

	in  += entry->height*sx + sy;
	out += y*lineSize + (x >> 2);

	outp(0x3C4, 0x02);

	int32_t stride  = entry->width >> 1;
	int32_t dec     = bh * lineSize;
	int32_t idec    = bh * stride;
	int mask    = 1 << (x & 3);

	bw >>= 1;

	do
	{
		uint8_t* cout      = out;
		const uint8_t* cin = in;

		outp(0x3C5, mask);

		int n = bh;
		do
		{
			*out = (*in >> 4) & 0x0F;
			out += lineSize;
			in  += stride;
		}
		while (--n);
		in  -= idec;
		out -= dec;
		mask <<= 1;

		outp(0x3C5, mask);
		out  += (mask >> 4);
		mask |= (mask >> 4);
		mask &= 0x0F;

		n = bh;
		do
		{
			*out = *in & 0x0F;
			out += lineSize;
			in  += stride;
		}
		while (--n);
		in  -= idec;
		out -= dec;
		mask <<= 1;

		out  += (mask >> 4);
		mask |= (mask >> 4);
		mask &= 0x0F;
		in++;
	}
	while (--bw);

	// Update color palette

	if (entry->fixed) 
	{
		uint32_t* palette = DMG_GetEntryPalette(dmg, bufferedEntryIndex, 
			screenMode == ScreenMode_VGA16 ? ImageMode_RGBA32 :
			screenMode == ScreenMode_EGA ? ImageMode_RGBA32EGA : ImageMode_RGBA32CGA);
		if (dmg->version == DMG_Version1)
			palette[inkMap[1]] = 0xFFFFFFFF;
		for (int n = 0; n < 16; n++) {
			uint8_t r = (palette[n] >> 16) & 0xFF;
			uint8_t g = (palette[n] >>  8) & 0xFF;
			uint8_t b = (palette[n] >>  0) & 0xFF;
			VID_SetPaletteColor(n, r, g, b);
		}
		VID_UpdateInkMap();
	}
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target)
{
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
		int mask = 0x01;
		uint8_t *out = offset;
		uint8_t *in = output;

		for (int n = 0; n < 16; n++) 
			VID_SetPaletteColor(n, 0, 0, 0);
		VID_VSync();

		outp(0x3C4, 0x02);

		for (int x = 0; x < screenWidth; x++)
		{
			uint8_t* inPtr = in;
			uint8_t* outPtr = out;

			outp(0x3C5, mask);

			int y = screenHeight;
			do
			{
				*outPtr = *inPtr;
				inPtr += screenWidth;
				outPtr += lineSize;
			}
			while (--y);

			in++;
			
			mask <<= 1;
			out  += (mask >> 4);
			mask |= (mask >> 4);
			mask &= 0x0F;
		}
		
		VID_VSync();
		for (int n = 0; n < 16; n++) 
		{
			uint8_t r = (palette[n] >> 16) & 0xFF;
			uint8_t g = (palette[n] >>  8) & 0xFF;
			uint8_t b = (palette[n] >>  0) & 0xFF;
			VID_SetPaletteColor(n, r, g, b);
		}
	}

	Free(output);
	Free(buffer);
	return true;
}

void VID_DrawCharacter (int x, int y, uint8_t c, uint8_t ink, uint8_t paper)
{
	uint8_t* ptr   = offset + lineSize*y + (x >> 2);	
	int      mask  = 1 << (x & 3);
	int32_t        nextX = x + charWidth[c];
	const uint8_t* data  = charset + 8*c;
	int            pixel = 0x0080;

	outp(0x3C4, 0x02);

	x = nextX - x;
	if (x <= 0) return;

	if (paper == 255)
	{
		do
		{
			uint8_t *cptr = ptr;
			const uint8_t *cdata = data;

			outp(0x3C5, mask);

			int n = 8;
			do
			{
				if (*data & pixel)
					*ptr = ink;
				ptr += lineSize, data++;
			} while (--n);

			data = cdata;
			ptr = cptr;

			mask <<= 1;
			ptr   += (mask >> 4);
			mask  |= (mask >> 4);
			mask  &= 0x0F;
			pixel >>= 1;
		}
		while (--x);
	}
	else
	{
		int dec = lineSize*8;

		do
		{
			outp(0x3C5, mask);

			int n = 8;
			do
			{
				*ptr = (*data & pixel) ? ink : paper;
				ptr += lineSize;
				data++;
			} while (--n);

			data -= 8;
			ptr -= dec;

			mask <<= 1;
			ptr   += (mask >> 4);
			mask  |= (mask >> 4);
			mask  &= 0x0F;
			pixel >>= 1;
		}
		while(--x);
	}
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* modifiers)
{
	union REGS regs;
	regs.h.ah = 0x02;
	int386(0x16, &regs, &regs);

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
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = Timer_GetMilliseconds();

	// union REGS regs;
	// regs.h.ah = 0x00;
	// int386(0x1A, &regs, &regs);
	// *time = ((regs.w.cx << 16) + regs.w.dx) * 55;
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	outp(0x3C7, color);
	*r = inp(0x3C9) << 2;
	*g = inp(0x3C9) << 2;
	*b = inp(0x3C9) << 2;
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{	if (bufferedEntry == NULL)
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
			*fixed = bufferedEntry->fixed;
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
	if (dmg == NULL) return;

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Image)
		return;

	bufferedEntry = entry;
	bufferedEntryIndex = picno;
	pictureData = DMG_GetEntryDataChunky(dmg, picno);
	if (pictureData == NULL)
		bufferedEntry = NULL;
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
		case DMG_5KHZ:   sampleHz =  5000; break;
		case DMG_7KHZ:   sampleHz =  7000; break;
		case DMG_9_5KHZ: sampleHz =  9500; break;
		case DMG_15KHZ:  sampleHz = 15000; break;
		case DMG_20KHZ:  sampleHz = 20000; break;
		case DMG_30KHZ:  sampleHz = 30000; break;
		default:         sampleHz = 11025; break;
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
	visiblePage = !visiblePage;
	VID_SaveScreen();
	visiblePage = !visiblePage;
}

void VID_SaveScreen ()
{
	const uint8_t* in = (uint8_t*)0xA0000 + pageSize*visiblePage;
	uint8_t* out = (uint8_t*)0xA0000 + pageSize*(!visiblePage);
	int mask  = 1;
	int plane = 0;
	
	outp(0x3C4, 0x02);
	outp(0x3CE, 0x04);

	for (int cx = 0; cx < screenWidth; cx++)
	{
		uint8_t* cout      = out;
		const uint8_t* cin = in;

		outp(0x3C5, mask);
		outp(0x3CF, plane);

		for (int n = 0 ; n < screenHeight ; n++)
		{
			*out = *in;
			out += lineSize;
			in  += lineSize;
		}
		in  = cin;
		out = cout;

		if (mask == 8)
			out++, mask = 0x01;
		else
			mask <<= 1;

		if (plane == 3)
			in++, plane = 0;
		else
			plane++;
	}	
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	const uint8_t* in  = offset + lineSize*(y + lines) + (x >> 2);
	uint8_t*       out = offset + lineSize*y + (x >> 2);
	int32_t        mask, plane;
		
	mask = 1 << (x & 3);
	plane = (x & 3);

	outp(0x3C4, 0x02);
	outp(0x3CE, 0x04);

	h -= lines;

	if (w <= 0 || h <= 0)
		return;

	cx = w;
	const int inc = lineSize;
	const int indec = lineSize * h;
	const int outdec = lineSize * (h + lines);
	do
	{
		outp(0x3C5, mask);
		outp(0x3CF, plane);

		int n = h;
		do
		{
			*out = *in;
			out += inc;
			in  += inc;
		}
		while (--n);

		n = lines;
		do
		{
			*out = paper;
			out += inc;
		}
		while (--n);

		in  -= indec;
		out -= outdec;

		mask <<= 1;
		out += (mask >> 4);
		mask |= (mask >> 4);
		mask &= 0x0F;
		
		if (plane < 3)
			plane++;
		else
		{
			in++;
			plane = 0;
		}
	}
	while (--cx);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	activePage = front ? visiblePage : !visiblePage;
	offset     = (uint8_t*)0xA0000 + pageSize*activePage;
}

void VID_ClearBuffer (bool front)
{
	uint8_t* top = (uint8_t*)0xA0000 + pageSize*(front ? 1 : 0);

	outpw(0x3C4, 0xF02);
	memset32(top, 0, 4000);
}

void VID_SwapScreen ()
{
	visiblePage = !visiblePage;
	activePage = !activePage;

	/* Wait for display disable */
	while (inp(0x3DA) & 0x01)				// IST1
		;		

	/* Program CRT controller */
	uint32_t offset = pageSize*activePage;
	outpw(0x3D4, 0x0C |  (offset & 0xFF00));
	outpw(0x3D4, 0x0D | ((offset & 0x00FF) << 8));
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

bool VID_Initialize()
{
	if (initialized)
		return false;

	if (SB_Init(0, 30000))
		SB_Start();

	Timer_Start();

	screenWidth   = 320;
	screenHeight  = 200;
	lineHeight    = 8;
	columnWidth   = 6;
	screenWidth   = 320;
	screenHeight  = 200;
	inkMap[0]     = 0;
	inkMap[1]     = 15;
	for (int n = 2; n < 16; n++)
		inkMap[n] = n-1;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);
	SetVideoMode(MODE_320x200);

	initialized = true;
	return true;
}

void VID_SetTextInputMode (bool enabled)
{
	// Not supported
}

#endif