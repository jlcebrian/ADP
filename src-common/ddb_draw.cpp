#if HAS_DRAWSTRING

#include <ddb.h>
#include <ddb_data.h>
#include <ddb_vid.h>
#include <os_bito.h>
#include <os_lib.h>
#include <os_file.h>
#include <stdio.h>

#define TRACE_VECTOR
// #define DISABLE_FILL

uint32_t CPC_Colors[27] = {
	0xFF000000, 0xFF000080, 0xFF0000FF, 0xFF800000, 0xFF800080, 0xFF8000FF, 0xFFFF0000, 0xFFFF0080, 0xFFFF00FF,
	0xFF008000, 0xFF008080, 0xFF0080FF, 0xFF808000, 0xFF808080, 0xFF8080FF, 0xFFFF8000, 0xFFFF8080, 0xFFFF80FF,
	0xFF00FF00, 0xFF00FF80, 0xFF00FFFF, 0xFF80FF00, 0xFF80FF80, 0xFF80FFFF, 0xFFFFFF00, 0xFFFFFF80, 0xFFFFFFFF,
};

extern void VID_SaveDebugBitmap();

static const uint8_t* vectorGraphicsRAM = 0;
static DDB_Machine vectorGraphicsMachine = DDB_MACHINE_SPECTRUM;

static uint16_t spare;
static uint16_t start;
static uint16_t table;
static uint16_t windefs;
static uint16_t unknown;
static uint16_t chset;
static uint16_t coltab;
static uint16_t extra;
static uint16_t ending;
static uint16_t count;

// TODO: Move drawing functions to a separate file
extern uint8_t* bitmap;
extern uint8_t* attributes;
extern uint8_t* graphicsBuffer;
extern uint16_t screenWidth;

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
static uint8_t  stride  = 32;
static bool     pixelMode = false;
static uint8_t  transparentColor = 8;

void VID_SetInk (uint8_t value)
{
	uint8_t mask = transparentColor - 1;

	ink = value;
	if (ink == transparentColor)
	{
		attrMask  |=  mask;
		attrValue &= ~mask;
	}
	else
	{
		attrMask &= ~mask;
		attrValue = (attrValue & ~mask) | (ink & mask);
	}
}

void VID_SetPaper (uint8_t value)
{
	uint8_t shift = transparentColor == 8 ? 3 : 4;
	uint8_t mask = (transparentColor - 1) << shift;

	paper = value;
	if (paper == transparentColor)
	{
		attrMask  |=  mask;
		attrValue &= ~mask;
	}
	else
	{
		attrMask &= ~mask;
		attrValue = (attrValue & ~mask) | ((paper << shift) & mask);
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
	uint16_t offset = (cursorY >> 3) * stride + (cursorX >> 3);
	attributes[offset] = (attributes[offset] & attrMask) | attrValue;
}

int VID_GetPixel(int16_t x, int16_t y)
{
	if (x < 0 || x > scrMaxX || y < 0 || y > scrMaxY)
		return 0;

	if (pixelMode)
		return graphicsBuffer[screenWidth*y + x];

	uint8_t* ptr = bitmap + y * stride + (x >> 3);
	uint8_t mask = 0x80 >> (x & 7);
	return (*ptr & mask) != 0 ? 1 : 0;
}

int VID_GetPixel()
{
	return VID_GetPixel(cursorX, cursorY);
}

void VID_DrawPixel(uint8_t color)
{
	if (pixelMode)
	{
		graphicsBuffer[screenWidth*cursorY + cursorX] = color;
		return;
	}

	uint8_t* ptr = bitmap + cursorY * stride + (cursorX >> 3);
	uint8_t mask = 0x80 >> (cursorX & 7);

	if (color == 255)
		*ptr ^= mask;
	else if (color == 0)
		*ptr &= ~mask;
	else
		*ptr |= mask;

	VID_SetAttribute();
}

static void GenericInnerFill (int16_t minX, int16_t maxX, int16_t y, uint8_t* pattern, int direction, uint8_t color)
{
	#ifdef DISABLE_FILL
	VID_DrawPixel(minX, y, 15);
	return;
	#endif

	int16_t  x    = minX;
	uint8_t *ptr  = graphicsBuffer + screenWidth * y + x;

	if (*ptr != color)
	{
		// Find left wall, to the right of the initial position
		while (*ptr != color)
		{
			x++;
			if (x > maxX) return;
			ptr++;
		}
		minX  = x;
	}
	else
	{
		// Find left wall
		while (*ptr == color)
		{
			if (x == 0) break;
			x--;
			ptr--;
		}
		minX = x+1;
		if (*ptr != color)
		{
			x++;
			ptr++;
		}
	}

	// Fill to the right
	while (*ptr == color)
	{
		*ptr = (pattern[y & 0x07] & (0x80 >> (x & 7))) ? ink : paper;
		x++;
		if (x > scrMaxX) break;
		ptr++;
	}
	if (x-1 > maxX)
		maxX = x-1;

	if (direction == -1 && y > 0)
		GenericInnerFill(minX, x-1, y-1, pattern, -1, color);
	else if (direction == 1 && y < scrMaxY)
		GenericInnerFill(minX, x-1, y+1, pattern, 1, color);

	if (maxX > x)
	{
		// Find next hole to fill
		while (*ptr != color)
		{
			if (x == scrMaxX) return;
			x++;
			ptr++;
		}
		if (x <= maxX)
			GenericInnerFill(x, maxX, y, pattern, direction, color);
	}
}

static void SpectrumInnerFill (int16_t minX, int16_t maxX, int16_t y, uint8_t* pattern, int direction)
{
	#ifdef DISABLE_FILL
	uint8_t* p = bitmap + y * stride + (minX >> 3);
	*p |= 0x80 >> (minX & 7);
	return;
	#endif

	int16_t x = minX;
	uint8_t* ptr = bitmap + y * stride + (x >> 3);
	uint8_t mask = 0x80 >> (x & 7);
	uint8_t cy = y & 7;
	uint8_t* attr = attributes + (y >> 3) * stride + (x >> 3);

	if ((*ptr & mask) != 0)
	{
		// Find left wall, to the right of the initial position
		while ((*ptr & mask) != 0)
		{
			x++;
			if (x > maxX) {
				return;
			}
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

	if (direction == -1 && y > 0)
		SpectrumInnerFill(minX, x-1, y-1, pattern, -1);
	else if (direction == 1 && y < scrMaxY)
		SpectrumInnerFill(minX, x-1, y+1, pattern, 1);

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
			SpectrumInnerFill(x, maxX, y, pattern, direction);
	}
}

void VID_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
	if (x < 0 || x > scrMaxX || y < 0 || y > scrMaxY)
		return;

	if (pixelMode)
	{
		graphicsBuffer[screenWidth*y + x] = color;
		return;
	}

	uint8_t* ptr = bitmap + y * stride + (x >> 3);
	uint8_t mask = 0x80 >> (x & 7);
	if (color == 0)
		*ptr &= ~mask;
	else if (color == 255)
		*ptr ^= mask;
	else
		*ptr |= mask;

	uint16_t offset = (y >> 3) * stride + (x >> 3);
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
		MemCopy(ch, vectorGraphicsRAM + chset + 8*pattern, 8);
	else
		for (int n = 0; n < 8; n++) ch[n] = 0xFF;

	if (pixelMode)
	{
		int color = VID_GetPixel(x, y);
		GenericInnerFill(x, x, y, ch, 1, color);
		if (y > 0)
			GenericInnerFill(x, x, y-1, ch, -1, color);
	}
	else
	{
		int color = VID_GetPixel(x, y);
		if (color)
			return;
		SpectrumInnerFill(x, x, y, ch, 1);
		if (y > 0)
			SpectrumInnerFill(x, x, y-1, ch, -1);
	}
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
	if (pixelMode)
		return;

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

	uint8_t* ptr = attributes + y0 * stride + x0;
	uint8_t* end = attributes + y1 * stride + x1;
	for (int y = y0; y <= y1; y++)
	{
		uint8_t* line = ptr;
		for (int x = x0; x <= x1; x++, ptr++)
			*ptr = (*ptr & attrMask) | attrValue;
		ptr = line + stride;
	}
}

void VID_Draw8x8Character (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	if (!attributes)
		return;

	uint8_t* ptr = charset + (ch << 3);
	uint8_t* out = bitmap + y * stride + (x >> 3);
	uint8_t  rot = x & 7;
	uint8_t* attr = attributes + (y >> 3) * stride + (x >> 3);
	uint8_t xattr = 0;
	uint8_t width = 8;
	uint8_t paperShift = 4;

	if (screenMachine == DDB_MACHINE_SPECTRUM)
	{
		xattr = (ink & 0x30) << 2;			
		ink &= 7;
		paper &= 7;
		paperShift = 3;
	}

	uint8_t curAttr = ink | (paper << paperShift) | xattr;

	if (paper == 255)
	{
		*attr = (*attr & 0x37) | ink | xattr;
		if (rot > 8-width)
			attr[1] = (attr[1] & 0x37) | ink | xattr;
	}
	else
	{
		paper &= 7;
		*attr = curAttr;
		if (rot > 8-width)
			attr[1] = *attr;
	}

	for (int line = 0; line < 8; line++)
	{
		uint8_t* sav = out;
		uint8_t mask = 0x80 >> rot;
		for (int col = 0; col < 8; col++)
		{
			if ((ptr[line] & (0x80 >> col)))
				*out |= mask;
			else if (paper != 255)
				*out &= ~mask;
			mask >>= 1;
			if (mask == 0)
			{
				mask = 0x80;
				out++;
			}
		}
		out = sav + stride;
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

	switch (vectorGraphicsMachine)
	{
		default:
			return false;

		case DDB_MACHINE_C64:
			if (*ptr == 0x02)
				return false;
			for (;;)
			{
				switch (*ptr & 0x07)
				{
					case 0:
						// TODO: THIS IS GUESSWORK AND PROBABLY WRONG !!!!!
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X INK %d\n", ptr[0], ptr[0] >> 3);
						#endif
						VID_SetInk(*ptr >> 3);
						ptr += 1;
						break;

					case 1:
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X PAPER %d\n", ptr[0], ptr[0] >> 3);
						#endif
						VID_SetPaper(*ptr >> 3);
						ptr += 1;
						break;

					case 2:		// RETURN
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X RETURN\n\n", ptr[0]);
						#endif

						depth--;
						return true;

					case 3: // GOSUB
					{
						int scale = (*ptr >> 3) & 0x7;
						bool sflipX = (*ptr & 0x80) != 0;
						bool sflipY = (*ptr & 0x40) != 0;

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

					case 4:		// PLOT
					{
						int x = ptr[1] | ((ptr[0] & 0x08) << 5);
						int y = ptr[2];
						if (flipX) x = scrMaxX - x;
						if (flipY) y = scrMaxY - y;

						#ifdef TRACE_VECTOR
						DebugPrintf("%02X PLOT    %02X %02X         (%d, %d)\n", ptr[0], ptr[1], ptr[2], x, y);
						#endif
						VID_MoveTo(x, y);
						switch (*ptr & 0xC0)		// Inverse + Over flags
						{
							case 0x00: VID_DrawPixel(1); break;
							case 0x40: VID_DrawPixel(255); break;
							case 0x80: VID_DrawPixel(0); break;
							case 0xC0: VID_SetAttribute(); break;
						}
						ptr += 3;
						break;
					}

					case 5: // LINE
					{
						int x = ptr[1];
						int y = ptr[2];

						#ifdef TRACE_VECTOR
						if ((*ptr & 0xC0) == 0xC0)
							DebugPrintf("%02X MOVE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
						else
							DebugPrintf("%02X LINE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
						#endif
						
						if (*ptr & 0x20) x = -x;
						if (*ptr & 0x10) y = -y;
						if (flipX) x = -x;
						if (flipY) y = -y;
						y = y * scale / 8;
						x = x * scale / 8;

						#ifdef TRACE_VECTOR
						DebugPrintf("       (%d, %d)\n", x, y);
						#endif
						switch (*ptr & 0xC0)		// Inverse + Over flags
						{
							case 0x00: VID_DrawLine(x, y, 1); break;
							case 0x40: VID_DrawLine(x, y, 255); break;
							case 0x80: VID_DrawLine(x, y, 0); break;
							case 0xC0: VID_MoveBy(x, y); break;
						}
						ptr += 3;
						break;
					}

					case 6:							// FILL/SHADE
					{
						int x = ptr[1];
						int y = ptr[2];
						int p = (*ptr & 0x80) ? ptr[3] : -1;
						if (flipX) x = -x;
						if (flipY) y = -y;
						if (*ptr & 0x20) x = -x;
						if (*ptr & 0x10) y = -y;

						#ifdef TRACE_VECTOR
						if (p != -1)
							DebugPrintf("%02X SHADE   %02X %02X %02X      (%d, %d, %d)\n", ptr[0], ptr[1], ptr[2], ptr[3], x, y, p);
						else
							DebugPrintf("%02X FILL    %02X %02X         (%d, %d)\n", ptr[0], ptr[1], ptr[2], x, y);
						#endif
						VID_PatternFill(x, y, p);
						ptr += p == -1 ? 3 : 4;
						break;
					}
						
					case 7:		// BLOCK
					{
						uint8_t y0 = ptr[1];
						uint8_t x0 = ptr[2];
						uint8_t y1 = y0+ptr[3];
						uint8_t x1 = x0+ptr[4];
						
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X BLOCK   %02X %02X %02X %02X   (%d,%d)-(%d,%d)\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], x0, y0, x1, y1);
						#endif
						VID_AttributeFill(x0, y0, x1, y1);
						ptr += 5;
						break;
					}
				}
			}

		case DDB_MACHINE_CPC:
			if (*ptr == 0x40)
				return false;
			for (;;)
			{
				switch ((*ptr & 0x0E) >> 1)
				{
					case 0:		// PEN/RETURN
						if ((*ptr & 0x40) != 0)
						{
							#ifdef TRACE_VECTOR
							DebugPrintf("%02X RETURN\n\n", ptr[0]);
							#endif
							return true;
						}
						else if ((*ptr & 0x10) != 0)
						{
							int ink = ((ptr[0] >> 7) & 0x01) | ((ptr[0] << 1) & 0x02);
							#ifdef TRACE_VECTOR
							DebugPrintf("%02X PEN     %02X            (ink set to %d)\n", *ptr, ptr[1], ink);
							#endif
							VID_SetInk(ink);
							ptr++;
						}
						else
						{
							// TODO: What to do in other cases?
							#ifdef TRACE_VECTOR
							DebugPrintf("%02X ??\n\n", ptr[0]);
							#endif
							int ink = ((ptr[0] >> 7) & 0x01) | ((ptr[0] << 1) & 0x02);
							VID_SetInk(ink);
							ptr++;
						}
						break;

					case 1:		// TEXT (TODO)
					{
						uint16_t x = ptr[1] | ((ptr[0] & 0x80) << 1);
						uint16_t y = ptr[2];
						uint16_t c = ptr[3];

						#ifdef TRACE_VECTOR
						DebugPrintf("%02X TEXT                  (char %d at %d,%d)\n\n", ptr[0], c, x, y);
						#endif
						// TODO: Print character
						ptr += 4;
						break;
					}

					case 2:		// GOSUB
					{
						int  scale  = ((ptr[0] >> 6) & 0x03) | ((ptr[0] << 2) & 0x04);
						bool sflipX = (*ptr & 0x10) != 0;
						bool sflipY = (*ptr & 0x20) != 0;

						#ifdef TRACE_VECTOR
						DebugPrintf("%02X GOSUB   %02X            (scale: %d, flipX: %d, flipY: %d)\n", ptr[0], ptr[1], scale, sflipX, sflipY);
						#endif

						if (flipX) sflipX = !sflipX;
						if (flipY) sflipY = !sflipY;
						DrawVectorSubroutine(ptr[1], scale, sflipX, sflipY);
						ptr += 2;
						break;
					}

					case 3:		// PLOT
					{
						uint16_t x = ptr[1] | ((ptr[0] & 0x80) << 1);
						uint16_t y = scrMaxY - ptr[2];

						// TODO: not sure if flip affects plot!
						if (flipX) x = scrMaxX - x;
						if (flipY) y = scrMaxY - y;

						#ifdef TRACE_VECTOR
						DebugPrintf("%02X PLOT    %02X %02X         (%d,%d)\n", ptr[0], ptr[1], ptr[2], x, y);
						#endif
						VID_MoveTo(x, y);
						if ((*ptr & 0x40) != 0)
							VID_DrawPixel(ink);
						ptr += 3;
						break;
					}
					case 4: // LINE
					{
						uint8_t c = *ptr;
						int x, y;
						if ((c & 0x01) != 0)
						{
							x = ((ptr[1] >> 4) & 0x0F) | ((ptr[0] & 0x80) >> 3);
							y = ptr[1] & 0x0F;

							#ifdef TRACE_VECTOR
							if ((c & 0x40) != 0x40)
								DebugPrintf("%02X MOVE    %02X     ", ptr[0], ptr[1]);
							else
								DebugPrintf("%02X LINE    %02X     ", ptr[0], ptr[1]);
							#endif
							ptr += 2;
						}
						else
						{
							x = ptr[1] | ((ptr[0] & 0x80) << 1);
							y = ptr[2];

							#ifdef TRACE_VECTOR
							if ((c & 0x40) != 0x40)
								DebugPrintf("%02X MOVE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
							else
								DebugPrintf("%02X LINE    %02X %02X  ", ptr[0], ptr[1], ptr[2]);
							#endif
							ptr += 3;
						}
						if (flipX) x = -x;
						if (!flipY) y = -y;
						if (c & 0x20) x = -x;
						if (c & 0x10) y = -y;
						y = y * scale / 8;
						x = x * scale / 8;

						#ifdef TRACE_VECTOR
						DebugPrintf("       (%d, %d)\n", x, y);
						#endif
						switch (c & 0x40)
						{
							case 0x40: VID_DrawLine(x, y, ink); break;
							case 0x00: VID_MoveBy(x, y); break;
						}
						break;
					}
					case 5:		// FILL
					{						
						int x = ptr[1] | ((ptr[0] & 0x80) << 1);
						int y = ptr[2];
						if (flipX) x = -x;
						if (!flipY) y = -y;
						if (*ptr & 0x20) x = -x;
						if (*ptr & 0x10) y = -y;

						#ifdef TRACE_VECTOR
						DebugPrintf("%02X FILL    %02X %02X         (%d,%d)\n", ptr[0], ptr[1], ptr[2], x, y);
						#endif
						VID_PatternFill(x, y, -1);
						ptr += 3;
						break;
					}
					case 6:		// SHADE
					{
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X SHADE   %02X %02X %02X\n", ptr[0], ptr[1], ptr[2], ptr[3]);
						#endif
						
						int cink  = ink;
						int sink  = ink;
						int spaper = ((ptr[0] & 0xC0) >> 6);
						int x = ptr[1] | ((ptr[0] & 0x80) << 1);
						int y = ptr[2];
						if (flipX) x = -x;
						if (!flipY) y = -y;
						if (*ptr & 0x20) x = -x;
						if (*ptr & 0x10) y = -y;
						if (*ptr & 0x01) {
							sink = spaper;
							spaper = cink;
						}
						VID_SetInk(sink);
						VID_SetPaper(spaper);
						VID_PatternFill(x, y, ptr[3]);
						VID_SetInk(cink);
						ptr += 4;
						break;
					}
					case 7:		// BLOCK
					{
						// TODO
						ptr += 5;
						break;
					}
				}
			}
			return true;

		case DDB_MACHINE_SPECTRUM:
		case DDB_MACHINE_MSX:
			if (*ptr == 7)
				return false;
			for (;;)
			{
				switch (*ptr & 7)
				{
					case 0:	// PLOT
					{
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
					}
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

						int code = ptr[1];
						int col  = ptr[2];
						int row  = ptr[3];
						VID_Draw8x8Character(col*8, row*8, code, ink, paper);

						ptr += 4;
						break;
					}
					case 5: // PAPER
					{
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X PAPER                 ", ptr[0]);
						#endif

						if (transparentColor == 8)
						{
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
						}
						else
						{
							#ifdef TRACE_VECTOR
							DebugPrintf("(paper set to %d)\n", (*ptr >> 3) & 0x1F);
							#endif
							VID_SetPaper((*ptr >> 3) & 0x1F);
						}
						ptr++;
						break;
					}
					case 6: // INK
					{
						#ifdef TRACE_VECTOR
						DebugPrintf("%02X INK                   ", ptr[0]);
						#endif

						if (transparentColor == 8)
						{
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
						}
						else
						{
							#ifdef TRACE_VECTOR
							DebugPrintf("(ink set to %d)\n", (*ptr >> 3) & 0x1F);
							#endif
							VID_SetInk((*ptr >> 3) & 0x1F);
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
			return true;
	}
}

bool DDB_HasVectorPicture (uint8_t picno)
{
	if (picno >= count)
		return false;

	uint16_t offset = read16LE(vectorGraphicsRAM + table + picno * 2);
	const uint8_t *ptr = vectorGraphicsRAM + offset;

	switch (vectorGraphicsMachine)
	{
		default:
			return (*ptr != 2);
		case DDB_MACHINE_C64:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 6*picno;
			return (*win == 0) && (*ptr != 0x02);
		}
		case DDB_MACHINE_CPC:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 8*picno;
			return (*win != 0) && (*ptr != 0x40);
		}
		case DDB_MACHINE_MSX:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 6*picno;
			return (*win & 0x80) && (*ptr != 7);
		}
		case DDB_MACHINE_SPECTRUM:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 5*picno;
			return (*win & 0x80) && (*ptr != 7);
		}
	}
}

bool DDB_DrawVectorPicture (uint8_t picno)
{
	if (picno >= count)
	{
		printf("DDB_DrawVectorPicture: picno %d out of range (0-%d)\n", picno, count-1);
		return false;
	}

	switch (vectorGraphicsMachine)
	{
		case DDB_MACHINE_C64:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 6*picno;
			if (win[0] == 0)
			{
				int row    = win[2];
				int col    = win[3];
				int height = win[4];
				int width  = win[5];

				VID_SetInk(win[1] & 0x0F);
				VID_SetPaper((win[1] >> 4) & 0x0F);
				VID_Clear(col*8, row*8, width*8, height*8, 0);
			}
			else
			{
				// Subroutine
				return false;
			}
			cursorX = 0;
			cursorY = scrMaxY;

			bool result = DrawVectorSubroutine(picno, 0, false, false);
			// VID_SaveDebugBitmap();
			return result;
		}

		case DDB_MACHINE_CPC:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 8*picno;
			if (win[0] == 0)
			{
				// Subroutine
				return false;
			}
			else
			{
				if (win[1] < 27) VID_SetPaletteColor32(1, CPC_Colors[win[1] & 0x1F]);
				if (win[2] < 27) VID_SetPaletteColor32(2, CPC_Colors[win[2] & 0x1F]);
				if (win[3] < 27) VID_SetPaletteColor32(3, CPC_Colors[win[3] & 0x1F]);

				int row    = win[4];
				int col    = win[5];
				int height = win[6];
				int width  = win[7];
				VID_Clear(col*8, row*8, width*8, height*8, 0);
			}
			cursorX = 0;
			cursorY = scrMaxY;
			ink     = 1;

			bool result = DrawVectorSubroutine(picno, 0, false, false);
			// VID_SaveDebugBitmap();
			return result;
		}

		case DDB_MACHINE_MSX:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 6*picno;
			if ((win[0] & 0x80) == 0x80)
			{
				VID_SetInk(win[1] & 0x0F);
				VID_SetPaper((win[1] >> 4) & 0x0F);
				VID_SetBright(0);
				VID_SetFlash(0);
				
				int row = win[2];
				int col = win[3];
				int height = win[4];
				int width = win[5];
				VID_Clear(col*6, row*8, width*6, height*8, 0);
				int col8 = (col*6) / 8;
				int width8 = (col*6+width*6 + 5) / 8 - col8;
				VID_AttributeFill(col8, row, col8+width8-1, row+height-1);
			}
			else
			{
				// Subroutine
				return false;
			}
			cursorX = 0;
			cursorY = scrMaxY;

			bool result = DrawVectorSubroutine(picno, 0, false, false);
			// VID_SaveDebugBitmap();
			return result;
		}
		case DDB_MACHINE_SPECTRUM:
		{
			const uint8_t* win = vectorGraphicsRAM + windefs + 5*picno;
			if ((win[0] & 0x80) == 0x80)
			{
				VID_SetInk(win[0] & 0x07);
				VID_SetPaper((win[0] >> 3) & 0x07);
				VID_SetBright(0);
				VID_SetFlash(0);
				
				int row = win[1];
				int col = win[2];
				int height = win[3];
				int width = win[4];
				VID_Clear(col*6, row*8, width*6, height*8, 0);
				int col8 = (col*6) / 8;
				int width8 = (col*6+width*6 + 5) / 8 - col8;
				VID_AttributeFill(col8, row, col8+width8-1, row+height-1);
			}
			else
			{
				// Subroutine
				// return false;
			}
			cursorX = 0;
			cursorY = scrMaxY;

			bool result = DrawVectorSubroutine(picno, 0, false, false);
			// VID_SaveDebugBitmap();
			return result;
		}
	}

	return false;
}

bool DDB_HasVectorDatabase()
{
	return vectorGraphicsRAM != 0;
}

bool DDB_WriteVectorDatabase(const char* filename)
{
	if (vectorGraphicsRAM == 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	File* file = File_Create(filename);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_CREATING_FILE);
		return false;
	}
	
	DebugPrintf("Writing vector RAM 0x%04X to 0x%04X\n", start, ending);
	if (File_Write(file, vectorGraphicsRAM + start, ending - start + 1) != ending - start + 1)
	{
		File_Close(file);
		DDB_SetError(DDB_ERROR_WRITING_FILE);
		return false;
	}
	File_Close(file);
	return true;
}

bool DDB_LoadVectorGraphics (DDB_Machine target, const uint8_t* data, size_t size)
{
	vectorGraphicsMachine = target;

	switch (target)
	{
		case DDB_MACHINE_C64:
		{
			if (size < 65536)
				return false;

			vectorGraphicsRAM = data;
			start   = read16LE(data + 0xCBEF);
			table   = read16LE(data + 0xCBF1);
			windefs = read16LE(data + 0xCBF3);
			unknown = read16LE(data + 0xCBF5);
			chset   = read16LE(data + 0xCBF7);;
			coltab  = read16LE(data + 0xCBF9);
			ending  = read16LE(data + 0xCBFB);
			count   = data[0xCBFD];

			if (table < start || chset < start || coltab < start)
				return false;
			if (windefs + 6*count > size)
				return false;
			if (data[windefs + 6*count] != 0xFF)
				return false;
			if (ending != 0xFFFF)
				return false;
			
			ending  = 0xCBFF;
			scrMaxX = 319;
			scrMaxY = 199;
			stride  = 40;
			VID_SetCharset(data + chset);
			return true;
		}

		case DDB_MACHINE_CPC:
		{
			if (size < 65536)
				return false;

			vectorGraphicsRAM = data;

			start   = read16LE(data + 0x9DEF);
			table   = read16LE(data + 0x9DF1);
			windefs = read16LE(data + 0x9DF3);
			unknown = read16LE(data + 0x9DF5);
			chset   = 0x9E00;
			coltab  = read16LE(data + 0x9DF9);
			ending  = read16LE(data + 0x9DFB);
			count   = data[0x9DFD];

			if (table < start || chset < start || coltab < start)
				return false;
			if (windefs + 8*count > size)
				return false;
			if (data[windefs + 8*count] != 0xFF)
				return false;
			if (ending != 0xFFFF)
				return false;
			
			ending  = chset + 2048 - 1;
			scrMaxX = 319;
			scrMaxY = 183;
			VID_SetCharset(data + chset);
			return true;
		}

		case DDB_MACHINE_MSX:
		{
			if (size < 65536)
				return false;

			vectorGraphicsRAM = data;

			spare   = read16LE(data + 0xAFED);
			start   = read16LE(data + 0xAFEF);
			table   = read16LE(data + 0xAFF1);
			windefs = read16LE(data + 0xAFF3);
			unknown = read16LE(data + 0xAFF5);
			chset   = read16LE(data + 0xAFF7);
			coltab  = read16LE(data + 0xAFF9);
			ending  = read16LE(data + 0xAFFB);
			count   = data[0xAFFD];

			if (table < start || chset < start || coltab < start)
				return false;
			if (windefs + 6*count > size)
				return false;
			if (data[windefs + 6*count] != 0xFF)
				return false;
			if (ending != 0xFFFF)
				return false;

			scrMaxX = 255;
			scrMaxY = 175;
			stride  = 32;
			transparentColor = 16;

			VID_SetCharset(data + chset);
			return true;
		}

		case DDB_MACHINE_SPECTRUM:
		{
			if (size < 65536)
				return false;

			vectorGraphicsRAM = data;

			spare   = read16LE(data + 0xFFED);
			start   = read16LE(data + 0xFFEF);
			table   = read16LE(data + 0xFFF1);
			windefs = read16LE(data + 0xFFF3);
			unknown = read16LE(data + 0xFFF5);
			chset   = read16LE(data + 0xFFF7);
			coltab  = read16LE(data + 0xFFF9);
			ending  = read16LE(data + 0xFFFB);
			count   = data[0xFFFD];

			if (table < start || chset < start || coltab < start)
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

			ending  = 0xFFFF;
			scrMaxX = 255;
			scrMaxY = 175;
			stride  = 32;
			transparentColor = 8;

			VID_SetCharset(data + chset);
			return true;
		}

		default:
			return false;
	}
}

#endif
