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

void VID_ClearST (int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	(void)mode;
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

void VID_ScrollST (int x, int y, int w, int h, int lines, uint8_t paper)
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

void VID_BlitST(uint16_t* dstBase, const uint16_t* srcBase, uint32_t srcStride, int x, int y, int w, int h)
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

#endif