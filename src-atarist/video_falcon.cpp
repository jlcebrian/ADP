#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_pal.h>
#include <ddb_vid.h>
#include <ddb_xmsg.h>
#include <dmg.h>
#include <dmg_font.h>
#include <os_char.h>
#include <os_mem.h>

#include "video.h"

#ifdef _ATARIST

static void VID_ClearFalconPlanes(int x, int y, int w, int h, uint8_t color, int planes)
{
	uint16_t planeValue[8];
	for (int plane = 0; plane < 8; plane++)
		planeValue[plane] = (color & (1 << plane)) ? 0xFFFF : 0x0000;

	uint16_t* scr = screen + 160 * y + 8 * (x >> 4);
	int skip = x & 15;
	if (skip != 0)
	{
		uint16_t pix = (0x10000 >> (x & 15)) - 1;
		if (w < 16 - skip)
			pix &= ~((0x10000 >> ((x + w) & 15)) - 1);
		uint16_t mask = (uint16_t)~pix;
		uint16_t* out = scr;
		for (int row = 0; row < h; row++)
		{
			for (int plane = 0; plane < planes; plane++)
				out[plane] = (out[plane] & mask) | (pix & planeValue[plane]);
			out += 160;
		}

		int inc = 16 - skip;
		x += inc;
		w -= inc;
		scr += 8;
		if (w < 1)
			return;
	}

	int fullWords = w >> 4;
	for (int row = 0; row < h; row++)
	{
		uint16_t* out = scr + row * 160;
		for (int block = 0; block < fullWords; block++)
		{
			for (int plane = 0; plane < planes; plane++)
				out[plane] = planeValue[plane];
			out += 8;
		}
	}

	if (w & 15)
	{
		uint16_t pix = (uint16_t)~((0x10000 >> (w & 15)) - 1);
		uint16_t mask = (uint16_t)~pix;
		uint16_t* out = scr + 8 * fullWords;
		for (int row = 0; row < h; row++)
		{
			for (int plane = 0; plane < planes; plane++)
				out[plane] = (out[plane] & mask) | (pix & planeValue[plane]);
			out += 160;
		}
	}
}

void VID_ClearFalcon(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	VID_ClearFalconPlanes(x, y, w, h, color, mode == Clear_All ? 8 : 4);
}

void VID_ScrollFalcon(int x, int y, int w, int h, int lines, uint8_t paper)
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
	uint16_t* scr = screen + 160 * y + 8 * (x >> 4);

	int skip = x & 15;
	if (skip != 0)
	{
		uint16_t pix = (0x10000 >> (x & 15)) - 1;
		if (w < 16 - skip)
			pix &= ~((0x10000 >> ((x + w) & 15)) - 1);
		uint16_t mask = (uint16_t)~pix;
		uint16_t* inp = scr + 160 * lines;
		for (int32_t off = 0; off < scrollHeight * 160; off += 160)
		{
			uint16_t* out = scr + off;
			uint16_t* in = inp + off;
			for (int plane = 0; plane < 4; plane++)
				out[plane] = (out[plane] & mask) | (in[plane] & pix);
		}

		int inc = 16 - skip;
		x += inc;
		w -= inc;
		scr += 8;
		if (w <= 0)
		{
			VID_Clear(clearX, clearY, clearW, lines, paper);
			return;
		}
	}

	uint16_t* ptr = scr;
	uint16_t* inp = scr + 160 * lines;
	int copy = 16 * (w >> 4);
	for (int row = 0; row < scrollHeight; row++)
	{
		memcpy(ptr, inp, copy);
		ptr += 160;
		inp += 160;
	}

	if (w & 15)
	{
		uint16_t pix = (uint16_t)~((0x10000 >> (w & 15)) - 1);
		uint16_t mask = (uint16_t)~pix;
		uint16_t* out = scr + (copy >> 1);
		uint16_t* in = scr + 160 * lines + (copy >> 1);
		for (int row = 0; row < scrollHeight; row++)
		{
			for (int plane = 0; plane < 4; plane++)
				out[plane] = (out[plane] & mask) | (in[plane] & pix);
			out += 160;
			in += 160;
		}
	}

	VID_Clear(clearX, clearY, clearW, lines, paper);
}

void VID_BlitFalcon(uint16_t* dstBase, const uint16_t* srcBase, uint32_t srcStride, int x, int y, int w, int h)
{
	const uint16_t* srcPtr = srcBase;
	uint16_t* dstPtr = dstBase + y * 160 + 8 * (x >> 4);

	if (x & 15)
	{
		bool extraByte = (w & 15) != 0;
		w >>= 4;
		if (w == 0)
		{
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (const uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				for (int plane = 0; plane < 8; plane++)
					d[plane * 2 + 1] = s[plane * 2 + 0];
				dstPtr += 160;
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
				for (int plane = 0; plane < 8; plane++)
					d[plane * 2 + 1] = s[plane * 2 + 0];
				d += 16;
				for (int block = 0; block < w; block++)
				{
					for (int plane = 0; plane < 8; plane++)
						d[plane * 2 + 0] = s[plane * 2 + 1];
					s += 16;
					for (int plane = 0; plane < 8; plane++)
						d[plane * 2 + 1] = s[plane * 2 + 0];
					d += 16;
				}
				for (int plane = 0; plane < 8; plane++)
					d[plane * 2 + 0] = s[plane * 2 + 1];
				if (extraByte)
				{
					s += 16;
					for (int plane = 0; plane < 8; plane++)
						d[plane * 2 + 1] = s[plane * 2 + 0];
				}

				dstPtr += 160;
				srcPtr += srcStride;
			}
		}
	}
	else
	{
		bool extraByte = (w & 15) != 0;
		w >>= 4;
		if (w == 0)
		{
			for (int dy = 0; dy < h; dy++)
			{
				const uint8_t* s = (const uint8_t*)srcPtr;
				uint8_t* d = (uint8_t*)dstPtr;
				for (int plane = 0; plane < 8; plane++)
					d[plane * 2 + 0] = s[plane * 2 + 0];
				dstPtr += 160;
				srcPtr += srcStride;
			}
		}
		else
		{
			uint32_t rowBytes = (uint32_t)w * 16;
			if (!extraByte && rowBytes == 320)
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
						for (int plane = 0; plane < 8; plane++)
							d8[plane * 2 + 0] = s8[plane * 2 + 0];
					}
					dstPtr += 160;
					srcPtr += srcStride;
				}
			}
		}
	}
}

#endif