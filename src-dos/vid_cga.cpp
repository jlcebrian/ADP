#ifdef _DOS

#include "vid_cga.h"
#include "vid_common.h"
#include "video.h"
#include <ddb.h>
#include <ddb_scr.h>
#include <os_mem.h>
#include <os_lib.h>

#include <i86.h>
#include <conio.h>
#include <dos.h>
#include <stdint.h>

#define CGA_SEGMENT 0xB800
#define CGA_WIDTH 320
#define CGA_HEIGHT 200
#define CGA_LINE_BYTES 80
#define CGA_PAGE_SIZE (CGA_LINE_BYTES * CGA_HEIGHT)
#define CGA_PAGE_COUNT 2

#if defined(__386__)
#define CGA_PTR(off) ((dos_ptr8)(0xB8000UL + (uint32_t)(off)))
#else
#define CGA_PTR(off) ((dos_ptr8)MK_FP(CGA_SEGMENT, (uint16_t)(off)))
#endif

static bool cgaRedPalette;

static dos_ptr8 CGA_GetPagePtr(unsigned page);
static void CGA_PresentPage(unsigned page);
static void CGA_CopyPage(unsigned srcPage, unsigned dstPage);
static void CGA_ClearPage(unsigned page, uint8_t color);
static void CGA_SetTarget(SCR_Operation op, bool front);
static void CGA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
static void CGA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper);
static void CGA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
static void CGA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h);
static void CGA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h);
static uint16_t CGA_PhysicalOffsetForRow(int y);
static bool CGA_InitTables(void);
static dos_ptr8 CGA_GetRowPtr(unsigned pageIndex, int y);
static uint8_t CGA_GetRowPixel(dos_ptr8 row, int x);
static void CGA_SetRowPixel(dos_ptr8 row, int x, uint8_t color);

static uint8_t* cgaColorPattern;
static uint8_t* cgaGlyphSetMask;
static uint8_t* cgaTextByteCount;
static uint8_t* cgaTextCoverMask;
static int8_t* cgaTextCxBase;
static const uint8_t cgaClipFromPixel[4] = { 0xFF, 0x3F, 0x0F, 0x03 };
static const uint8_t cgaClipToPixelEnd[4] = { 0x00, 0xC0, 0xF0, 0xFC };

#define CGA_GLYPH_CXBASE_MIN (-3)
#define CGA_GLYPH_CXBASE_MAX 7
#define CGA_GLYPH_CXBASE_COUNT (CGA_GLYPH_CXBASE_MAX - CGA_GLYPH_CXBASE_MIN + 1)
#define CGA_TEXT_SHAPE_COUNT (4 * 9)
#define CGA_TEXT_SHAPE_STRIDE 3

static VID_Adapter cgaAdapter =
{
	{
		CGA_WIDTH,
		CGA_HEIGHT,
		6,
		8,
		2,
		4,
		ImageMode_CGA,
		1
	},
	{
		CGA_GetPagePtr,
		CGA_PresentPage,
		0,
		CGA_CopyPage,
		CGA_ClearPage,
		CGA_SetTarget,
		CGA_Clear,
		CGA_Scroll,
		CGA_DrawTextSpan,
		CGA_BlitNativeImage,
		CGA_BlitIndexedImage
	}
};

static void CGA_SetBIOSMode(int mode)
{
	union REGS regs;
	regs.w.ax = mode;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
}

bool CGA_IsAvailable()
{
	union REGS regs;
#if defined(__386__)
	int386(0x11, &regs, &regs);
#else
	int86(0x11, &regs, &regs);
#endif
	uint16_t display = (regs.w.ax >> 4) & 0x03;
	return display == 1 || display == 2 || display == 3;
}

void CGA_SetPaletteRed(bool red)
{
	cgaRedPalette = red;
	outp(0x3D8, 0x0A);
	outp(0x3D9, red ? 0x00 : 0x20);
}

static uint8_t CGA_GetPixel(unsigned pageIndex, int x, int y)
{
	uint8_t packed = *CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + (x >> 2));
	int shift = 6 - ((x & 3) << 1);
	return (packed >> shift) & 0x03;
}

static void CGA_SetPixel(unsigned pageIndex, int x, int y, uint8_t color)
{
	dos_ptr8 ptr = CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + (x >> 2));
	int shift = 6 - ((x & 3) << 1);
	uint8_t mask = (uint8_t)(0x03 << shift);
	*ptr = (uint8_t)((*ptr & ~mask) | ((color & 0x03) << shift));
}

static uint16_t CGA_PhysicalOffsetForRow(int y)
{
	return (uint16_t)((y & 1 ? 0x2000 : 0) + ((y >> 1) * CGA_LINE_BYTES));
}

static bool CGA_InitTables(void)
{
	if (cgaColorPattern != 0 && cgaGlyphSetMask != 0 && cgaTextByteCount != 0 &&
		cgaTextCoverMask != 0 && cgaTextCxBase != 0)
		return true;

	uint16_t textShapeBytes = CGA_TEXT_SHAPE_COUNT * CGA_TEXT_SHAPE_STRIDE;
	uint8_t* tableBlock = Allocate<uint8_t>("CGA text tables",
		4 + (CGA_GLYPH_CXBASE_COUNT * 256) + CGA_TEXT_SHAPE_COUNT +
		textShapeBytes + textShapeBytes, false);
	if (tableBlock == 0)
		return false;

	cgaColorPattern = tableBlock;
	cgaGlyphSetMask = tableBlock + 4;
	cgaTextByteCount = cgaGlyphSetMask + (CGA_GLYPH_CXBASE_COUNT * 256);
	cgaTextCoverMask = cgaTextByteCount + CGA_TEXT_SHAPE_COUNT;
	cgaTextCxBase = (int8_t*)(cgaTextCoverMask + textShapeBytes);

	for (int color = 0; color < 4; color++)
		cgaColorPattern[color] = (uint8_t)(color * 0x55);

	for (int cxBase = CGA_GLYPH_CXBASE_MIN; cxBase <= CGA_GLYPH_CXBASE_MAX; cxBase++)
	{
		int idx = cxBase - CGA_GLYPH_CXBASE_MIN;
		for (int bits = 0; bits < 256; bits++)
		{
			uint8_t setMask = 0;
			for (int pixel = 0; pixel < 4; pixel++)
			{
				int cx = cxBase + pixel;
				if (cx >= 0 && cx < 8 && ((bits & (0x80 >> cx)) != 0))
					setMask |= (uint8_t)(0x03 << (6 - (pixel << 1)));
			}
			cgaGlyphSetMask[(idx << 8) | bits] = setMask;
		}
	}

	for (int phase = 0; phase < 4; phase++)
	{
		for (int width = 0; width <= 8; width++)
		{
			int shape = phase * 9 + width;
			int byteCount = width == 0 ? 0 : ((phase + width + 3) >> 2);
			cgaTextByteCount[shape] = (uint8_t)byteCount;
			for (int n = 0; n < CGA_TEXT_SHAPE_STRIDE; n++)
			{
				uint8_t cover = 0;
				int cxBase = (n << 2) - phase;
				for (int pixel = 0; pixel < 4; pixel++)
				{
					int cx = cxBase + pixel;
					if (cx >= 0 && cx < width)
						cover |= (uint8_t)(0x03 << (6 - (pixel << 1)));
				}
				cgaTextCoverMask[shape * CGA_TEXT_SHAPE_STRIDE + n] = cover;
				cgaTextCxBase[shape * CGA_TEXT_SHAPE_STRIDE + n] = (int8_t)cxBase;
			}
		}
	}

	return true;
}

static dos_ptr8 CGA_GetRowPtr(unsigned pageIndex, int y)
{
	return CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y));
}

static uint8_t CGA_GetRowPixel(dos_ptr8 row, int x)
{
	uint8_t packed = row[x >> 2];
	int shift = 6 - ((x & 3) << 1);
	return (packed >> shift) & 0x03;
}

static void CGA_SetRowPixel(dos_ptr8 row, int x, uint8_t color)
{
	dos_ptr8 ptr = row + (x >> 2);
	int shift = 6 - ((x & 3) << 1);
	uint8_t mask = (uint8_t)(0x03 << shift);
	*ptr = (uint8_t)((*ptr & ~mask) | ((color & 0x03) << shift));
}

bool CGA_SetVideoMode(DDB_ScreenMode mode)
{
	CGA_SetBIOSMode(mode == ScreenMode_CGA ? 0x04 : 0x05);
	CGA_SetPaletteRed(cgaRedPalette);
	if (!CGA_InitTables())
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	VID_CommonInit(&cgaAdapter, CGA_PAGE_SIZE, CGA_LINE_BYTES, CGA_PAGE_COUNT, VID_INVALID_PAGE);
	CGA_ClearPage(0, 0);
	CGA_ClearPage(1, 0);
	CGA_PresentPage(0);
	return true;
}

static dos_ptr8 CGA_GetPagePtr(unsigned page)
{
	return page < CGA_PAGE_COUNT ? CGA_PTR((uint16_t)(page * CGA_PAGE_SIZE)) : 0;
}

static void CGA_PresentPage(unsigned page)
{
	if (page >= CGA_PAGE_COUNT)
		return;

	if (page == 0)
		return;

	for (int y = 0; y < CGA_HEIGHT; y++)
		memcpy_bytes(CGA_PTR(CGA_PhysicalOffsetForRow(y)),
			CGA_PTR((uint16_t)(page * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y)),
			CGA_LINE_BYTES);
}

static void CGA_PresentRect(unsigned page, int x, int y, int w, int h)
{
	if (page >= CGA_PAGE_COUNT)
		return;
	if (page == 0)
		return;

	int x1 = x;
	int y1 = y;
	int x2 = x + w;
	int y2 = y + h;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 > CGA_WIDTH) x2 = CGA_WIDTH;
	if (y2 > CGA_HEIGHT) y2 = CGA_HEIGHT;
	if (x2 <= x1 || y2 <= y1)
		return;

	int leftByte = x1 >> 2;
	int rightByte = (x2 + 3) >> 2;
	int bytes = rightByte - leftByte;
	for (int row = y1; row < y2; row++)
		memcpy_bytes(CGA_PTR(CGA_PhysicalOffsetForRow(row) + leftByte),
			CGA_PTR((uint16_t)(page * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(row) + leftByte),
			bytes);
}

static void CGA_PresentActiveRectIfFront(int x, int y, int w, int h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static uint8_t CGA_GetLogicalPixel(unsigned pageIndex, int x, int y)
{
	return CGA_GetPixel(pageIndex, x, y);
}

static uint8_t CGA_GetLogicalByte(unsigned pageIndex, int byteX, int y)
{
	return *CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + byteX);
}

static void CGA_SetLogicalByte(unsigned pageIndex, int byteX, int y, uint8_t value)
{
	*CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + byteX) = value;
}

static void CGA_SetLogicalPixel(unsigned pageIndex, int x, int y, uint8_t color)
{
	CGA_SetPixel(pageIndex, x, y, color);
}

static uint8_t CGA_Pack4(uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3)
{
	return (uint8_t)(((c0 & 0x03) << 6) |
		((c1 & 0x03) << 4) |
		((c2 & 0x03) << 2) |
		(c3 & 0x03));
}

static void CGA_PresentByteSpan(unsigned pageIndex, int byteX, int y, int bytes)
{
	if (pageIndex == 0 || bytes <= 0)
		return;
	memcpy_bytes(CGA_PTR(CGA_PhysicalOffsetForRow(y) + byteX),
		CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + byteX),
		bytes);
}

static void CGA_CopyNativeBytes(unsigned pageIndex, int byteX, int y, const uint8_t* src, int bytes)
{
	if (bytes <= 0)
		return;
	memcpy_bytes(CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + byteX), src, bytes);
}

static void CGA_CopyLogicalBytes(unsigned pageIndex, int dstByteX, int dstY, int srcByteX, int srcY, int bytes)
{
	if (bytes <= 0)
		return;
	dos_ptr8 dst = CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(dstY) + dstByteX);
	dos_ptr8 src = CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(srcY) + srcByteX);
	if (dst <= src)
	{
		for (int i = 0; i < bytes; i++)
			dst[i] = src[i];
	}
	else
	{
		for (int i = bytes - 1; i >= 0; i--)
			dst[i] = src[i];
	}
}

static void CGA_FillLogicalBytes(unsigned pageIndex, int byteX, int y, int bytes, uint8_t value)
{
	if (bytes <= 0)
		return;
	memset8(CGA_PTR((uint16_t)(pageIndex * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y) + byteX), value, bytes);
}

static void CGA_CopyPage(unsigned srcPage, unsigned dstPage)
{
	if (srcPage >= CGA_PAGE_COUNT || dstPage >= CGA_PAGE_COUNT || srcPage == dstPage)
		return;
	for (int y = 0; y < CGA_HEIGHT; y++)
		memcpy_bytes(CGA_PTR((uint16_t)(dstPage * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y)),
			CGA_PTR((uint16_t)(srcPage * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y)),
			CGA_LINE_BYTES);
}

static void CGA_ClearPage(unsigned page, uint8_t color)
{
	if (page >= CGA_PAGE_COUNT)
		return;
	uint8_t c = (uint8_t)((color & 0x03) * 0x55);
	for (int y = 0; y < CGA_HEIGHT; y++)
		memset8(CGA_PTR((uint16_t)(page * CGA_PAGE_SIZE) + CGA_PhysicalOffsetForRow(y)), c, CGA_LINE_BYTES);
}

static void CGA_SetTarget(SCR_Operation op, bool front)
{
	(void)op;
	VID_CommonSetActiveBuffer(front);
}

static void CGA_ClearRect(int x, int y, int w, int h, uint8_t color)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= CGA_PAGE_COUNT)
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

	uint8_t packedColor = (uint8_t)((color & 0x03) * 0x55);
	int leftByte = x1 >> 2;
	int rightByte = (x2 - 1) >> 2;
	int leftPixel = x1 & 3;
	int rightPixelEnd = x2 & 3;
	uint8_t leftMask = cgaClipFromPixel[leftPixel];
	uint8_t rightMask = cgaClipToPixelEnd[rightPixelEnd];

	for (int py = y1; py < y2; py++)
	{
		dos_ptr8 row = CGA_GetRowPtr(page, py);
		int rowLeftByte = leftByte;
		int rowRightByte = rightByte;
		if (leftByte == rightByte)
		{
			uint8_t mask = rightPixelEnd == 0 ? leftMask : (uint8_t)(leftMask & rightMask);
			row[rowLeftByte] = (uint8_t)((row[rowLeftByte] & ~mask) | (packedColor & mask));
			continue;
		}

		if (leftPixel != 0)
		{
			row[rowLeftByte] = (uint8_t)((row[rowLeftByte] & ~leftMask) | (packedColor & leftMask));
			rowLeftByte++;
		}

		if (rowLeftByte < rowRightByte)
			memset8(row + rowLeftByte, packedColor, rowRightByte - rowLeftByte);

		if (rightPixelEnd != 0)
		{
			row[rowRightByte] = (uint8_t)((row[rowRightByte] & ~rightMask) | (packedColor & rightMask));
		}
		else
		{
			row[rowRightByte] = packedColor;
		}
	}
}

static void CGA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	(void)mode;
	CGA_ClearRect(x, y, w, h, color);
	CGA_PresentActiveRectIfFront(x, y, w, h);
}

static void CGA_DrawCharacter(int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= CGA_PAGE_COUNT)
		return;

	const uint8_t* data = charset + 8 * ch;
	int width = charWidth[ch];
	if (width <= 0)
		return;

	int x1 = x;
	int x2 = x + width;
	if (x1 < state->minx) x1 = state->minx;
	if (x2 > state->maxx + 1) x2 = state->maxx + 1;
	if (x2 <= x1)
		return;

	ink &= 0x03;
	int x1Pixel = x1 & 3;
	int x2Pixel = x2 & 3;
	uint8_t firstClipMask = cgaClipFromPixel[x1Pixel];
	uint8_t lastClipMask = cgaClipToPixelEnd[x2Pixel];
	bool opaque = paper != 255;
	uint8_t inkPattern = cgaColorPattern[ink];
	uint8_t paperPattern = cgaColorPattern[paper & 0x03];
	for (int cy = 0; cy < 8; cy++)
	{
		int py = y + cy;
		if (py < state->miny || py > state->maxy)
			continue;

		dos_ptr8 row = CGA_GetRowPtr(page, py);
		uint8_t bits = data[cy];
		int firstByte = x1 >> 2;
		int lastByte = (x2 - 1) >> 2;
		for (int byteX = firstByte; byteX <= lastByte; byteX++)
		{
			int cxBase = (byteX << 2) - x;
			uint8_t clipMask = 0xFF;
			if (byteX == firstByte && x1Pixel != 0)
				clipMask = (uint8_t)(clipMask & firstClipMask);
			if (byteX == lastByte && x2Pixel != 0)
				clipMask = (uint8_t)(clipMask & lastClipMask);

			uint8_t setMask = 0;
			if (cxBase >= CGA_GLYPH_CXBASE_MIN && cxBase <= CGA_GLYPH_CXBASE_MAX)
			{
				int idx = cxBase - CGA_GLYPH_CXBASE_MIN;
				setMask = (uint8_t)(cgaGlyphSetMask[(idx << 8) | bits] & clipMask);
			}

			uint8_t packed = row[byteX];
			if (opaque)
			{
				uint8_t coverMask = clipMask;
				if (coverMask == 0)
					continue;
				uint8_t out = (uint8_t)((inkPattern & setMask) |
					(paperPattern & (uint8_t)(coverMask & ~setMask)));
				packed = (uint8_t)((packed & ~coverMask) | out);
			}
			else if (setMask != 0)
			{
				packed = (uint8_t)((packed & ~setMask) | (inkPattern & setMask));
			}

			row[byteX] = packed;
		}
	}
}

static void CGA_DrawTextSpanFast(unsigned page, int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	uint8_t inkPattern = cgaColorPattern[ink & 0x03];

	if (paper != 255)
	{
		uint8_t paperPattern = cgaColorPattern[paper & 0x03];
		for (int cy = 0; cy < 8; cy++)
		{
			dos_ptr8 rowBase = CGA_GetRowPtr(page, y + cy);
			int px = x;
			for (uint16_t chIndex = 0; chIndex < length; chIndex++)
			{
				uint8_t ch = text[chIndex];
				uint8_t width = charWidth[ch];
				uint8_t phase = (uint8_t)(px & 3);
				int shape = phase * 9 + width;
				dos_ptr8 row = rowBase + (px >> 2);
				uint8_t count = cgaTextByteCount[shape];
				const uint8_t* covers = cgaTextCoverMask + shape * CGA_TEXT_SHAPE_STRIDE;
				const int8_t* cxBases = cgaTextCxBase + shape * CGA_TEXT_SHAPE_STRIDE;
				uint8_t bits = charset[(ch << 3) + cy];
				for (uint8_t n = 0; n < count; n++)
				{
					uint8_t cover = covers[n];
					int idx = cxBases[n] - CGA_GLYPH_CXBASE_MIN;
					uint8_t set = (uint8_t)(cgaGlyphSetMask[(idx << 8) | bits] & cover);
					uint8_t out = (uint8_t)((inkPattern & set) |
						(paperPattern & (uint8_t)(cover & ~set)));
					if (cover == 0xFF)
						row[n] = out;
					else
						row[n] = (uint8_t)((row[n] & ~cover) | out);
				}
				px += width;
			}
		}
	}
	else
	{
		for (int cy = 0; cy < 8; cy++)
		{
			dos_ptr8 rowBase = CGA_GetRowPtr(page, y + cy);
			int px = x;
			for (uint16_t chIndex = 0; chIndex < length; chIndex++)
			{
				uint8_t ch = text[chIndex];
				uint8_t width = charWidth[ch];
				uint8_t phase = (uint8_t)(px & 3);
				int shape = phase * 9 + width;
				dos_ptr8 row = rowBase + (px >> 2);
				uint8_t count = cgaTextByteCount[shape];
				const int8_t* cxBases = cgaTextCxBase + shape * CGA_TEXT_SHAPE_STRIDE;
				uint8_t bits = charset[(ch << 3) + cy];
				for (uint8_t n = 0; n < count; n++)
				{
					int idx = cxBases[n] - CGA_GLYPH_CXBASE_MIN;
					uint8_t set = cgaGlyphSetMask[(idx << 8) | bits];
					if (set != 0)
						row[n] = (uint8_t)((row[n] & ~set) | (inkPattern & set));
				}
				px += width;
			}
		}
	}
}

static void CGA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	int startX = x;
	for (uint16_t n = 0; n < length && x < CGA_WIDTH;)
	{
		uint8_t ch = text[n];
		uint8_t width = charWidth[ch];
		uint16_t run = 0;
		int runWidth = 0;
		while (n + run < length)
		{
			uint8_t runCharWidth = charWidth[text[n + run]];
			if (runCharWidth == 0 || runCharWidth > 8)
				break;
			if (x + runWidth + runCharWidth > state->maxx + 1)
				break;
			runWidth += runCharWidth;
			run++;
		}
		if (run != 0 && page < CGA_PAGE_COUNT &&
			x >= state->minx &&
			y >= state->miny && y + 7 <= state->maxy)
		{
			CGA_DrawTextSpanFast(page, x, y, text + n, run, ink, paper);
			x += runWidth;
			n = (uint16_t)(n + run);
		}
		else
		{
			CGA_DrawCharacter(x, y, ch, ink, paper);
			x += width;
			n++;
		}
	}
	CGA_PresentActiveRectIfFront(startX, y, x - startX, 8);
}

static void CGA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || page >= CGA_PAGE_COUNT)
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

	for (int row = 0; row < h; row++)
	{
		dos_ptr8 dstRow = CGA_GetRowPtr(page, y + row);
		const uint8_t* src = pixels + (uint32_t)(sy + row) * srcW + sx;
		int col = 0;
		while (col < w && ((x + col) & 3) != 0)
		{
			CGA_SetRowPixel(dstRow, x + col, src[col]);
			col++;
		}
		int byteStart = (x + col) >> 2;
		int byteCount = 0;
		while (col + 3 < w)
		{
			dstRow[byteStart + byteCount] = CGA_Pack4(src[col], src[col + 1], src[col + 2], src[col + 3]);
			byteCount++;
			col += 4;
		}
		while (col < w)
		{
			CGA_SetRowPixel(dstRow, x + col, src[col]);
			col++;
		}
	}
	CGA_PresentActiveRectIfFront(x, y, w, h);
}

static void CGA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || srcH <= 0 || page >= CGA_PAGE_COUNT)
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

	uint32_t srcStride = ((uint32_t)srcW + 3u) >> 2;
	for (int row = 0; row < bh; row++)
	{
		dos_ptr8 dstRow = CGA_GetRowPtr(page, y + row);
		const uint8_t* srcRow = pixels + (uint32_t)(sy + row) * srcStride;
		int col = 0;
		while (col < bw && (((x + col) & 3) != 0 || ((sx + col) & 3) != 0))
		{
			uint32_t srcX = sx + col;
			uint8_t packed = srcRow[srcX >> 2];
			uint8_t color = (packed >> (6 - ((srcX & 3u) << 1))) & 0x03;
			CGA_SetRowPixel(dstRow, x + col, color);
			col++;
		}

		int bytes = 0;
		const uint8_t* srcBytes = srcRow + ((sx + col) >> 2);
		int dstByte = (x + col) >> 2;
		while (col + 3 < bw)
		{
			bytes++;
			col += 4;
		}
		if (bytes > 0)
			memcpy_bytes(dstRow + dstByte, srcBytes, bytes);

		while (col < bw)
		{
			uint32_t srcX = sx + col;
			uint8_t packed = srcRow[srcX >> 2];
			uint8_t color = (packed >> (6 - ((srcX & 3u) << 1))) & 0x03;
			CGA_SetRowPixel(dstRow, x + col, color);
			col++;
		}
	}
}

static void CGA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= CGA_PAGE_COUNT || lines <= 0)
		return;
	if (x < state->minx) x = state->minx;
	if (y < state->miny) y = state->miny;
	if (x + w > state->maxx + 1) w = state->maxx - x + 1;
	if (y + h > state->maxy + 1) h = state->maxy - y + 1;
	if (w <= 0 || h <= lines)
		return;

	if ((x & 3) == 0 && (w & 3) == 0)
	{
		int byteX = x >> 2;
		int bytes = w >> 2;
		for (int row = 0; row < h - lines; row++)
			CGA_CopyLogicalBytes(page, byteX, y + row, byteX, y + row + lines, bytes);
	}
	else
	{
		for (int row = 0; row < h - lines; row++)
		{
			dos_ptr8 dstRow = CGA_GetRowPtr(page, y + row);
			dos_ptr8 srcRow = CGA_GetRowPtr(page, y + row + lines);
			for (int col = 0; col < w; col++)
				CGA_SetRowPixel(dstRow, x + col, CGA_GetRowPixel(srcRow, x + col));
		}
	}
	CGA_ClearRect(x, y + h - lines, w, lines, paper);
	CGA_PresentActiveRectIfFront(x, y, w, h);
}

#endif
