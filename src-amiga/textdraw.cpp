#ifdef _AMIGA

#include "textdraw.h"

#include <ddb_data.h>
#include <ddb_scr.h>
#include "video.h"

#include <exec/memory.h>
#include <proto/exec.h>

enum SolidPatchMode
{
	SolidPatchMode_And = 0,      // Clear glyph pixels and preserve paper pixels.
	SolidPatchMode_AndOr = 1,    // Clear paper pixels, then set glyph pixels.
	SolidPatchMode_OrM = 2,      // Preserve paper pixels and force glyph pixels on.
	SolidPatchMode_OrMXorO = 3,  // Force paper pixels on, then toggle glyph pixels back off.
};

typedef void (*DrawCharacterSolidPatchedAsmFn)(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint16_t cover);
typedef void (*DrawCharacterTransparentPatchedAsmFn)(uint8_t* out, const uint16_t* rotation, const uint8_t* in);

static uint16_t solidTextStubCode[0xE4 / 2];
static const uint32_t solidTextStubSize = 0xE4;
static uint8_t lastSolidAttributes = 0xFF;
static uint16_t transparentTextStubCode[0xD4 / 2];
static const uint32_t transparentTextStubSize = 0xD4;
static uint8_t lastTransparentInk = 0xFF;

static uint8_t GetSolidPatchMode(uint8_t ink, uint8_t paper, int planeIndex);
static bool EmitSolidTextStub(uint16_t* code, uint32_t codeSize, uint8_t ink, uint8_t paper);
static bool EmitTransparentTextStub(uint16_t* code, uint32_t codeSize, uint8_t ink);

bool VID_InitializeTextDraw()
{
	lastSolidAttributes = 0xFF;
	lastTransparentInk = 0xFF;

	return true;
}

void VID_FinishTextDraw()
{
	lastSolidAttributes = 0xFF;
	lastTransparentInk = 0xFF;
}

struct SolidWordEmitter
{
	uint16_t* ptr;

	void Emit(uint16_t value)
	{
		*ptr++ = value;
	}
};

static void EmitUnalignedPlaneBlock(SolidWordEmitter& emitter, int planeIndex, bool rightByte, uint8_t mode)
{
	uint16_t offset = (uint16_t)(planeIndex * 8000 + (rightByte ? 1 : 0));

	switch (mode)
	{
		case SolidPatchMode_And:
			emitter.Emit((uint16_t)(rightByte ? 0x1A00 : 0x1A01)); // move.b d0,d5 / move.b d1,d5
			if (offset == 0)
				emitter.Emit(0xCA10); // and.b (a0),d5
			else
			{
				emitter.Emit(0xCA28); // and.b xxxx(a0),d5
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0x4E71); // nop
			break;

		case SolidPatchMode_AndOr:
			emitter.Emit((uint16_t)(rightByte ? 0x1A00 : 0x1A01)); // move.b d0,d5 / move.b d1,d5
			if (offset == 0)
				emitter.Emit(0xCA10); // and.b (a0),d5
			else
			{
				emitter.Emit(0xCA28); // and.b xxxx(a0),d5
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit((uint16_t)(rightByte ? 0x8A02 : 0x8A04)); // or.b d2,d5 / or.b d4,d5
			break;

		case SolidPatchMode_OrM:
			emitter.Emit((uint16_t)(rightByte ? 0x1A06 : 0x1A07)); // move.b d6,d5 / move.b d7,d5
			if (offset == 0)
				emitter.Emit(0x8A10); // or.b (a0),d5
			else
			{
				emitter.Emit(0x8A28); // or.b xxxx(a0),d5
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0x4E71); // nop
			break;

		case SolidPatchMode_OrMXorO:
			emitter.Emit((uint16_t)(rightByte ? 0x1A06 : 0x1A07)); // move.b d6,d5 / move.b d7,d5
			if (offset == 0)
				emitter.Emit(0x8A10); // or.b (a0),d5
			else
			{
				emitter.Emit(0x8A28); // or.b xxxx(a0),d5
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit((uint16_t)(rightByte ? 0xB505 : 0xB905)); // eor.b d2,d5 / eor.b d4,d5
			break;
	}

	if (offset == 0)
		emitter.Emit(0x1085); // move.b d5,(a0)
	else
	{
		emitter.Emit(0x1145); // move.b d5,xxxx(a0)
		emitter.Emit(offset); // xxxx displacement
	}
}

static void EmitAlignedPlaneBlock(SolidWordEmitter& emitter, int planeIndex, uint8_t mode)
{
	uint16_t offset = (uint16_t)(planeIndex * 8000);

	switch (mode)
	{
		case SolidPatchMode_And:
			emitter.Emit(0x3600); // move.w d0,d3
			if (offset == 0)
				emitter.Emit(0xC650); // and.w (a0),d3
			else
			{
				emitter.Emit(0xC668); // and.w xxxx(a0),d3
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0x4E71); // nop
			break;

		case SolidPatchMode_AndOr:
			emitter.Emit(0x3600); // move.w d0,d3
			if (offset == 0)
				emitter.Emit(0xC650); // and.w (a0),d3
			else
			{
				emitter.Emit(0xC668); // and.w xxxx(a0),d3
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0x8642); // or.w d2,d3
			break;

		case SolidPatchMode_OrM:
			emitter.Emit(0x3606); // move.w d6,d3
			if (offset == 0)
				emitter.Emit(0x8650); // or.w (a0),d3
			else
			{
				emitter.Emit(0x8668); // or.w xxxx(a0),d3
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0x4E71); // nop
			break;

		case SolidPatchMode_OrMXorO:
			emitter.Emit(0x3606); // move.w d6,d3
			if (offset == 0)
				emitter.Emit(0x8650); // or.w (a0),d3
			else
			{
				emitter.Emit(0x8668); // or.w xxxx(a0),d3
				emitter.Emit(offset); // xxxx displacement
			}
			emitter.Emit(0xB543); // eor.w d2,d3
			break;
	}

	if (offset == 0)
		emitter.Emit(0x3083); // move.w d3,(a0)
	else
	{
		emitter.Emit(0x3143); // move.w d3,xxxx(a0)
		emitter.Emit(offset); // xxxx displacement
	}
}

static void EmitTransparentUnalignedPlaneBlock(SolidWordEmitter& emitter, int planeIndex, bool rightByte, bool inkBit)
{
	uint16_t offset = (uint16_t)(planeIndex * 8000 + (rightByte ? 1 : 0));

	if (!inkBit)
	{
		emitter.Emit((uint16_t)(rightByte ? 0x1A02 : 0x1A04)); // move.b d2,d5 / move.b d4,d5
		emitter.Emit(0x4605); // not.b d5
		if (offset == 0)
			emitter.Emit(0xCA10); // and.b (a0),d5
		else
		{
			emitter.Emit(0xCA28); // and.b xxxx(a0),d5
			emitter.Emit(offset); // xxxx displacement
		}
	}
	else
	{
		if (offset == 0)
			emitter.Emit(0x1A10); // move.b (a0),d5
		else
		{
			emitter.Emit(0x1A28); // move.b xxxx(a0),d5
			emitter.Emit(offset); // xxxx displacement
		}
		emitter.Emit((uint16_t)(rightByte ? 0x8A02 : 0x8A04)); // or.b d2,d5 / or.b d4,d5
		emitter.Emit(0x4E71); // nop
	}

	if (offset == 0)
		emitter.Emit(0x1085); // move.b d5,(a0)
	else
	{
		emitter.Emit(0x1145); // move.b d5,xxxx(a0)
		emitter.Emit(offset); // xxxx displacement
	}
}

static void EmitTransparentAlignedPlaneBlock(SolidWordEmitter& emitter, int planeIndex, bool inkBit)
{
	uint16_t offset = (uint16_t)(planeIndex * 8000);

	if (!inkBit)
	{
		emitter.Emit(0x3602); // move.w d2,d3
		emitter.Emit(0x4643); // not.w d3
		if (offset == 0)
			emitter.Emit(0xC650); // and.w (a0),d3
		else
		{
			emitter.Emit(0xC668); // and.w xxxx(a0),d3
			emitter.Emit(offset); // xxxx displacement
		}
	}
	else
	{
		if (offset == 0)
			emitter.Emit(0x3610); // move.w (a0),d3
		else
		{
			emitter.Emit(0x3628); // move.w xxxx(a0),d3
			emitter.Emit(offset); // xxxx displacement
		}
		emitter.Emit(0x8642); // or.w d2,d3
		emitter.Emit(0x4E71); // nop
	}

	if (offset == 0)
		emitter.Emit(0x3083); // move.w d3,(a0)
	else
	{
		emitter.Emit(0x3143); // move.w d3,xxxx(a0)
		emitter.Emit(offset); // xxxx displacement
	}
}

static bool EmitSolidTextStub(uint16_t* code, uint32_t templateSize, uint8_t ink, uint8_t paper)
{
	if ((templateSize & 1) != 0)
		return false;

	SolidWordEmitter emitter = { code };

	emitter.Emit(0x48E7); // movem.l d2-d7/a2,-(sp)
	emitter.Emit(0x3F20); // movem register mask
	emitter.Emit(0x206F); // move.l 32(sp),a0
	emitter.Emit(0x0020); // 32(sp)
	emitter.Emit(0x226F); // move.l 36(sp),a1
	emitter.Emit(0x0024); // 36(sp)
	emitter.Emit(0x246F); // move.l 40(sp),a2
	emitter.Emit(0x0028); // 40(sp)
	emitter.Emit(0x3C2F); // move.w 46(sp),d6
	emitter.Emit(0x002E); // 46(sp)
	emitter.Emit(0x3006); // move.w d6,d0
	emitter.Emit(0x4640); // not.w d0
	emitter.Emit(0x2208); // move.l a0,d1
	emitter.Emit(0x0801); // btst #0,d1
	emitter.Emit(0x0000); // immediate bit number 0
	emitter.Emit(0x677E); // beq.s aligned
	emitter.Emit(0x3200); // move.w d0,d1
	emitter.Emit(0xE049); // lsr.w #8,d1
	emitter.Emit(0x3E06); // move.w d6,d7
	emitter.Emit(0xE04F); // lsr.w #8,d7
	emitter.Emit(0x7607); // moveq #7,d3

	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3802); // move.w d2,d4
	emitter.Emit(0xE04C); // lsr.w #8,d4

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint8_t mode = GetSolidPatchMode(ink, paper, planeIndex);
		EmitUnalignedPlaneBlock(emitter, planeIndex, false, mode);
		EmitUnalignedPlaneBlock(emitter, planeIndex, true, mode);
	}

	emitter.Emit(0x41E8); // lea 40(a0),a0
	emitter.Emit(0x0028); // 40(a0)
	emitter.Emit(0x51CB); // dbra d3,unalignedLoop
	emitter.Emit(0xFF90); // relative displacement
	emitter.Emit(0x6040); // bra.s return

	emitter.Emit(0x7207); // moveq #7,d1
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint8_t mode = GetSolidPatchMode(ink, paper, planeIndex);
		EmitAlignedPlaneBlock(emitter, planeIndex, mode);
	}

	emitter.Emit(0x41E8); // lea 40(a0),a0
	emitter.Emit(0x0028); // 40(a0)
	emitter.Emit(0x51C9); // dbra d1,alignedLoop
	emitter.Emit(0xFFC4); // relative displacement
	emitter.Emit(0x4CDF); // movem.l (sp)+,d2-d7/a2
	emitter.Emit(0x04FC); // movem register mask
	emitter.Emit(0x4E75); // rts

	return (uint32_t)(emitter.ptr - code) == (templateSize >> 1);
}

static bool EmitTransparentTextStub(uint16_t* code, uint32_t templateSize, uint8_t ink)
{
	if ((templateSize & 1) != 0)
		return false;

	SolidWordEmitter emitter = { code };

	emitter.Emit(0x48E7); // movem.l d2-d7/a2,-(sp)
	emitter.Emit(0x3F20); // movem register mask
	emitter.Emit(0x206F); // move.l 32(sp),a0
	emitter.Emit(0x0020); // 32(sp)
	emitter.Emit(0x226F); // move.l 36(sp),a1
	emitter.Emit(0x0024); // 36(sp)
	emitter.Emit(0x246F); // move.l 40(sp),a2
	emitter.Emit(0x0028); // 40(sp)
	emitter.Emit(0x2208); // move.l a0,d1
	emitter.Emit(0x0801); // btst #0,d1
	emitter.Emit(0x0000); // immediate bit number 0
	emitter.Emit(0x6776); // beq.s aligned
	emitter.Emit(0x7607); // moveq #7,d3

	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3802); // move.w d2,d4
	emitter.Emit(0xE04C); // lsr.w #8,d4

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		bool inkBit = ((ink >> planeIndex) & 1) != 0;
		EmitTransparentUnalignedPlaneBlock(emitter, planeIndex, false, inkBit);
		EmitTransparentUnalignedPlaneBlock(emitter, planeIndex, true, inkBit);
	}

	emitter.Emit(0x41E8); // lea 40(a0),a0
	emitter.Emit(0x0028); // 40(a0)
	emitter.Emit(0x51CB); // dbra d3,unalignedLoop
	emitter.Emit(0xFF90); // relative displacement
	emitter.Emit(0x6040); // bra.s return

	emitter.Emit(0x7207); // moveq #7,d1
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		bool inkBit = ((ink >> planeIndex) & 1) != 0;
		EmitTransparentAlignedPlaneBlock(emitter, planeIndex, inkBit);
	}

	emitter.Emit(0x41E8); // lea 40(a0),a0
	emitter.Emit(0x0028); // 40(a0)
	emitter.Emit(0x51C9); // dbra d1,alignedLoop
	emitter.Emit(0xFFC4); // relative displacement
	emitter.Emit(0x4CDF); // movem.l (sp)+,d2-d7/a2
	emitter.Emit(0x04FC); // movem register mask
	emitter.Emit(0x4E75); // rts

	return (uint32_t)(emitter.ptr - code) == (templateSize >> 1);
}

static uint8_t GetSolidPatchMode(uint8_t ink, uint8_t paper, int planeIndex)
{
	// Choose the cheapest per-plane operation that converts the existing paper bit into the target ink bit.
	uint8_t inkBit = (uint8_t)((ink >> planeIndex) & 1);
	uint8_t paperBit = (uint8_t)((paper >> planeIndex) & 1);
	if (inkBit != 0)
		return paperBit != 0 ? SolidPatchMode_OrM : SolidPatchMode_AndOr;
	return paperBit != 0 ? SolidPatchMode_OrMXorO : SolidPatchMode_And;
}

void VID_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	if (length == 0 || text == 0)
		return;

	uint8_t* dst = (charToBack ^ displaySwap) ? backBuffer : frontBuffer;
	uint8_t* out = dst + y * SCR_STRIDEB + (x >> 3);
	uint16_t shift = (uint16_t)(x & 0x07);
	ink &= 0x0F;

	if (paper == 255)
	{
		if (lastTransparentInk != ink)
		{
			DebugPrintf("Amiga DrawTextSpan transparent emitter: ink=%u paper=255\n", (unsigned)ink);
			if (!EmitTransparentTextStub(transparentTextStubCode, transparentTextStubSize, ink))
				return;
			lastTransparentInk = ink;
		}
		
		for (uint16_t n = 0; n < length && x < screenWidth; n++)
		{
			uint8_t ch = text[n];
			uint8_t width = charWidth[ch];

			const uint16_t* rotation = rotationTable[shift];
			const uint8_t* glyph = charset + 8 * ch;
			((DrawCharacterTransparentPatchedAsmFn)transparentTextStubCode)(out, rotation, glyph);

			x += width;
			shift += width;
			out += shift >> 3;
			shift &= 0x07;
		}
		return;
	}

	paper &= 0x0F;
	uint8_t attributes = (paper << 4) | ink;
	if (lastSolidAttributes != attributes)
	{
		DebugPrintf("Amiga DrawTextSpan solid emitter: ink=%u paper=%u attr=%u\n",
			(unsigned)ink,
			(unsigned)paper,
			(unsigned)attributes);
		if (!EmitSolidTextStub(solidTextStubCode, solidTextStubSize, ink, paper))
			return;
		lastSolidAttributes = attributes;
	}

	for (uint16_t n = 0; n < length && x < screenWidth; n++)
	{
		uint8_t ch = text[n];
		uint8_t width = charWidth[ch];

		const uint16_t* rotation = rotationTable[shift];
		const uint8_t* glyph = charset + 8 * ch;
		const uint8_t widthMask8 = (uint8_t)(0xFF << (8 - width));
		const uint16_t cover = rotation[widthMask8];
		((DrawCharacterSolidPatchedAsmFn)solidTextStubCode)(out, rotation, glyph, cover);

		x += width;
		shift += width;
		out += shift >> 3;
		shift &= 0x07;
	}
}

// Slow path

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	uint8_t text[2] = { ch, 0 };
	VID_DrawTextSpan(x, y, text, 1, ink, paper);
}

#endif
