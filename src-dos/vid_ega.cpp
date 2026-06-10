#ifdef _DOS

#include "vid_ega.h"
#include "vid_common.h"
#include "video.h"
#include <ddb.h>
#include <ddb_scr.h>
#include <os_lib.h>
#include <os_mem.h>

#include <i86.h>
#include <conio.h>
#include <dos.h>
#include <stdint.h>

#define EGA_SEGMENT 0xA000
#define EGA_WIDTH 320
#define EGA_HEIGHT 200
#define EGA_ROW_BYTES 40
#define EGA_PLANE_BYTES (EGA_ROW_BYTES * EGA_HEIGHT)
#define EGA_PAGE_SIZE (EGA_PLANE_BYTES * 4)
#define EGA_PAGE_COUNT 2
#define EGA_WRITE_MODE_UNKNOWN 0xFF
#define EGA_WRITE_MODE_COPY 0
#define EGA_WRITE_MODE_MASKED_COLOR 1

#if defined(__386__)
#define EGA_PTR(off) ((dos_ptr8)(0xA0000UL + (uint32_t)(off)))
#else
#define EGA_PTR(off) ((dos_ptr8)MK_FP(EGA_SEGMENT, (uint16_t)(off)))
#endif

static uint8_t egaWriteMode = EGA_WRITE_MODE_UNKNOWN;
static uint8_t egaWriteColor = 0xFF;
static uint8_t egaBitMask = 0xFF;
static uint8_t egaPlaneMask = 0xFF;
static uint8_t egaReadPlane = 0xFF;

static dos_ptr8 EGA_GetPagePtr(unsigned page);
static void EGA_PresentPage(unsigned page);
static void EGA_CopyPage(unsigned srcPage, unsigned dstPage);
static void EGA_ClearPage(unsigned page, uint8_t color);
static void EGA_SetTarget(SCR_Operation op, bool front);
static void EGA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
static void EGA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper);
static void EGA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
static void EGA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h);
static void EGA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h);

static VID_Adapter egaAdapter =
{
	{
		EGA_WIDTH,
		EGA_HEIGHT,
		6,
		8,
		4,
		16,
		ImageMode_Planar,
		8
	},
	{
		EGA_GetPagePtr,
		EGA_PresentPage,
		0,
		EGA_CopyPage,
		EGA_ClearPage,
		EGA_SetTarget,
		EGA_Clear,
		EGA_Scroll,
		EGA_DrawTextSpan,
		EGA_BlitNativeImage,
		EGA_BlitIndexedImage
	}
};

static void EGA_SetBIOSMode(int mode)
{
	union REGS regs;
	regs.w.ax = mode;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
}

bool EGA_IsAvailable()
{
	union REGS regs;
	regs.w.ax = 0x1200;
	regs.h.bl = 0x10;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
	return regs.h.bl != 0x10;
}

static uint16_t EGA_PageOffset(unsigned page)
{
	return (uint16_t)(page * EGA_PLANE_BYTES);
}

static dos_ptr8 EGA_RowPtr(unsigned page, int y)
{
	return EGA_PTR(EGA_PageOffset(page) + (uint16_t)(y * EGA_ROW_BYTES));
}

static void EGA_WriteSequencer(uint8_t index, uint8_t value)
{
	outp(0x3C4, index);
	outp(0x3C5, value);
}

static void EGA_WriteGraphics(uint8_t index, uint8_t value)
{
	outp(0x3CE, index);
	outp(0x3CF, value);
}

static void EGA_SelectPlaneMask(uint8_t mask)
{
	if (egaPlaneMask == mask)
		return;
	EGA_WriteSequencer(0x02, mask);
	egaPlaneMask = mask;
}

static void EGA_SelectReadPlane(uint8_t plane)
{
	if (egaReadPlane == plane)
		return;
	EGA_WriteGraphics(0x04, plane);
	egaReadPlane = plane;
}

static void EGA_SetBitMask(uint8_t mask)
{
	if (egaBitMask == mask)
		return;
	EGA_WriteGraphics(0x08, mask);
	egaBitMask = mask;
}

static void EGA_SetWriteMode0()
{
	if (egaWriteMode == EGA_WRITE_MODE_COPY)
		return;
	EGA_WriteGraphics(0x01, 0x00);
	EGA_WriteGraphics(0x03, 0x00);
	EGA_WriteGraphics(0x05, 0x00);
	EGA_WriteGraphics(0x08, 0xFF);
	egaWriteMode = EGA_WRITE_MODE_COPY;
	egaBitMask = 0xFF;
}

static void EGA_SetWriteModeMaskedColor(uint8_t color)
{
	color &= 0x0F;
	if (egaWriteMode == EGA_WRITE_MODE_MASKED_COLOR && egaWriteColor == color)
		return;
	EGA_WriteGraphics(0x00, color);
	EGA_WriteGraphics(0x01, 0x0F);
	EGA_WriteGraphics(0x03, 0x00);
	EGA_WriteGraphics(0x05, 0x00);
	EGA_SelectPlaneMask(0x0F);
	egaWriteMode = EGA_WRITE_MODE_MASKED_COLOR;
	egaWriteColor = color;
}

static uint8_t EGA_PixelMask(int x)
{
	return (uint8_t)(0x80 >> (x & 7));
}

static uint8_t EGA_LeftMask(int x)
{
	return (uint8_t)(0xFF >> (x & 7));
}

static uint8_t EGA_RightMask(int xEnd)
{
	static const uint8_t masks[8] = { 0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE };
	return masks[xEnd & 7];
}

static void EGA_WriteColorMask(unsigned page, int byteX, int y, uint8_t color, uint8_t mask)
{
	if (mask == 0)
		return;
	dos_ptr8 dst = EGA_RowPtr(page, y) + byteX;
	EGA_SetWriteMode0();
	EGA_SetBitMask(0xFF);
	for (int plane = 0; plane < 4; plane++)
	{
		uint8_t planeMask = (uint8_t)(1 << plane);
		uint8_t value = (color & planeMask) != 0 ? mask : 0;
		EGA_SelectReadPlane((uint8_t)plane);
		EGA_SelectPlaneMask(planeMask);
		if (mask == 0xFF)
			*dst = value;
		else
			*dst = (uint8_t)((*dst & ~mask) | value);
	}
	EGA_SelectPlaneMask(0x0F);
}

static uint8_t EGA_ReadPixel(unsigned page, int x, int y)
{
	uint8_t color = 0;
	dos_ptr8 ptr = EGA_RowPtr(page, y) + (x >> 3);
	uint8_t mask = EGA_PixelMask(x);
	for (int plane = 0; plane < 4; plane++)
	{
		EGA_SelectReadPlane((uint8_t)plane);
		if ((*ptr & mask) != 0)
			color |= (uint8_t)(1 << plane);
	}
	return color;
}

static void EGA_WritePixel(unsigned page, int x, int y, uint8_t color)
{
	EGA_WriteColorMask(page, x >> 3, y, color, EGA_PixelMask(x));
}

static void EGA_CopyPlaneBytes(unsigned srcPage, unsigned dstPage, int dstByteX, int dstY, int srcByteX, int srcY, int bytes)
{
	if (bytes <= 0)
		return;

	EGA_SetWriteMode0();
	for (int plane = 0; plane < 4; plane++)
	{
		EGA_SelectReadPlane((uint8_t)plane);
		EGA_SelectPlaneMask((uint8_t)(1 << plane));
		dos_ptr8 src = EGA_RowPtr(srcPage, srcY) + srcByteX;
		dos_ptr8 dst = EGA_RowPtr(dstPage, dstY) + dstByteX;
		if (dst <= src)
		{
			for (int n = 0; n < bytes; n++)
				dst[n] = src[n];
		}
		else
		{
			for (int n = bytes - 1; n >= 0; n--)
				dst[n] = src[n];
		}
	}
	EGA_SelectPlaneMask(0x0F);
}

bool EGA_SetVideoMode()
{
	EGA_SetBIOSMode(0x0D);
	egaWriteMode = EGA_WRITE_MODE_UNKNOWN;
	egaWriteColor = 0xFF;
	egaBitMask = 0xFF;
	egaPlaneMask = 0xFF;
	egaReadPlane = 0xFF;
	EGA_SetWriteModeMaskedColor(0);
	VID_CommonInit(&egaAdapter, EGA_PAGE_SIZE, EGA_ROW_BYTES, EGA_PAGE_COUNT, VID_INVALID_PAGE);
	EGA_ClearPage(0, 0);
	EGA_ClearPage(1, 0);
	EGA_PresentPage(0);
	return true;
}

static dos_ptr8 EGA_GetPagePtr(unsigned page)
{
	return page < EGA_PAGE_COUNT ? EGA_PTR(EGA_PageOffset(page)) : 0;
}

static void EGA_PresentPage(unsigned page)
{
	if (page >= EGA_PAGE_COUNT)
		return;
	if (page == 0)
		return;

	for (int y = 0; y < EGA_HEIGHT; y++)
		EGA_CopyPlaneBytes(page, 0, 0, y, 0, y, EGA_ROW_BYTES);
}

static void EGA_CopyPage(unsigned srcPage, unsigned dstPage)
{
	if (srcPage >= EGA_PAGE_COUNT || dstPage >= EGA_PAGE_COUNT || srcPage == dstPage)
		return;

	for (int y = 0; y < EGA_HEIGHT; y++)
		EGA_CopyPlaneBytes(srcPage, dstPage, 0, y, 0, y, EGA_ROW_BYTES);
}

static void EGA_ClearPage(unsigned page, uint8_t color)
{
	if (page >= EGA_PAGE_COUNT)
		return;

	EGA_SetWriteMode0();
	EGA_SetBitMask(0xFF);
	for (int plane = 0; plane < 4; plane++)
	{
		EGA_SelectPlaneMask((uint8_t)(1 << plane));
		uint8_t value = (color & (1 << plane)) != 0 ? 0xFF : 0x00;
		for (int y = 0; y < EGA_HEIGHT; y++)
			memset8(EGA_RowPtr(page, y), value, EGA_ROW_BYTES);
	}
	EGA_SelectPlaneMask(0x0F);
}

static void EGA_SetTarget(SCR_Operation op, bool front)
{
	(void)op;
	VID_CommonSetActiveBuffer(front);
}

static void EGA_ClearRect(int x, int y, int w, int h, uint8_t color)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= EGA_PAGE_COUNT)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;
	if (x1 < state->minx) x1 = state->minx;
	if (y1 < state->miny) y1 = state->miny;
	if (x2 > state->maxx + 1) x2 = state->maxx + 1;
	if (y2 > state->maxy + 1) y2 = state->maxy + 1;
	if (x2 <= x1 || y2 <= y1)
		return;

	int leftByte = x1 >> 3;
	int rightByte = (x2 - 1) >> 3;
	uint8_t c = color & 0x0F;
	uint8_t leftMask = EGA_LeftMask(x1);
	uint8_t rightMask = (x2 & 7) != 0 ? EGA_RightMask(x2) : 0xFF;

	EGA_SetWriteMode0();
	EGA_SetBitMask(0xFF);
	if (leftByte == rightByte)
	{
		uint8_t mask = (uint8_t)(leftMask & rightMask);
		for (int row = y1; row < y2; row++)
			EGA_WriteColorMask(page, leftByte, row, c, mask);
		return;
	}

	int fillStart = leftByte;
	int fillEnd = rightByte;
	if (leftMask != 0xFF)
	{
		for (int row = y1; row < y2; row++)
			EGA_WriteColorMask(page, leftByte, row, c, leftMask);
		fillStart++;
	}
	if (rightMask != 0xFF)
	{
		for (int row = y1; row < y2; row++)
			EGA_WriteColorMask(page, rightByte, row, c, rightMask);
		fillEnd--;
	}

	int bytes = fillEnd - fillStart + 1;
	if (bytes > 0)
	{
		for (int plane = 0; plane < 4; plane++)
		{
			uint8_t value = (c & (1 << plane)) != 0 ? 0xFF : 0x00;
			EGA_SelectPlaneMask((uint8_t)(1 << plane));
			for (int row = y1; row < y2; row++)
				memset8(EGA_RowPtr(page, row) + fillStart, value, bytes);
		}
		EGA_SelectPlaneMask(0x0F);
	}
}

static void EGA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	(void)mode;
	EGA_ClearRect(x, y, w, h, color);
}

static void EGA_BuildGlyphByteMasks(uint8_t bits, int charX, uint8_t width, int byteX,
	uint8_t* coverOut, uint8_t* setOut)
{
	int shift = (byteX << 3) - charX;
	uint8_t glyphMask = width >= 8 ? 0xFF : (uint8_t)(0xFF << (8 - width));
	uint8_t cover;
	uint8_t set;

	if (shift <= -8 || shift >= 8)
	{
		cover = 0;
		set = 0;
	}
	else if (shift >= 0)
	{
		cover = (uint8_t)(glyphMask << shift);
		set = (uint8_t)((bits & glyphMask) << shift);
	}
	else
	{
		cover = (uint8_t)(glyphMask >> -shift);
		set = (uint8_t)((bits & glyphMask) >> -shift);
	}
	set &= cover;
	*coverOut = cover;
	*setOut = set;
}

static void EGA_DrawCharacter(int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= EGA_PAGE_COUNT)
		return;

	int width = charWidth[ch];
	if (width <= 0)
		return;

	int x1 = x;
	int x2 = x + width;
	if (x1 < state->minx) x1 = state->minx;
	if (x2 > state->maxx + 1) x2 = state->maxx + 1;
	if (x2 <= x1)
		return;

	int firstByte = x1 >> 3;
	int lastByte = (x2 - 1) >> 3;
	uint8_t firstClip = EGA_LeftMask(x1);
	uint8_t lastClip = EGA_RightMask(x2);
	bool opaque = paper != 255;
	uint8_t inkColor = ink & 0x0F;
	uint8_t paperColor = paper & 0x0F;
	const uint8_t* data = charset + 8 * ch;
	int byteCount = lastByte - firstByte + 1;

	if (width <= 8 && byteCount <= 2)
	{
		for (int cy = 0; cy < 8; cy++)
		{
			int py = y + cy;
			if (py < state->miny || py > state->maxy)
				continue;

			uint8_t coverMask[2] = { 0, 0 };
			uint8_t setMask[2] = { 0, 0 };
			uint8_t bits = data[cy];

			for (int n = 0; n < byteCount; n++)
			{
				int byteX = firstByte + n;
				uint8_t cover;
				uint8_t set;
				EGA_BuildGlyphByteMasks(bits, x, (uint8_t)width, byteX, &cover, &set);
				if (byteX == firstByte && (x1 & 7) != 0)
					cover &= firstClip;
				if (byteX == lastByte && (x2 & 7) != 0)
					cover &= lastClip;
				set &= cover;
				coverMask[n] = cover;
				setMask[n] = set;
			}

			if ((coverMask[0] | coverMask[1]) == 0)
				continue;

			dos_ptr8 dst = EGA_RowPtr(page, py) + firstByte;
			for (int plane = 0; plane < 4; plane++)
			{
				uint8_t planeMask = (uint8_t)(1 << plane);
				bool inkBit = (inkColor & planeMask) != 0;
				bool paperBit = (paperColor & planeMask) != 0;
				EGA_SelectReadPlane((uint8_t)plane);
				EGA_SelectPlaneMask(planeMask);

				for (int n = 0; n < byteCount; n++)
				{
					uint8_t cover = coverMask[n];
					uint8_t set = setMask[n];
					if (!opaque)
					{
						if (set == 0)
							continue;
						if (set == 0xFF)
							dst[n] = inkBit ? 0xFF : 0x00;
						else if (inkBit)
							dst[n] = (uint8_t)(dst[n] | set);
						else
							dst[n] = (uint8_t)(dst[n] & ~set);
					}
					else
					{
						uint8_t value = 0;
						if (inkBit)
							value |= set;
						if (paperBit)
							value |= (uint8_t)(cover & ~set);
						if (cover == 0xFF)
							dst[n] = value;
						else if (cover != 0)
							dst[n] = (uint8_t)((dst[n] & ~cover) | value);
					}
				}
			}
		}
		EGA_SelectPlaneMask(0x0F);
		return;
	}

	for (int cy = 0; cy < 8; cy++)
	{
		int py = y + cy;
		if (py < state->miny || py > state->maxy)
			continue;

		uint8_t bits = data[cy];
		for (int byteX = firstByte; byteX <= lastByte; byteX++)
		{
			uint8_t cover = 0;
			uint8_t set = 0;
			EGA_BuildGlyphByteMasks(bits, x, (uint8_t)width, byteX, &cover, &set);
			if (byteX == firstByte && (x1 & 7) != 0)
				cover &= firstClip;
			if (byteX == lastByte && (x2 & 7) != 0)
				cover &= lastClip;
			set &= cover;

			if (opaque)
				EGA_WriteColorMask(page, byteX, py, paperColor, (uint8_t)(cover & ~set));
			if (set != 0)
				EGA_WriteColorMask(page, byteX, py, inkColor, set);
		}
	}
}

static void EGA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= EGA_PAGE_COUNT)
		return;

	ink &= 0x0F;
	bool transparent = paper == 255;
	uint8_t paperColor = paper & 0x0F;

	for (uint16_t n = 0; n < length && x < EGA_WIDTH;)
	{
		uint8_t ch = text[n];
		uint8_t width = charWidth[ch];
		if (width == 0)
		{
			n++;
			continue;
		}

		uint16_t run = 0;
		int runWidth = 0;
		while (n + run < length)
		{
			uint8_t runWidthChar = charWidth[text[n + run]];
			if (runWidthChar == 0 || runWidthChar > 8)
				break;
			if (x + runWidth + runWidthChar > state->maxx + 1)
				break;
			runWidth += runWidthChar;
			run++;
		}

		if (run != 0 &&
			x >= state->minx &&
			y >= state->miny &&
			y + 7 <= state->maxy)
		{
			struct EGA_RunGlyph
			{
				uint8_t ch;
				uint8_t shift;
				uint8_t rowByte0;
				uint8_t rowByte1;
				uint8_t cover0;
				uint8_t cover1;
				uint8_t hasSecond;
			};

			EGA_SetWriteMode0();
			EGA_SetBitMask(0xFF);
			int startByte = x >> 3;
			int endByte = (x + runWidth - 1) >> 3;
			int byteCount = endByte - startByte + 1;
			EGA_RunGlyph glyphs[EGA_WIDTH];
			uint8_t coverTemplate[EGA_ROW_BYTES];
			MemClear(coverTemplate, byteCount);

			int px = x;
			for (uint16_t chIndex = 0; chIndex < run; chIndex++)
			{
				uint8_t fastCh = text[n + chIndex];
				uint8_t fastWidth = charWidth[fastCh];
				uint8_t glyphMask = fastWidth >= 8 ? 0xFF : (uint8_t)(0xFF << (8 - fastWidth));
				uint8_t shift = (uint8_t)(px & 7);
				uint8_t cover0 = (uint8_t)(glyphMask >> shift);
				uint8_t cover1 = shift == 0 ? 0 : (uint8_t)(glyphMask << (8 - shift));
				uint8_t rowByte0 = (uint8_t)((px >> 3) - startByte);
				uint8_t rowByte1 = (uint8_t)(rowByte0 + 1);
				uint8_t hasSecond = (uint8_t)(((px + fastWidth - 1) >> 3) != (px >> 3));

				glyphs[chIndex].ch = fastCh;
				glyphs[chIndex].shift = shift;
				glyphs[chIndex].rowByte0 = rowByte0;
				glyphs[chIndex].rowByte1 = rowByte1;
				glyphs[chIndex].cover0 = cover0;
				glyphs[chIndex].cover1 = cover1;
				glyphs[chIndex].hasSecond = hasSecond;

				coverTemplate[rowByte0] |= cover0;
				if (hasSecond)
					coverTemplate[rowByte1] |= cover1;
				px += fastWidth;
			}

			for (int cy = 0; cy < 8; cy++)
			{
				uint8_t setRow[EGA_ROW_BYTES];
				MemClear(setRow, byteCount);

				for (uint16_t chIndex = 0; chIndex < run; chIndex++)
				{
					const EGA_RunGlyph* glyph = &glyphs[chIndex];
					uint8_t bits = charset[(glyph->ch << 3) + cy];
					uint8_t shift = glyph->shift;
					uint8_t set0;
					uint8_t set1;

					if (shift == 0)
					{
						set0 = bits;
						set1 = 0;
					}
					else
					{
						set0 = (uint8_t)(bits >> shift);
						set1 = (uint8_t)(bits << (8 - shift));
					}

					set0 &= glyph->cover0;
					setRow[glyph->rowByte0] |= set0;
					if (glyph->hasSecond)
					{
						set1 &= glyph->cover1;
						setRow[glyph->rowByte1] |= set1;
					}
				}

				if (!transparent)
				{
					for (int plane = 0; plane < 4; plane++)
					{
						uint8_t planeMask = (uint8_t)(1 << plane);
						bool inkBit = (ink & planeMask) != 0;
						bool paperBit = (paperColor & planeMask) != 0;
						EGA_SelectReadPlane((uint8_t)plane);
						EGA_SelectPlaneMask(planeMask);

						dos_ptr8 dst = EGA_RowPtr(page, y + cy) + startByte;
						for (int rowByte = 0; rowByte < byteCount; rowByte++)
						{
							uint8_t cover = coverTemplate[rowByte];
							if (cover == 0)
								continue;
							uint8_t set = setRow[rowByte];
							uint8_t value = 0;
							if (inkBit)
								value |= set;
							if (paperBit)
								value |= (uint8_t)(cover & ~set);
							if (cover == 0xFF)
								dst[rowByte] = value;
							else
								dst[rowByte] = (uint8_t)((dst[rowByte] & ~cover) | value);
						}
					}
				}
				else
				{
					for (int plane = 0; plane < 4; plane++)
					{
						uint8_t planeMask = (uint8_t)(1 << plane);
						bool inkBit = (ink & planeMask) != 0;
						EGA_SelectReadPlane((uint8_t)plane);
						EGA_SelectPlaneMask(planeMask);

						dos_ptr8 dst = EGA_RowPtr(page, y + cy) + startByte;
						for (int rowByte = 0; rowByte < byteCount; rowByte++)
						{
							uint8_t set = setRow[rowByte];
							if (set == 0)
								continue;
							if (set == 0xFF)
								dst[rowByte] = inkBit ? 0xFF : 0x00;
							else if (inkBit)
								dst[rowByte] = (uint8_t)(dst[rowByte] | set);
							else
								dst[rowByte] = (uint8_t)(dst[rowByte] & ~set);
						}
					}
				}
			}
			EGA_SelectPlaneMask(0x0F);
			x += runWidth;
			n = (uint16_t)(n + run);
		}
		else
		{
			EGA_DrawCharacter(x, y, ch, ink, transparent ? 255 : paperColor);
			x += width;
			n++;
		}
	}
}

static uint8_t EGA_PackIndexedPlaneByte(const uint8_t* pixels, int plane)
{
	uint8_t out = 0;
	uint8_t bit = (uint8_t)(1 << plane);
	for (int n = 0; n < 8; n++)
		if ((pixels[n] & bit) != 0)
			out |= (uint8_t)(0x80 >> n);
	return out;
}

static void EGA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || page >= EGA_PAGE_COUNT)
		return;

	int sx = 0;
	int sy = 0;
	if (x < state->minx)
	{
		sx = state->minx - x;
		w -= sx;
		x = state->minx;
	}
	if (y < state->miny)
	{
		sy = state->miny - y;
		h -= sy;
		y = state->miny;
	}
	if (x + w > state->maxx + 1)
		w = state->maxx - x + 1;
	if (y + h > state->maxy + 1)
		h = state->maxy - y + 1;
	if (w <= 0 || h <= 0)
		return;

	EGA_SetWriteMode0();
	for (int row = 0; row < h; row++)
	{
		const uint8_t* src = pixels + (uint32_t)(sy + row) * srcW + sx;
		int col = 0;
		while (col < w && ((x + col) & 7) != 0)
		{
			EGA_WritePixel(page, x + col, y + row, src[col]);
			col++;
			EGA_SetWriteMode0();
		}

		int byteX = (x + col) >> 3;
		int byteCount = (w - col) >> 3;
		for (int plane = 0; plane < 4; plane++)
		{
			EGA_SelectPlaneMask((uint8_t)(1 << plane));
			dos_ptr8 dst = EGA_RowPtr(page, y + row) + byteX;
			for (int n = 0; n < byteCount; n++)
				dst[n] = EGA_PackIndexedPlaneByte(src + col + n * 8, plane);
		}
		col += byteCount << 3;

		while (col < w)
		{
			EGA_WritePixel(page, x + col, y + row, src[col]);
			col++;
			EGA_SetWriteMode0();
		}
	}
	EGA_SelectPlaneMask(0x0F);
}

static void EGA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || srcH <= 0 || page >= EGA_PAGE_COUNT)
		return;

	int sx = 0;
	int sy = 0;
	int bw = srcW;
	int bh = srcH;
	if (bw > w) bw = w;
	if (bh > h) bh = h;
	if (x < state->minx)
	{
		sx = state->minx - x;
		bw -= sx;
		x = state->minx;
	}
	if (y < state->miny)
	{
		sy = state->miny - y;
		bh -= sy;
		y = state->miny;
	}
	if (x + bw > state->maxx + 1)
		bw = state->maxx - x + 1;
	if (y + bh > state->maxy + 1)
		bh = state->maxy - y + 1;
	if (bw <= 0 || bh <= 0)
		return;

	uint32_t srcStride = ((uint32_t)(srcW + 15) & ~15u) >> 3;
	uint32_t srcPlaneSize = srcStride * (uint32_t)srcH;

	if (((x ^ sx) & 7) != 0)
	{
		for (int row = 0; row < bh; row++)
		{
			for (int col = 0; col < bw; col++)
			{
				uint8_t color = 0;
				uint32_t srcX = sx + col;
				uint8_t mask = (uint8_t)(0x80 >> (srcX & 7));
				for (int plane = 0; plane < 4; plane++)
				{
					const uint8_t* srcPlane = pixels + srcPlaneSize * (uint32_t)plane;
					const uint8_t* srcRow = srcPlane + (uint32_t)(sy + row) * srcStride;
					if ((srcRow[srcX >> 3] & mask) != 0)
						color |= (uint8_t)(1 << plane);
				}
				EGA_WritePixel(page, x + col, y + row, color);
			}
		}
		return;
	}

	int leftPixels = (8 - (x & 7)) & 7;
	if (leftPixels > bw)
		leftPixels = bw;
	int middlePixels = bw - leftPixels;
	int middleBytes = middlePixels >> 3;
	int rightPixels = middlePixels & 7;
	int middleX = x + leftPixels;
	int srcMiddleX = sx + leftPixels;
	int rightX = middleX + (middleBytes << 3);
	int srcRightX = srcMiddleX + (middleBytes << 3);

	EGA_SetWriteMode0();
	for (int plane = 0; plane < 4; plane++)
	{
		EGA_SelectPlaneMask((uint8_t)(1 << plane));
		const uint8_t* srcPlane = pixels + srcPlaneSize * (uint32_t)plane;
		for (int row = 0; row < bh; row++)
		{
			const uint8_t* srcRow = srcPlane + (uint32_t)(sy + row) * srcStride;
			dos_ptr8 dstRow = EGA_RowPtr(page, y + row);
			if (leftPixels != 0)
			{
				uint8_t mask = EGA_LeftMask(x);
				if (leftPixels < 8 - (x & 7))
					mask &= EGA_RightMask(x + leftPixels);
				uint8_t srcValue = srcRow[sx >> 3] & mask;
				dos_ptr8 dst = dstRow + (x >> 3);
				*dst = (uint8_t)((*dst & ~mask) | srcValue);
			}
			if (middleBytes > 0)
			{
				const uint8_t* src = srcRow + (srcMiddleX >> 3);
				dos_ptr8 dst = dstRow + (middleX >> 3);
				memcpy_bytes(dst, src, middleBytes);
			}
			if (rightPixels != 0)
			{
				uint8_t mask = EGA_RightMask(rightX + rightPixels);
				uint8_t srcValue = srcRow[srcRightX >> 3] & mask;
				dos_ptr8 dst = dstRow + (rightX >> 3);
				*dst = (uint8_t)((*dst & ~mask) | srcValue);
			}
		}
	}
	EGA_SelectPlaneMask(0x0F);
}

static void EGA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= EGA_PAGE_COUNT || lines <= 0)
		return;
	if (x < state->minx) x = state->minx;
	if (y < state->miny) y = state->miny;
	if (x + w > state->maxx + 1) w = state->maxx - x + 1;
	if (y + h > state->maxy + 1) h = state->maxy - y + 1;
	if (w <= 0 || h <= lines)
		return;

	if ((x & 7) == 0 && (w & 7) == 0)
	{
		int byteX = x >> 3;
		int bytes = w >> 3;
		for (int row = 0; row < h - lines; row++)
			EGA_CopyPlaneBytes(page, page, byteX, y + row, byteX, y + row + lines, bytes);
	}
	else
	{
		for (int row = 0; row < h - lines; row++)
		{
			for (int col = 0; col < w; col++)
				EGA_WritePixel(page, x + col, y + row, EGA_ReadPixel(page, x + col, y + row + lines));
		}
	}
	EGA_ClearRect(x, y + h - lines, w, lines, paper);
}

#endif
