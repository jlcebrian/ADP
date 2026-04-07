#ifdef _ATARIST

#include "textdraw.h"

#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include "video.h"

enum SolidPatchMode
{
	SolidPatchMode_And = 0,
	SolidPatchMode_AndOr = 1,
	SolidPatchMode_OrM = 2,
	SolidPatchMode_OrMXorO = 3,
};

typedef void (*DrawCharacterSolidPatchedAsmFn)(uint16_t* out, const uint16_t* rotation, const uint8_t* in, const uint16_t* cover);
typedef void (*DrawCharacterTransparentPatchedAsmFn)(uint16_t* out, const uint16_t* rotation, const uint8_t* in);

static uint16_t solidTextStubCode[87];
static const uint32_t solidTextStubWords = 87;
static uint8_t lastSolidAttributes = 0xFF;
static uint16_t transparentTextStubCode[74];
static const uint32_t transparentTextStubWords = 74;
static uint8_t lastTransparentInk = 0xFF;
static bool textDrawTablesReady = false;
static uint16_t textRotationTable[16][256][2];
static uint16_t textCoverTable[16][9][2];

static uint8_t GetSolidPatchMode(uint8_t ink, uint8_t paper, int planeIndex);
static bool EmitSolidTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, uint8_t paper);
static bool EmitTransparentTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink);

bool VID_InitializeTextDraw()
{
	if (!textDrawTablesReady)
	{
		static const uint8_t widthMask[9] =
		{
			0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF
		};

		for (int shift = 0; shift < 16; shift++)
		{
			for (int value = 0; value < 256; value++)
			{
				uint32_t bits = ((uint32_t)value << 24) >> shift;
				textRotationTable[shift][value][0] = (uint16_t)(bits >> 16);
				textRotationTable[shift][value][1] = (uint16_t)bits;
			}

			for (int width = 0; width <= 8; width++)
			{
				uint32_t cover = ((uint32_t)widthMask[width] << 24) >> shift;
				textCoverTable[shift][width][0] = (uint16_t)(cover >> 16);
				textCoverTable[shift][width][1] = (uint16_t)cover;
			}
		}

		textDrawTablesReady = true;
	}

	lastSolidAttributes = 0xFF;
	lastTransparentInk = 0xFF;
	return true;
}

void VID_FinishTextDraw()
{
	lastSolidAttributes = 0xFF;
	lastTransparentInk = 0xFF;
}

struct WordEmitter
{
	uint16_t* ptr;

	void Emit(uint16_t value)
	{
		*ptr++ = value;
	}
};

static uint8_t GetSolidPatchMode(uint8_t ink, uint8_t paper, int planeIndex)
{
	uint8_t inkBit = (uint8_t)((ink >> planeIndex) & 1);
	uint8_t paperBit = (uint8_t)((paper >> planeIndex) & 1);
	if (inkBit != 0)
		return paperBit != 0 ? SolidPatchMode_OrM : SolidPatchMode_AndOr;
	return paperBit != 0 ? SolidPatchMode_OrMXorO : SolidPatchMode_And;
}

static bool EmitSolidTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, uint8_t paper)
{
	WordEmitter emitter = { code };
	static const uint16_t firstWordOffset[4] = { 0, 2, 4, 6 };
	static const uint16_t secondWordOffset[4] = { 8, 10, 12, 14 };

	emitter.Emit(0x48E7); // movem.l d2-d7/a2,-(sp)
	emitter.Emit(0x3F20); // movem register mask
	emitter.Emit(0x206F); // move.l 32(sp),a0
	emitter.Emit(0x0020); // 32(sp)
	emitter.Emit(0x226F); // move.l 36(sp),a1
	emitter.Emit(0x0024); // 36(sp)
	emitter.Emit(0x246F); // move.l 44(sp),a2
	emitter.Emit(0x002C); // 44(sp)
	emitter.Emit(0x3C12); // move.w (a2),d6
	emitter.Emit(0x3E2A); // move.w 2(a2),d7
	emitter.Emit(0x0002); // 2(a2)
	emitter.Emit(0x246F); // move.l 40(sp),a2
	emitter.Emit(0x0028); // 40(sp)
	emitter.Emit(0x2A0A); // move.l a2,d5
	emitter.Emit(0x5085); // addq.l #8,d5

	uint16_t loopStart = (uint16_t)(emitter.ptr - code);
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3031); // move.w 0(a1,d2.l),d0
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3231); // move.w 2(a1,d2.l),d1
	emitter.Emit(0x2802); // indexed extension word

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint16_t firstOffset = firstWordOffset[planeIndex];
		uint16_t secondOffset = secondWordOffset[planeIndex];

		switch (GetSolidPatchMode(ink, paper, planeIndex))
		{
			case SolidPatchMode_And:
				emitter.Emit(0x3606);       // move.w d6,d3
				emitter.Emit(0x4643);       // not.w d3
				emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
				emitter.Emit(firstOffset);  // xxxx displacement

				emitter.Emit(0x3607);       // move.w d7,d3
				emitter.Emit(0x4643);       // not.w d3
				emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
				emitter.Emit(secondOffset); // xxxx displacement
				break;

			case SolidPatchMode_AndOr:
				emitter.Emit(0x3606);       // move.w d6,d3
				emitter.Emit(0x4643);       // not.w d3
				emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
				emitter.Emit(firstOffset);  // xxxx displacement
				emitter.Emit(0x8168);       // or.w d0,xxxx(a0)
				emitter.Emit(firstOffset);  // xxxx displacement

				emitter.Emit(0x3607);       // move.w d7,d3
				emitter.Emit(0x4643);       // not.w d3
				emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
				emitter.Emit(secondOffset); // xxxx displacement
				emitter.Emit(0x8368);       // or.w d1,xxxx(a0)
				emitter.Emit(secondOffset); // xxxx displacement
				break;

			case SolidPatchMode_OrM:
				emitter.Emit(0x8D68);       // or.w d6,xxxx(a0)
				emitter.Emit(firstOffset);  // xxxx displacement

				emitter.Emit(0x8F68);       // or.w d7,xxxx(a0)
				emitter.Emit(secondOffset); // xxxx displacement
				break;

			default:
				emitter.Emit(0x3606);       // move.w d6,d3
				emitter.Emit(0xB143);       // eor.w d0,d3
				emitter.Emit(0x8768);       // or.w d3,xxxx(a0)
				emitter.Emit(firstOffset);  // xxxx displacement

				emitter.Emit(0x3607);       // move.w d7,d3
				emitter.Emit(0xB343);       // eor.w d1,d3
				emitter.Emit(0x8768);       // or.w d3,xxxx(a0)
				emitter.Emit(secondOffset); // xxxx displacement
				break;
		}
	}

	emitter.Emit(0x41E8);       // lea 160(a0),a0
	emitter.Emit(0x00A0);       // 160(a0)
	emitter.Emit(0xB5C5);       // cmpa.l d5,a2
	emitter.Emit(0x6600);       // bne.w xxxx
	uint16_t displacement = (uint16_t)(((int32_t)loopStart - (int32_t)(emitter.ptr - code)) * 2);
	emitter.Emit(displacement); // xxxx displacement
	emitter.Emit(0x4CDF);       // movem.l (sp)+,d2-d7/a2
	emitter.Emit(0x04FC);       // movem register mask
	emitter.Emit(0x4E75);       // rts

	return (uint32_t)(emitter.ptr - code) <= templateWords;
}

static bool EmitTransparentTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink)
{
	WordEmitter emitter = { code };
	static const uint16_t firstWordOffset[4] = { 0, 2, 4, 6 };
	static const uint16_t secondWordOffset[4] = { 8, 10, 12, 14 };

	emitter.Emit(0x48E7); // movem.l d2-d7/a2,-(sp)
	emitter.Emit(0x3F20); // movem register mask
	emitter.Emit(0x206F); // move.l 32(sp),a0
	emitter.Emit(0x0020); // 32(sp)
	emitter.Emit(0x226F); // move.l 36(sp),a1
	emitter.Emit(0x0024); // 36(sp)
	emitter.Emit(0x246F); // move.l 40(sp),a2
	emitter.Emit(0x0028); // 40(sp)
	emitter.Emit(0x2A0A); // move.l a2,d5
	emitter.Emit(0x5085); // addq.l #8,d5

	uint16_t loopStart = (uint16_t)(emitter.ptr - code);
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3031); // move.w 0(a1,d2.l),d0
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3231); // move.w 2(a1,d2.l),d1
	emitter.Emit(0x2802); // indexed extension word

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint16_t firstOffset = firstWordOffset[planeIndex];
		uint16_t secondOffset = secondWordOffset[planeIndex];
		bool inkBit = ((ink >> planeIndex) & 1) != 0;

		if (!inkBit)
		{
			emitter.Emit(0x3600);       // move.w d0,d3
			emitter.Emit(0x4643);       // not.w d3
			emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
			emitter.Emit(firstOffset);  // xxxx displacement

			emitter.Emit(0x3601);       // move.w d1,d3
			emitter.Emit(0x4643);       // not.w d3
			emitter.Emit(0xC768);       // and.w d3,xxxx(a0)
			emitter.Emit(secondOffset); // xxxx displacement
		}
		else
		{
			emitter.Emit(0x8168);       // or.w d0,xxxx(a0)
			emitter.Emit(firstOffset);  // xxxx displacement

			emitter.Emit(0x8368);       // or.w d1,xxxx(a0)
			emitter.Emit(secondOffset); // xxxx displacement
		}
	}

	emitter.Emit(0x41E8); // lea 160(a0),a0
	emitter.Emit(0x00A0); // 160(a0)
	emitter.Emit(0xB5C5); // cmpa.l d5,a2
	emitter.Emit(0x6600); // bne.w xxxx
	emitter.Emit((uint16_t)(((int32_t)loopStart - (int32_t)(emitter.ptr - code)) * 2)); // xxxx displacement
	emitter.Emit(0x4CDF); // movem.l (sp)+,d2-d7/a2
	emitter.Emit(0x04FC); // movem register mask
	emitter.Emit(0x4E75); // rts

	return (uint32_t)(emitter.ptr - code) <= templateWords;
}

void VID_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	if (length == 0 || text == 0)
		return;

	ink &= 0x0F;

	if (paper == 255)
	{
		if (lastTransparentInk != ink)
		{
			EmitTransparentTextStub(transparentTextStubCode, transparentTextStubWords, ink);
			lastTransparentInk = ink;
		}

		DrawCharacterTransparentPatchedAsmFn draw = (DrawCharacterTransparentPatchedAsmFn)transparentTextStubCode;
		uint16_t wordOffset = (uint16_t)(x >> 4);
		uint8_t shift = (uint8_t)(x & 0x0F);

		for (uint16_t n = 0; n < length && x < screenWidth; n++)
		{
			uint8_t ch = text[n];
			uint8_t width = charWidth[ch];
			if (width == 0)
				continue;
			if (width > 8)
				width = 8;

			draw(textScreen + 80 * y + 4 * wordOffset, &textRotationTable[shift][0][0], charset + 8 * ch);

			x += width;
			shift += width;
			wordOffset += shift >> 4;
			shift &= 0x0F;
		}
		return;
	}

	paper &= 0x0F;
	uint8_t attributes = (uint8_t)((paper << 4) | ink);
	if (lastSolidAttributes != attributes)
	{
		EmitSolidTextStub(solidTextStubCode, solidTextStubWords, ink, paper);
		lastSolidAttributes = attributes;
	}

	DrawCharacterSolidPatchedAsmFn draw = (DrawCharacterSolidPatchedAsmFn)solidTextStubCode;
	uint16_t wordOffset = (uint16_t)(x >> 4);
	uint8_t shift = (uint8_t)(x & 0x0F);

	for (uint16_t n = 0; n < length && x < screenWidth; n++)
	{
		uint8_t ch = text[n];
		uint8_t width = charWidth[ch];
		if (width == 0)
			continue;
		if (width > 8)
			width = 8;

		draw(textScreen + 80 * y + 4 * wordOffset, &textRotationTable[shift][0][0], charset + 8 * ch, &textCoverTable[shift][width][0]);

		x += width;
		shift += width;
		wordOffset += shift >> 4;
		shift &= 0x0F;
	}
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_DrawTextSpan(x, y, &ch, 1, ink, paper);
}

#endif