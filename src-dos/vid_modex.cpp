#ifdef _DOS

#include "vid_common.h"
#include "vid_modex.h"
#include "video.h"
#include <os_lib.h>

#include <ddb_scr.h>

#include <i86.h>
#include <conio.h>
#include <dos.h>
#include <stdint.h>

#define VGA_SEGMENT 0xA000
#if defined(__386__)
#define VGA_PTR(off) ((dos_ptr8)(0xA0000UL + (uint32_t)(off)))
#else
#define VGA_PTR(off) ((dos_ptr8)MK_FP(VGA_SEGMENT, (uint16_t)(off)))
#endif

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

static int32_t middleMask[4][4] =
{
	{0x102, 0x302, 0x702, 0xf02},
	{0x002, 0x202, 0x602, 0xe02},
	{0x002, 0x002, 0x402, 0xc02},
	{0x002, 0x002, 0x002, 0x802},
};

static int32_t leftMask[4]  = {0xf02, 0xe02, 0xc02, 0x802};
static int32_t rightMask[4] = {0x102, 0x302, 0x702, 0xf02};

static unsigned modeXPageSize;

static dos_ptr8 ModeX_GetPagePtr(unsigned page);
static void ModeX_PresentPage(unsigned page);
static void ModeX_CopyPage(unsigned srcPage, unsigned dstPage);
static void ModeX_ClearPage(unsigned page, uint8_t color);

static void SetBIOSMode(int mode)
{
	union REGS regs;
	regs.w.ax = mode;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
}

void ModeX_SetVideoMode(int mode)
{
	VideoModeReg* regs;
	unsigned width = 0;
	unsigned height = 0;

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

	SetBIOSMode(0x13);

	outp(0x3D4, 0x11);
	uint8_t v = inp(0x3D5);

	outp(0x3D4, 0x11);
	outp(0x3D5, v & 0x7F);

	while (regs->port != 0)
	{
		switch (regs->port)
		{
			case 0x3C0:
				inp(0x3DA);
				outp(0x3C0, regs->index | 0x20);
				outp(0x3C0, regs->value);
				break;

			case 0x3C2:
			case 0x3C3:
				outp(regs->port, regs->value);
				break;

			default:
				outp(regs->port, regs->index);
				outp(regs->port + 1, regs->value);
				break;
		}
		regs++;
	}

	outpw(0x3C4, 0x0F02);

	modeXPageSize = (width * height) >> 2;
	unsigned pageCount = 65536 / modeXPageSize;

	static const VID_CommonOps commonOps =
	{
		ModeX_GetPagePtr,
		ModeX_PresentPage,
		ModeX_CopyPage,
		ModeX_ClearPage
	};
	VID_CommonInit(width, height, modeXPageSize, width >> 2, pageCount,
		pageCount > 2 ? 2 : VID_INVALID_PAGE, &commonOps);
}

static dos_ptr8 ModeX_GetPagePtr(unsigned page)
{
	return VGA_PTR(modeXPageSize * page);
}

static void ModeX_PresentPage(unsigned page)
{
	while (inp(0x3DA) & 0x01)
		;

	uint32_t videoOffset = modeXPageSize * page;
	outpw(0x3D4, 0x0C | (videoOffset & 0xFF00));
	outpw(0x3D4, 0x0D | ((videoOffset & 0x00FF) << 8));
}

static void ModeX_CopyPage(unsigned srcPage, unsigned dstPage)
{
	if (srcPage == dstPage)
		return;

	dos_cptr8 in = ModeX_GetPagePtr(srcPage);
	dos_ptr8 out = ModeX_GetPagePtr(dstPage);
	uint8_t oldMapMask;
	uint8_t oldMode;
	uint8_t oldBitMask;

	outp(0x3C4, 0x02);
	oldMapMask = inp(0x3C5);

	outp(0x3CE, 0x05);
	oldMode = inp(0x3CF);

	outp(0x3CE, 0x08);
	oldBitMask = inp(0x3CF);

	outpw(0x3C4, 0x0F02);
	outp(0x3CE, 0x05);
	outp(0x3CF, (oldMode & 0xFC) | 0x01);
	outp(0x3CE, 0x08);
	outp(0x3CF, 0xFF);

	#if !defined(__386__)
	uint16_t savedDS = dos_get_ds();
	uint16_t savedES = dos_get_es();
	#endif
	memcpy_bytes(out, in, (int32_t)modeXPageSize);
	#if !defined(__386__)
	dos_set_ds(savedDS);
	dos_set_es(savedES);
	#endif

	outp(0x3CE, 0x08);
	outp(0x3CF, oldBitMask);
	outp(0x3CE, 0x05);
	outp(0x3CF, oldMode);
	outpw(0x3C4, 0x0200 | oldMapMask);
}

static void ModeX_ClearPage(unsigned page, uint8_t color)
{
	dos_ptr8 top = ModeX_GetPagePtr(page);
	uint32_t color32 = color | (color << 8);
	color32 |= (color32 << 16);

	outpw(0x3C4, 0xF02);
	memset32(top, color32, (int32_t)(modeXPageSize >> 2));
}

static void ModeX_BlitNativeColumns(const uint8_t* input, dos_ptr8 out, int srcPlane, int srcBand, int dstPlane, int width, int rows, int bands, int rowStride, uint16_t localLineSize);

void ModeX_BlitNativePicture(const uint8_t* input, int inputWidth, int inputHeight, int x, int y, int w, int h)
{
	if (input == NULL || inputWidth <= 0 || inputHeight <= 0)
		return;
	VID_CommonState* state = VID_CommonGetState();

	int sx = 0;
	int sy = 0;
	int bw = inputWidth;
	int bh = inputHeight;

	if (bw > w)
		bw = w;
	if (bh > h)
		bh = h;
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

	const uint16_t localLineSize = (uint16_t)state->lineSize;
	int bands = (inputWidth + 3) >> 2;
	uint32_t rowStride = (uint32_t)bands << 2;
	const uint8_t* srcBase = input + (uint32_t)sy * rowStride;
	dos_ptr8 dstBase = state->offset + y * localLineSize + (x >> 2);
	if (((x ^ sx) & 3) != 0)
	{
		DebugPrintf("FATAL: Mode X native blit called with unaligned source/destination phases (x=%d sx=%d)\n", x, sx);
		Abort();
		return;
	}

	int leftPixels = (4 - (x & 3)) & 3;
	if (leftPixels > bw)
		leftPixels = bw;

	if (leftPixels > 0)
		ModeX_BlitNativeColumns(srcBase, dstBase, sx & 3, sx >> 2, x & 3, leftPixels, bh, bands, rowStride, localLineSize);

	int middlePixels = bw - leftPixels;
	int middleBands = middlePixels >> 2;
	int rightPixels = middlePixels & 3;

	if (middleBands > 0)
	{
		int middleX = x + leftPixels;
		int middleSX = sx + leftPixels;
		int srcBand = middleSX >> 2;
		dos_ptr8 middleDstBase = state->offset + y * localLineSize + (middleX >> 2);

		outp(0x3C4, 0x02);
		for (int row = 0; row < bh; row++)
		{
			const uint8_t* srcRow = input + (uint32_t)(sy + row) * rowStride + srcBand;
			dos_ptr8 dstRow = middleDstBase + row * localLineSize;
			for (int plane = 0; plane < 4; plane++)
			{
				outp(0x3C5, 1 << plane);
				memcpy_bytes(dstRow, srcRow + plane * bands, middleBands);
			}
		}
	}

	if (rightPixels > 0)
	{
		int rightX = x + leftPixels + (middleBands << 2);
		int rightSX = sx + leftPixels + (middleBands << 2);
		dos_ptr8 rightDstBase = state->offset + y * localLineSize + (rightX >> 2);
		ModeX_BlitNativeColumns(srcBase, rightDstBase, rightSX & 3, rightSX >> 2, rightX & 3, rightPixels, bh, bands, rowStride, localLineSize);
	}
}

void ModeX_ClearRect(int x, int y, int width, int height, uint8_t color)
{
	VID_CommonState* state = VID_CommonGetState();
	int32_t i;
	int32_t x1 = x;
	int32_t x2 = x + width - 1;
	int32_t y1 = y;
	int32_t y2 = y + height - 1;

	if (x1 < state->minx) x1 = state->minx;
	if (x2 > state->maxx) x2 = state->maxx;
	if (y1 < state->miny) y1 = state->miny;
	if (y2 > state->maxy) y2 = state->maxy;

	if (y2 < y1 || x2 < x1)
		return;

	int32_t leftBand = x1 >> 2;
	int32_t rightBand = x2 >> 2;
	int32_t leftBit = x1 & 3;
	int32_t rightBit = x2 & 3;

	dos_ptr8 top;
	dos_ptr8 where;

	if (leftBand == rightBand)
	{
		int32_t mask = middleMask[leftBit][rightBit];
		outpw(0x3C4, mask);

		top = state->offset + (state->lineSize * y1) + leftBand;
		for (i = y1; i <= y2; i++)
		{
			*top = color;
			top += state->lineSize;
		}
		return;
	}

	int32_t mask = leftMask[leftBit];
	outpw(0x3C4, mask);

	top = state->offset + (state->lineSize * y1) + leftBand;
	where = top;
	for (i = y1; i <= y2; i++)
	{
		*where = color;
		where += state->lineSize;
	}
	top++;

	int32_t bands = rightBand - (leftBand + 1);
	if (bands > 0)
	{
		outpw(0x3C4, 0xF02);

		int32_t bands32 = bands >> 2;
		bands &= 0x03;

		if (bands32 > 0)
		{
			uint32_t color32 = color | (color << 8);
			color32 |= (color32 << 16);

			where = top;
			for (i = y1; i <= y2; i++)
			{
				memset32(where, color32, bands32);
				where += state->lineSize;
			}
			top += bands32 * 4;
		}
		if (bands > 0)
		{
			where = top;
			for (i = y1; i <= y2; i++)
			{
				memset8(where, color, bands);
				where += state->lineSize;
			}
			top += bands;
		}
	}

	mask = rightMask[rightBit];
	outpw(0x3C4, mask);

	where = top;
	for (i = y1; i <= y2; i++)
	{
		*where = color;
		where += state->lineSize;
	}
}

void ModeX_DrawCharacter(int x, int y, uint8_t c, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	const uint16_t localLineSize = (uint16_t)state->lineSize;
	dos_ptr8 ptr = state->offset + localLineSize * y + (x >> 2);
	int mask = 1 << (x & 3);
	int32_t nextX = x + charWidth[c];
	const uint8_t* data = charset + 8 * c;
	int pixel = 0x0080;

	outp(0x3C4, 0x02);

	x = nextX - x;
	if (x <= 0)
		return;

	if (paper == 255)
	{
		do
		{
			dos_ptr8 cptr = ptr;
			const uint8_t* cdata = data;

			outp(0x3C5, mask);

			int n = 8;
			do
			{
				if (*data & pixel)
					*ptr = ink;
				ptr += localLineSize;
				data++;
			}
			while (--n);

			data = cdata;
			ptr = cptr;

			mask <<= 1;
			ptr += (mask >> 4);
			mask |= (mask >> 4);
			mask &= 0x0F;
			pixel >>= 1;
		}
		while (--x);
		return;
	}

	int dec = localLineSize * 8;
	do
	{
		outp(0x3C5, mask);

		int n = 8;
		do
		{
			*ptr = (*data & pixel) ? ink : paper;
			ptr += localLineSize;
			data++;
		}
		while (--n);

		data -= 8;
		ptr -= dec;

		mask <<= 1;
		ptr += (mask >> 4);
		mask |= (mask >> 4);
		mask &= 0x0F;
		pixel >>= 1;
	}
	while (--x);
}

static void ModeX_CopyColumnsUp(dos_cptr8 in, dos_ptr8 out, int startPlane, int width, int rows, uint16_t localLineSize)
{
	if (width <= 0 || rows <= 0)
		return;
	VID_CommonState* state = VID_CommonGetState();

	int32_t mask = 1 << startPlane;
	int32_t plane = startPlane;
	const int inc = localLineSize;
	const int indec = state->lineSize * rows;
	const int outdec = state->lineSize * rows;

	outp(0x3C4, 0x02);
	outp(0x3CE, 0x04);

	int cx = width;
	do
	{
		outp(0x3C5, mask);
		outp(0x3CF, plane);

		int n = rows;
		do
		{
			*out = *in;
			out += inc;
			in += inc;
		}
		while (--n);

		in -= indec;
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

static void ModeX_CopyBandsUp(dos_cptr8 in, dos_ptr8 out, int bands, int rows, uint16_t localLineSize)
{
	if (bands <= 0 || rows <= 0)
		return;

	uint8_t oldMapMask;
	uint8_t oldMode;
	uint8_t oldBitMask;

	outp(0x3C4, 0x02);
	oldMapMask = inp(0x3C5);

	outp(0x3CE, 0x05);
	oldMode = inp(0x3CF);

	outp(0x3CE, 0x08);
	oldBitMask = inp(0x3CF);

	outpw(0x3C4, 0x0F02);
	outp(0x3CE, 0x05);
	outp(0x3CF, (oldMode & 0xFC) | 0x01);
	outp(0x3CE, 0x08);
	outp(0x3CF, 0xFF);

	#if !defined(__386__)
	uint16_t savedDS = dos_get_ds();
	uint16_t savedES = dos_get_es();
	#endif
	for (int row = 0; row < rows; row++)
	{
		memcpy_bytes(out, in, bands);
		in += localLineSize;
		out += localLineSize;
	}
	#if !defined(__386__)
	dos_set_ds(savedDS);
	dos_set_es(savedES);
	#endif

	outp(0x3CE, 0x08);
	outp(0x3CF, oldBitMask);
	outp(0x3CE, 0x05);
	outp(0x3CF, oldMode);
	outpw(0x3C4, 0x0200 | oldMapMask);
}

static void ModeX_BlitNativeColumns(const uint8_t* input, dos_ptr8 out, int srcPlane, int srcBand, int dstPlane, int width, int rows, int bands, int rowStride, uint16_t localLineSize)
{
	if (width <= 0 || rows <= 0)
		return;

	int mask = 1 << dstPlane;

	outp(0x3C4, 0x02);
	do
	{
		const uint8_t* in = input + srcPlane * bands + srcBand;
		dos_ptr8 dst = out;

		outp(0x3C5, mask);

		int n = rows;
		do
		{
			*dst = *in;
			dst += localLineSize;
			in += rowStride;
		}
		while (--n);

		mask <<= 1;
		out += (mask >> 4);
		mask |= (mask >> 4);
		mask &= 0x0F;

		if (srcPlane < 3)
			srcPlane++;
		else
		{
			srcPlane = 0;
			srcBand++;
		}
	}
	while (--width);
}

void ModeX_Scroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	const uint16_t localLineSize = (uint16_t)state->lineSize;

	h -= lines;
	if (w <= 0 || h <= 0)
		return;

	const int startPlane = x & 3;
	int leftPixels = (4 - startPlane) & 3;
	if (leftPixels > w)
		leftPixels = w;

	int middlePixels = w - leftPixels;
	int middleBands = middlePixels >> 2;
	int rightPixels = middlePixels & 3;

	dos_cptr8 src = state->offset + localLineSize * (y + lines) + (x >> 2);
	dos_ptr8 dst = state->offset + localLineSize * y + (x >> 2);

	if (leftPixels > 0)
		ModeX_CopyColumnsUp(src, dst, startPlane, leftPixels, h, localLineSize);

	if (middleBands > 0)
	{
		const int middleX = x + leftPixels;
		dos_cptr8 middleSrc = state->offset + localLineSize * (y + lines) + (middleX >> 2);
		dos_ptr8 middleDst = state->offset + localLineSize * y + (middleX >> 2);
		ModeX_CopyBandsUp(middleSrc, middleDst, middleBands, h, localLineSize);
	}

	if (rightPixels > 0)
	{
		const int rightX = x + leftPixels + (middleBands << 2);
		dos_cptr8 rightSrc = state->offset + localLineSize * (y + lines) + (rightX >> 2);
		dos_ptr8 rightDst = state->offset + localLineSize * y + (rightX >> 2);
		ModeX_CopyColumnsUp(rightSrc, rightDst, rightX & 3, rightPixels, h, localLineSize);
	}

	ModeX_ClearRect(x, y + h, w, lines, paper);
}

#endif
