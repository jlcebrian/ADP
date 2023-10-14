#if HAS_DRAWSTRING

#include "ddb.h"
#include "ddb_data.h"
#include "ddb_vid.h"
#include "os_bito.h"
#include "os_lib.h"

// #define TRACE_VECTOR
// #define DISABLE_FILL

extern void VID_SaveDebugBitmap();

static const uint8_t* vectorGraphicsRAM = 0;

static uint16_t start;
static uint16_t table;
static uint16_t windefs;
static uint16_t unknown;
static uint16_t charset;
static uint16_t coltab;
static uint16_t ending;
static uint16_t count;

extern uint8_t* bitmap;
extern uint8_t* attributes;

static uint16_t cursorX;
static uint16_t cursorY;
static uint8_t ink;
static uint8_t paper;
static uint8_t bright;
static uint8_t flash;
static uint8_t attrValue = 7;
static uint8_t attrMask = 0xFF;
static uint8_t depth = 0;

static uint16_t scrMaxX = 255;
static uint16_t scrMaxY = 175;

void VID_SetInk (uint8_t value)
{
	ink = value;
	if (ink == 8)
	{
		attrMask  |=  0x07;
		attrValue &= ~0x07;
	}
	else
	{
		attrMask &= ~0x07;
		attrValue = (attrValue & ~0x07) | (ink & 0x07);
	}
}

void VID_SetPaper (uint8_t value)
{
	paper = value;
	if (paper == 8)
	{
		attrMask  |=  0x38;
		attrValue &= ~0x38;
	}
	else
	{
		attrMask &= ~0x38;
		attrValue = (attrValue & ~0x38) | ((paper & 0x07) << 3);
	}
}

void VID_SetBright (uint8_t value)
{
	bright = value;
	if (bright == 8)
	{
		attrMask  |=  0x40;
		attrValue &= ~0x40;
	}
	else
	{
		attrMask &= ~0x40;
		attrValue = (attrValue & ~0x40) | ((bright & 0x01) << 6);
	}
}

void VID_SetFlash (uint8_t value)
{
	flash = value;
	if (flash == 8)
	{
		attrMask  |=  0x80;
		attrValue &= ~0x80;
	}
	else
	{
		attrMask &= ~0x80;
		attrValue = (attrValue & ~0x80) | ((flash & 0x01) << 7);
	}
}

void VID_MoveTo (uint16_t x, uint16_t y)
{
	if (x > scrMaxX) x = scrMaxX;
	if (y > scrMaxY) y = scrMaxY;
	cursorX = x;
	cursorY = y;
}

void VID_MoveBy (int16_t x, int16_t y)
{
	if (x < 0 && cursorX < -x)
		cursorX = 0;
	else
		cursorX += x;
	if (y < 0 && cursorY < -y)
		cursorY = 0;
	else
		cursorY += y;
	if (cursorX > scrMaxX)
		cursorX = scrMaxX;
	if (cursorY > scrMaxY)
		cursorY = scrMaxY;
}

void VID_SetAttribute()
{
	uint16_t offset = (cursorY >> 3) * 32 + (cursorX >> 3);
	attributes[offset] = (attributes[offset] & attrMask) | attrValue;
}

void VID_DrawPixel(uint8_t color)
{
	uint8_t* ptr = bitmap + cursorY * 32 + (cursorX >> 3);
	uint8_t mask = 0x80 >> (cursorX & 7);

	if (color == 255)
		*ptr ^= mask;
	else if (color == 0)
		*ptr &= ~mask;
	else
		*ptr |= mask;

	VID_SetAttribute();
}

static void InnerFill (int16_t minX, int16_t maxX, int16_t y, uint8_t* pattern, int direction)
{
	#ifdef DISABLE_FILL
	uint8_t* p = bitmap + y * 32 + (minX >> 3);
	*p |= 0x80 >> (minX & 7);
	return;
	#endif

	int16_t x = minX;
	uint8_t* ptr = bitmap + y * 32 + (x >> 3);
	uint8_t mask = 0x80 >> (x & 7);
	uint8_t cy = y & 7;
	uint8_t* attr = attributes + (y >> 3) * 32 + (x >> 3);

	if ((*ptr & mask) != 0)
	{
		// Find left wall, to the right of the initial position
		while ((*ptr & mask) != 0)
		{
			x++;
			if (x > maxX) return;
			mask >>= 1;
			if (mask == 0)
			{
				attr++;
				mask = 0x80;
				ptr++;
			}
		}

		minX  = x;
	}
	else
	{
		// Find left wall
		while ((*ptr & mask) == 0)
		{
			if (x == 0) break;
			x--;
			mask <<= 1;
			if (mask == 0)
			{
				attr--;
				mask = 0x01;
				ptr--;
			}
		}
		minX = x+1;

		if ((*ptr & mask) != 0)
		{
			x++;
			mask >>= 1;
			if (mask == 0)
			{
				attr++;
				mask = 0x80;
				ptr++;
			}
		}
	}

	*attr = (*attr & attrMask) | attrValue;

	// Fill to the right
	while ((*ptr & mask) == 0)
	{
		*ptr |= (pattern[cy] & mask);
		x++;
		if (x > scrMaxX) break;
		mask >>= 1;
		if (mask == 0)
		{
			attr++;
			*attr = (*attr & attrMask) | attrValue;
			mask = 0x80;
			ptr++;
		}
	}
	if (x-1 > maxX)
		maxX = x-1;

	switch (direction)
	{
		case -1:
			if (y > 0)
				InnerFill(minX, x-1, y-1, pattern, -1);
			break;

		case 1:
			if (y < scrMaxY)
				InnerFill(minX, x-1, y+1, pattern, 1);
			break;
	}

	if (maxX > x)
	{
		// Find next hole to fill
		while ((*ptr & mask) != 0)
		{
			if (x == scrMaxX) return;
			x++;
			mask >>= 1;
			if (mask == 0)
			{
				attr++;
				mask = 0x80;
				ptr++;
			}
		}
		if (x <= maxX)
			InnerFill(x, maxX, y, pattern, direction);
	}
}

void VID_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
	if (x < 0 || x > scrMaxX || y < 0 || y > scrMaxY)
		return;

	uint8_t* ptr = bitmap + y * 32 + (x >> 3);
	uint8_t mask = 0x80 >> (x & 7);
	if (color == 0)
		*ptr &= ~mask;
	else if (color == 255)
		*ptr ^= mask;
	else
		*ptr |= mask;
	
	uint16_t offset = (y >> 3) * 32 + (x >> 3);
	attributes[offset] = (attributes[offset] & attrMask) | attrValue;
}

void VID_PatternFill(int16_t x, int16_t y, int pattern)
{
	uint8_t ch[8];

	x += cursorX;
	if (x < 0) x = 0;
	if (x > scrMaxX) x = scrMaxX;
	y += cursorY;
	if (y < 0) y = 0;
	if (y > scrMaxY) y = scrMaxY;

	if (pattern >= 0)
		MemCopy(ch, vectorGraphicsRAM + charset + 8*pattern, 8);
	else
		for (int n = 0; n < 8; n++) ch[n] = 0xFF;

	InnerFill(x, x, y, ch, 1);
	InnerFill(x, x, y-1, ch, -1);
}

void VID_DrawLine(int16_t incx, int16_t incy, uint8_t color)
{
	int x = cursorX;
	int y = cursorY;
    int stepx, stepy;
    int signx, signy;
    int max, inc;
    int cur, pas;

	int destx = x + incx;
	int desty = y + incy;

	if (incx < 0)
		signx = -1, incx = -incx;
	else
		signx = 1;

	if (incy < 0)
		signy = -1, incy = -incy;
	else
		signy = 1;

	if (incy >= incx)
	{
		inc = incx;
		max = incy;
		stepx = 0;
		stepy = signy;
	}
	else
	{
		inc = incy;
		max = incx;
		stepx = signx;
		stepy = 0;
	}

	cur = max / 2;
	pas = 0;

	while (pas++ < max)
	{
		cur += inc;
		if (cur >= max)
		{
			y += signy;
			x += signx;
			cur -= max;
		}
		else
		{
			y += stepy;
			x += stepx;
		}
		VID_DrawPixel(x, y, color);
	}
	
	cursorX = x;
	cursorY = y;
}


void VID_AttributeFill (uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
	if (x1 < x0) {
		uint8_t tmp = x0;
		x0 = x1;
		x1 = tmp;
	}
	if (y1 < y0) {
		uint8_t tmp = y0;
		y0 = y1;
		y1 = tmp;
	}

	uint8_t maxrow = scrMaxY/8;
	uint8_t maxcol = scrMaxX/8;
	if (x0 > maxcol || y0 > maxrow)
		return;
	if (x1 > maxcol)
		x1 = maxcol;
	if (y1 > maxrow)
		y1 = maxrow;

	uint8_t* ptr = attributes + y0 * 32 + x0;
	uint8_t* end = attributes + y1 * 32 + x1;
	for (int y = y0; y <= y1; y++)
	{
		uint8_t* line = ptr;
		for (int x = x0; x <= x1; x++, ptr++)
			*ptr = (*ptr & attrMask) | attrValue;
		ptr = line + 32;
	}
}

bool DrawVectorSubroutine (uint8_t picno, int scale, bool flipX, bool flipY)
{
	if (vectorGraphicsRAM == 0 || picno >= count)
		return false;

	if (scale == 0) 
		scale = 8;

	if (++depth == 10) 
	{
		depth--;
		return false;
	}

	#ifdef TRACE_VECTOR
	DebugPrintf("\nEntering subroutine %d\n", picno);
	#endif

	uint16_t offset = read16LE(vectorGraphicsRAM + table + picno * 2);
	const uint8_t* ptr = vectorGraphicsRAM + offset;
	if (*ptr == 7)
		return false;

	for (;;)
	{
		switch (*ptr & 7)
		{
			case 0:	// PLOT
				#ifdef TRACE_VECTOR
				DebugPrintf("%02X PLOT    %02X %02X\n", ptr[0], ptr[1], ptr[2]);
				#endif

				VID_MoveTo(flipX ? scrMaxX - ptr[1] : ptr[1], flipY ? ptr[2] : scrMaxY - ptr[2]);
				switch (*ptr & 0x18)		// Inverse + Over flags
				{
					case 0x00: VID_DrawPixel(1); break;
					case 0x08: VID_DrawPixel(255); break;
					case 0x10: VID_DrawPixel(0); break;
					case 0x18: VID_SetAttribute(); break;
				}
				ptr += 3;
				break;
			case 1: // LINE
			{
				uint8_t c = *ptr;
				int x, y;
				if ((c & 0x20) != 0)
				{
					x = (ptr[1] >> 4) & 0x0F;
					y = ptr[1] & 0x0F;

					#ifdef TRACE_VECTOR
					if ((c & 0x18) == 0x18)
						DebugPrintf("%02X MOVE    %02X     ", ptr[0], ptr[1]);
					else
						DebugPrintf("%02X LINE    %02X     ", ptr[0], ptr[1]);
					#endif
					ptr += 2;
				}
				else
				{
					x = ptr[1];
					y = ptr[2];

					#ifdef TRACE_VECTOR
					if ((c & 0x18) == 0x18)
						DebugPrintf("%02X MOVE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
					else
						DebugPrintf("%02X LINE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
					#endif
					ptr += 3;
				}
				if (flipX) x = -x;
				if (!flipY) y = -y;
				if (c & 0x40) x = -x;
				if (c & 0x80) y = -y;
				y = y * scale / 8;
				x = x * scale / 8;

				#ifdef TRACE_VECTOR
				DebugPrintf("       (%d, %d)\n", x, y);
				#endif
				switch (c & 0x18)		// Inverse + Over flags
				{
					case 0x00: VID_DrawLine(x, y, 1); break;
					case 0x08: VID_DrawLine(x, y, 255); break;
					case 0x10: VID_DrawLine(x, y, 0); break;
					case 0x18: VID_MoveBy(x, y); break;
				}
				break;
			}
			case 2:
				if ((*ptr & 0x30) == 0x10)			// BLOCK
				{
					uint8_t x0 = ptr[3];
					uint8_t y0 = ptr[4];
					uint8_t x1 = x0+ptr[2];
					uint8_t y1 = y0+ptr[1];
					
					#ifdef TRACE_VECTOR
					DebugPrintf("%02X BLOCK   %02X %02X %02X %02X   (%d,%d)-(%d,%d)\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], x0, y0, x1, y1);
					#endif
					VID_AttributeFill(x0, y0, x1, y1);
					ptr += 5;
				}
				else if ((*ptr & 0x20) != 0)	// SHADE
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("%02X SHADE   %02X %02X %02X\n", ptr[0], ptr[1], ptr[2], ptr[3]);
					#endif

					int x = ptr[1];
					int y = ptr[2];
					uint8_t shade = ptr[3];
					if (flipX) x = -x;
					if (!flipY) y = -y;
					if (*ptr & 0x40) x = -x;
					if (*ptr & 0x80) y = -y;
					VID_PatternFill(x, y, shade);
					ptr += 4;
				}
				else							// FILL
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("%02X FILL    %02X %02X\n", ptr[0], ptr[1], ptr[2]);
					#endif
					
					int x = ptr[1];
					int y = ptr[2];
					if (flipX) x = -x;
					if (!flipY) y = -y;
					if (*ptr & 0x40) x = -x;
					if (*ptr & 0x80) y = -y;
					VID_PatternFill(x, y, -1);
					ptr += 3;
				}
				break;
			case 3: // GOSUB
			{
				int scale = (*ptr >> 3) & 0x7;
				bool sflipX = (*ptr & 0x40) != 0;
				bool sflipY = (*ptr & 0x80) != 0;

				#ifdef TRACE_VECTOR
				DebugPrintf("%02X GOSUB   %02X            (scale: %d, flipX: %d, flipY: %d)\n", ptr[0], ptr[1], scale, sflipX, sflipY);
				fflush(stdout);
				#endif

				if (flipX) sflipX = !sflipX;
				if (flipY) sflipY = !sflipY;
				DrawVectorSubroutine(ptr[1], scale, sflipX, sflipY);
				ptr += 2;
				break;
			}
			case 4: // TEXT
			{
				#ifdef TRACE_VECTOR
				DebugPrintf("%02X TEXT    %02X %02X %02X\n", ptr[0], ptr[1], ptr[2], ptr[3]);
				#endif

				ptr += 4;
				break;
			}
			case 5: // PAPER
			{
				#ifdef TRACE_VECTOR
				DebugPrintf("%02X PAPER                 ", ptr[0]);
				#endif

				if (*ptr & 0x80)
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("(bright set to %d)\n", (*ptr >> 3) & 0x0F);
					#endif
					VID_SetBright((*ptr >> 3) & 0x0F);
				}
				else
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("(paper set to %d)\n", (*ptr >> 3) & 0x0F);
					#endif
					VID_SetPaper((*ptr >> 3) & 0x0F);
				}
				ptr++;
				break;
			}
			case 6: // INK
			{
				#ifdef TRACE_VECTOR
				DebugPrintf("%02X INK                   ", ptr[0]);
				#endif

				if (*ptr & 0x80)
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("(flash set to %d)\n", (*ptr >> 3) & 0x0F);
					#endif
					VID_SetFlash((*ptr >> 3) & 0x0F);
				}
				else
				{
					#ifdef TRACE_VECTOR
					DebugPrintf("(ink set to %d)\n", (*ptr >> 3) & 0x0F);
					#endif
					VID_SetInk((*ptr >> 3) & 0x0F);
				}
				ptr++;
				break;
			}
			case 7:
			{
				#ifdef TRACE_VECTOR
				DebugPrintf("%02X RETURN\n\n", ptr[0]);
				#endif

				depth--;
				return true;
			}
		}
	}
}

bool DDB_LoadVectorGraphics (DDB_Machine target, const uint8_t* data, size_t size)
{
	switch (target)
	{
		case DDB_MACHINE_SPECTRUM:
		{
			if (size < 65536)
				return false;

			vectorGraphicsRAM = data;

			// There is actually an unknown data section at 0xFFED
			start   = read16LE(data + 0xFFEF);
			table   = read16LE(data + 0xFFF1);
			windefs = read16LE(data + 0xFFF3);
			unknown = read16LE(data + 0xFFF5);
			charset = read16LE(data + 0xFFF7);
			coltab  = read16LE(data + 0xFFF9);
			ending  = read16LE(data + 0xFFFB);
			count   = data[0xFFFD];

			if (table < start || charset < start || coltab < start)
				return false;
			if (windefs + 5*count > size)
				return false;
			if (data[windefs + 5*count] != 0xFF)
			{
				// This is mangled in Templos
				// The reason is not understood yet (?)
				windefs = (windefs & 0xFF) | (data[0xFFF2] << 8);
				if (windefs + 5*count > size)
					return false;
				if (data[windefs + 5*count] != 0xFF)
					return false;
			}

			VID_SetCharset(data + charset);
			return true;
		}

		default:
			return false;
	}
}

bool DDB_HasVectorPicture (uint8_t picno)
{
	if (picno >= count)
		return false;

	uint16_t offset = read16LE(vectorGraphicsRAM + table + picno * 2);
	const uint8_t *ptr = vectorGraphicsRAM + offset;
	if (*ptr == 7)
		return false;

	return true;
}

bool DDB_DrawVectorPicture (uint8_t picno)
{
	if (picno >= count)
		return false;

	const uint8_t* win = vectorGraphicsRAM + windefs + 5*picno;
	VID_SetInk(win[0] & 0x07);
	VID_SetPaper((win[0] >> 3) & 0x07);
	VID_SetBright(0);
	VID_SetFlash(0);
	if ((win[0] & 0x80) == 0x80)
	{
		int row = win[1];
		int col = win[2];
		int height = win[3];
		int width = win[4];
		VID_Clear(col*6, row*8, width*6, height*8, 0);
		int col8 = (col*6) / 8;
		int width8 = (col*6+width*6 + 5) / 8 - col8;
		VID_AttributeFill(col8, row, col8+width8-1, row+height-1);
	}
	cursorX = 0;
	cursorY = scrMaxY;
	bool result = DrawVectorSubroutine(picno, 0, false, false);
	// VID_SaveDebugBitmap();
	return result;
}

#endif