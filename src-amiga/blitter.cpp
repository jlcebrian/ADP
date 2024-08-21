#include <os_types.h>

#ifdef _AMIGA

#include "video.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <hardware/custom.h>
#include <hardware/blit.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <graphics/copper.h>

#define DISPLAY_PLANES 4

#define BLTCON0_ASH0_SHF 0xC
#define BLTCON0_USEA     0x0800
#define BLTCON0_USEB     0x0400
#define BLTCON0_USEC     0x0200
#define BLTCON0_USED     0x0100
#define BLTCON0_LF0_SHF  0x0
#define BLTCON1_BSH0_SHF 0xC
#define BLTCON1_TEX0_SHF 0xC
#define BLTCON1_SIGN_SHF 0x6
#define BLTCON1_AUL_SHF  0x2
#define BLTCON1_SING_SHF 0x1
#define BLTCON1_IFE      0x0008
#define BLTCON1_DESC     0x0002
#define BLTCON1_LINE     0x0001
#define BLTSIZE_H0_SHF   0x6
#define BLTSIZE_W0_SHF   0x0

#define ABS(a)    ((a) < 0   ? -(a) : (a))
#define MAX(a, b) ((a) > (b) ?  (a) : (b))
#define MIN(a, b) ((a) < (b) ?  (a) : (b))

extern volatile Custom *custom;

static inline void WaitForBlitter() 
{
	// Give blitter priority while we're waiting.
	// Also takes care of dummy DMACON access to work around Agnus bug.
	custom->dmacon = DMAF_SETCLR | DMAF_BLITHOG | DMAF_DISK;
	while (custom->dmaconr & DMAF_BLTDONE);
	custom->dmacon = DMAF_BLITHOG;
}

void BlitterCopy (
	void* src,  uint16_t srcX, uint16_t srcY, 
	void* dst,  uint16_t dstX, uint16_t dstY, 
	uint16_t w, uint16_t h, bool solid)
{
	uint16_t startX[2] = {srcX, dstX};
	uint16_t startXWord[2], endXWord[2], numWords[2], wordOffset[2];

	for (uint16_t i = 0; i < 2; ++i)
	{
		startXWord[i] = startX[i] >> 4;
		endXWord[i]   = ((startX[i] + w) + 0xF) >> 4;
		numWords[i]   = endXWord[i] - startXWord[i];
		wordOffset[i] = startX[i] & 0xF;
	}

	uint16_t widthWords = MAX(numWords[0], numWords[1]);
	int16_t  shift = (uint16_t)wordOffset[1] - (uint16_t)wordOffset[0];
	uint16_t srcMod = SCR_STRIDEB - (widthWords * 2);
	uint16_t dstMod = SCR_STRIDEB - (widthWords * 2);

	bool descending = shift < 0;

	uint16_t startXOffset = descending ? (widthWords - 1) : 0;
	uint16_t startYOffset = descending ? (h - 1) : 0;

	uint32_t srcStart = (uint32_t)src +
						((srcY + startYOffset) * SCR_STRIDEB) +
						((startXWord[0] + startXOffset) * 2);
	uint32_t dstStart = (uint32_t)dst +
						((dstY + startYOffset) * SCR_STRIDEB) +
						((startXWord[1] + startXOffset) * 2);

	uint16_t leftMask = (uint16_t)(0xFFFFU << (wordOffset[0] + MAX(0, 0x10 - (wordOffset[0] + w)))) >> wordOffset[0];
	uint16_t rightMask;

	if (widthWords == 1)
		rightMask = leftMask;
	else
		rightMask = 0xFFFFU << MIN(0x10, ((startXWord[0] + widthWords) << 4) - (srcX + w));

	WaitForBlitter();

	// A = Mask of bits inside copy region
	// B = Source data
	// C = Destination data (for region outside mask)
	// D = Destination data
	uint16_t minterm = solid ? 0xCA : 0xEA;

	custom->bltcon0 = (ABS(shift) << BLTCON0_ASH0_SHF) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED | minterm;
	custom->bltcon1 = (ABS(shift) << BLTCON1_BSH0_SHF) | (descending ? BLTCON1_DESC : 0);
	custom->bltbmod = srcMod;
	custom->bltcmod = dstMod;
	custom->bltdmod = dstMod;
	custom->bltafwm = (descending ? rightMask : leftMask);
	custom->bltalwm = (descending ? leftMask : rightMask);
	custom->bltadat = 0xFFFF;
	custom->bltbpt = (void*)srcStart;
	custom->bltcpt = (void*)dstStart;
	custom->bltdpt = (void*)dstStart;
	custom->bltsize = (h << BLTSIZE_H0_SHF) | widthWords;
}

void BlitterRect (void* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool set_bits)
{
	uint16_t startXWord = x >> 4;
	uint16_t endXWord = ((x + width) + 0xF) >> 4;
	uint16_t widthWords = endXWord - startXWord;
	uint16_t wordOffset = x & 0xF;

	uint16_t dstMod = SCR_STRIDEB - (widthWords * 2);

	uint32_t dstStart = (uint32_t)dst + (y * SCR_STRIDEB) + (startXWord * 2);

	uint16_t leftMask = (uint16_t)(0xFFFFU << (wordOffset + MAX(0, 0x10 - (wordOffset + width)))) >> wordOffset;
	uint16_t rightMask;

	if (widthWords == 1)
	{
		rightMask = leftMask;
	}
	else
	{
		rightMask = 0xFFFFU << MIN(0x10, ((startXWord + widthWords) << 4) - (x + width));
	}

	uint16_t minterm = 0xA;

	minterm |= set_bits ? 0xF0 : 0x00;

	WaitForBlitter();

	// A = Mask of bits inside copy region
	// B = Optional bitplane mask
	// C = Destination data (for region outside mask)
	// D = Destination data
	custom->bltcon0 = BLTCON0_USEC | BLTCON0_USED | minterm;
	custom->bltcon1 = 0;
	custom->bltcmod = dstMod;
	custom->bltdmod = dstMod;
	custom->bltafwm = leftMask;
	custom->bltalwm = rightMask;
	custom->bltadat = 0xFFFF;
	custom->bltcpt = (void*)dstStart;
	custom->bltdpt = (void*)dstStart;
	custom->bltsize = (height << BLTSIZE_H0_SHF) | widthWords;
}

void BlitterLine(void* dst, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint16_t dx = ABS(x1 - x0);
	uint16_t dy = ABS(y1 - y0);
	uint16_t dmax = MAX(dx, dy);
	uint16_t dmin = MIN(dx, dy);
	uint32_t dstStart = (uint32_t)dst + (y0 * SCR_STRIDEB) + ((x0 / 0x8) & ~0x1);
	uint8_t  octant =
		((((dx >= dy) && (x0 >= x1)) | ((dx < dy) && (y0 >= y1))) << 0) |
		((((dx >= dy) && (y0 >= y1)) | ((dx < dy) && (x0 >= x1))) << 1) |
		((dx >= dy) << 2);

	WaitForBlitter();

	// A = Line parameters
	// C = Destination data (for region outside mask)
	// D = Destination data
	custom->bltcon0 = ((x0 & 0xF) << BLTCON0_ASH0_SHF) | BLTCON0_USEA | BLTCON0_USEC | BLTCON0_USED | (0xCA << BLTCON0_LF0_SHF);
	custom->bltcon1 =
		((x0 & 0xF) << BLTCON1_TEX0_SHF) |
		((((4 * dmin) - (2 * dmax)) < 0 ? 1 : 0) << BLTCON1_SIGN_SHF) |
		(octant << BLTCON1_AUL_SHF) |
		(0 << BLTCON1_SING_SHF) |
		BLTCON1_LINE;
	custom->bltadat = 0x8000;
	custom->bltbdat = 0xFFFF;
	custom->bltafwm = 0xFFFF;
	custom->bltalwm = 0xFFFF;
	custom->bltamod = 4 * (dmin - dmax);
	custom->bltbmod = 4 * dmin;
	custom->bltcmod = SCR_STRIDEB;
	custom->bltdmod = SCR_STRIDEB;
	custom->bltapt = (void*)(uint32_t)((4 * dmin) - (2 * dmax));
	custom->bltcpt = (void*)dstStart;
	custom->bltdpt = (void*)dstStart;
	custom->bltsize = ((dmax + 1) << BLTSIZE_H0_SHF) | (0x2 << BLTSIZE_W0_SHF);
}

void BlitterChar(void* dst, uint16_t x, uint16_t y, uint8_t charIndex, uint8_t ink, uint8_t paper)
{
	uint16_t startXWord = x >> 4;
	uint16_t endXWord = (x + 5) >> 4;
	uint16_t widthWords = endXWord - startXWord + 1;
	uint32_t srcStart = (uint32_t)(&(*charsetWords)[charIndex][0]);
	uint32_t dstStart = (uint32_t)dst + (startXWord << 1) + (y * SCR_STRIDEB);
	uint16_t shift = x & 0xF;
	uint16_t leftMask = 0xFC00;
	uint16_t rightMask = (widthWords == 1 ? 0xFC00 : 0);

	uint16_t con = (shift << BLTCON0_ASH0_SHF) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED;

	WaitForBlitter();
	custom->bltcon1 = shift << BLTCON1_BSH0_SHF;
	custom->bltamod = 0;
	custom->bltbmod = -2*(widthWords - 1);
	custom->bltcmod = SCR_STRIDEB - (widthWords << 1);
	custom->bltdmod = SCR_STRIDEB - (widthWords << 1);
	custom->bltafwm = leftMask;
	custom->bltalwm = rightMask;
	custom->bltadat = 0xFFFF;

	// A = Character box mask
	// B = Glyph bits
	// C = Destination data in
	// D = Destination data out

	static uint16_t mintermSolid[4] = { 0x0A, 0x3A, 0xCA, 0xFA };
	static uint16_t mintermTrans[4] = { 0x2A, 0x2A, 0xEA, 0xEA };
	uint16_t* minterms = paper != 255 ? mintermSolid : mintermTrans;

	ink <<= 1;

	for (uint16_t plane_idx = 0; plane_idx < BITPLANES; ++plane_idx)
	{
		custom->bltcon0 = con | minterms[(ink & 2) | (paper & 1)];
		custom->bltbpt = (void*)srcStart;
		custom->bltcpt = (void*)dstStart;
		custom->bltdpt = (void*)dstStart;
		custom->bltsize = (8 << BLTSIZE_H0_SHF) | widthWords;

		if (plane_idx == BITPLANES-1)
			break;

		WaitForBlitter();

		ink     >>= 1;
		paper   >>= 1;
		dstStart += SCR_BPNEXTB;
	}
}

#endif