#ifdef _ATARIST

#include "textdraw.h"

#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include "video.h"

enum SolidPatchMode
{
	SolidPatchMode_And = 0,      // Clear glyph pixels and preserve paper pixels.
	SolidPatchMode_AndOr = 1,    // Clear paper pixels, then set glyph pixels.
	SolidPatchMode_OrM = 2,      // Preserve paper pixels and force glyph pixels on.
	SolidPatchMode_OrMXorO = 3,  // Force paper pixels on, then toggle glyph pixels back off.
};

enum TextByteAlignment
{
	TextByteAlignment_Left = 0,
	TextByteAlignment_Right = 1,
	TextByteAlignment_Count = 2,
};

typedef void (*DrawCharacterSolidPatchedAsmFn)(uint8_t* out, const uint16_t* rotation, const uint8_t* in, uint16_t cover);
typedef void (*DrawCharacterTransparentPatchedAsmFn)(uint8_t* out, const uint16_t* rotation, const uint8_t* in);

// Worst case today is 79 words: 24-word setup, 8 byte blocks at 6 words each,
// and a 7-word loop epilogue. Keep one word of slack.
static const uint32_t solidTextStubWords = 80;
static uint16_t solidTextStubCode[TextByteAlignment_Count][solidTextStubWords];
static uint8_t lastSolidAttributes = 0xFF;
// Worst case today is 71 words: 16-word setup, 8 byte blocks at 6 words each,
// and a 7-word loop epilogue. Keep one word of slack.
static const uint32_t transparentTextStubWords = 72;
static uint16_t transparentTextStubCode[TextByteAlignment_Count][transparentTextStubWords];
static uint8_t lastTransparentInk = 0xFF;
static bool textDrawTablesReady = false;
static uint16_t textRotationTable[8][256];

static uint8_t GetSolidPatchMode(uint8_t ink, uint8_t paper, int planeIndex);
static bool EmitSolidTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, uint8_t paper, bool rightByte);
static bool EmitTransparentTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, bool rightByte);

bool VID_InitializeTextDraw()
{
	if (!textDrawTablesReady)
	{
		for (int shift = 0; shift < 8; shift++)
		{
			uint16_t* table = textRotationTable[shift];
			for (int value = 0; value < 256; value++)
				table[value] = (uint16_t)(((uint16_t)value << 8) >> shift);
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

static void EmitSolidByteBlock(WordEmitter& emitter, uint16_t offset, bool secondByte, uint8_t mode)
{
	switch (mode)
	{
		case SolidPatchMode_And:
			emitter.Emit((uint16_t)(secondByte ? 0x1A00 : 0x1A01)); // move.b d0/d1,d5
			break;

		case SolidPatchMode_AndOr:
			emitter.Emit((uint16_t)(secondByte ? 0x1A00 : 0x1A01)); // move.b d0/d1,d5
			break;

		case SolidPatchMode_OrM:
			emitter.Emit((uint16_t)(secondByte ? 0x1A06 : 0x1A07)); // move.b d6/d7,d5
			break;

		case SolidPatchMode_OrMXorO:
			emitter.Emit((uint16_t)(secondByte ? 0x1A06 : 0x1A07)); // move.b d6/d7,d5
			break;
	}

	if (mode == SolidPatchMode_And || mode == SolidPatchMode_AndOr)
	{
		if (offset == 0)
			emitter.Emit(0xCA10); // and.b (a0),d5
		else
		{
			emitter.Emit(0xCA28); // and.b xxxx(a0),d5
			emitter.Emit(offset);
		}
		if (mode == SolidPatchMode_AndOr)
			emitter.Emit((uint16_t)(secondByte ? 0x8A02 : 0x8A04)); // or.b d2/d4,d5
	}
	else
	{
		if (offset == 0)
			emitter.Emit(0x8A10); // or.b (a0),d5
		else
		{
			emitter.Emit(0x8A28); // or.b xxxx(a0),d5
			emitter.Emit(offset);
		}
		if (mode == SolidPatchMode_OrMXorO)
			emitter.Emit((uint16_t)(secondByte ? 0xB505 : 0xB905)); // eor.b d2/d4,d5
	}

	if (offset == 0)
	{
		emitter.Emit(0x1085); // move.b d5,(a0)
	}
	else
	{
		emitter.Emit(0x1145); // move.b d5,xxxx(a0)
		emitter.Emit(offset);
	}
}

static void EmitTransparentByteBlock(WordEmitter& emitter, uint16_t offset, bool secondByte, bool inkBit)
{
	if (!inkBit)
	{
		emitter.Emit((uint16_t)(secondByte ? 0x1A02 : 0x1A04)); // move.b d2/d4,d5
		emitter.Emit(0x4605); // not.b d5
		if (offset == 0)
			emitter.Emit(0xCA10); // and.b (a0),d5
		else
		{
			emitter.Emit(0xCA28); // and.b xxxx(a0),d5
			emitter.Emit(offset);
		}
	}
	else
	{
		if (offset == 0)
			emitter.Emit(0x1A10); // move.b (a0),d5
		else
		{
			emitter.Emit(0x1A28); // move.b xxxx(a0),d5
			emitter.Emit(offset);
		}
		emitter.Emit((uint16_t)(secondByte ? 0x8A02 : 0x8A04)); // or.b d2/d4,d5
	}

	if (offset == 0)
		emitter.Emit(0x1085); // move.b d5,(a0)
	else
	{
		emitter.Emit(0x1145); // move.b d5,xxxx(a0)
		emitter.Emit(offset);
	}
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

static bool EmitSolidTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, uint8_t paper, bool rightByte)
{
	WordEmitter emitter = { code };

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
	emitter.Emit(0x3200); // move.w d0,d1
	emitter.Emit(0xE049); // lsr.w #8,d1
	emitter.Emit(0x3E06); // move.w d6,d7
	emitter.Emit(0xE04F); // lsr.w #8,d7
	emitter.Emit(0x7607); // moveq #7,d3

	uint16_t loopStart = (uint16_t)(emitter.ptr - code);
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3802); // move.w d2,d4
	emitter.Emit(0xE04C); // lsr.w #8,d4

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint16_t firstOffset = (uint16_t)(planeIndex * 2 + (rightByte ? 1 : 0));
		uint16_t secondOffset = (uint16_t)(rightByte ? (planeIndex * 2 + 8) : (planeIndex * 2 + 1));
		uint8_t mode = GetSolidPatchMode(ink, paper, planeIndex);
		EmitSolidByteBlock(emitter, firstOffset, false, mode);
		EmitSolidByteBlock(emitter, secondOffset, true, mode);
	}

	emitter.Emit(0x41E8);       // lea 160(a0),a0
	emitter.Emit(0x00A0);       // 160(a0)
	emitter.Emit(0x51CB);       // dbra d3,loop
	uint16_t displacement = (uint16_t)(((int32_t)loopStart - (int32_t)(emitter.ptr - code)) * 2);
	emitter.Emit(displacement); // xxxx displacement
	emitter.Emit(0x4CDF);       // movem.l (sp)+,d2-d7/a2
	emitter.Emit(0x04FC);       // movem register mask
	emitter.Emit(0x4E75);       // rts

	return (uint32_t)(emitter.ptr - code) <= templateWords;
}

static bool EmitTransparentTextStub(uint16_t* code, uint32_t templateWords, uint8_t ink, bool rightByte)
{
	WordEmitter emitter = { code };

	emitter.Emit(0x48E7); // movem.l d2-d7/a2,-(sp)
	emitter.Emit(0x3F20); // movem register mask
	emitter.Emit(0x206F); // move.l 32(sp),a0
	emitter.Emit(0x0020); // 32(sp)
	emitter.Emit(0x226F); // move.l 36(sp),a1
	emitter.Emit(0x0024); // 36(sp)
	emitter.Emit(0x246F); // move.l 40(sp),a2
	emitter.Emit(0x0028); // 40(sp)
	emitter.Emit(0x7607); // moveq #7,d3

	uint16_t loopStart = (uint16_t)(emitter.ptr - code);
	emitter.Emit(0x7400); // moveq #0,d2
	emitter.Emit(0x141A); // move.b (a2)+,d2
	emitter.Emit(0xD442); // add.w d2,d2
	emitter.Emit(0x3431); // move.w 0(a1,d2.l),d2
	emitter.Emit(0x2800); // indexed extension word
	emitter.Emit(0x3802); // move.w d2,d4
	emitter.Emit(0xE04C); // lsr.w #8,d4

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		uint16_t firstOffset = (uint16_t)(planeIndex * 2 + (rightByte ? 1 : 0));
		uint16_t secondOffset = (uint16_t)(rightByte ? (planeIndex * 2 + 8) : (planeIndex * 2 + 1));
		bool inkBit = ((ink >> planeIndex) & 1) != 0;
		EmitTransparentByteBlock(emitter, firstOffset, false, inkBit);
		EmitTransparentByteBlock(emitter, secondOffset, true, inkBit);
	}

	emitter.Emit(0x41E8); // lea 160(a0),a0
	emitter.Emit(0x00A0); // 160(a0)
	emitter.Emit(0x51CB); // dbra d3,loop
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
			if (!EmitTransparentTextStub(transparentTextStubCode[TextByteAlignment_Left], transparentTextStubWords, ink, false) ||
				!EmitTransparentTextStub(transparentTextStubCode[TextByteAlignment_Right], transparentTextStubWords, ink, true))
				return;
			lastTransparentInk = ink;
		}

		uint8_t* out = (uint8_t*)textScreen + 160 * y + ((x >> 4) << 3);
		uint8_t phase = (uint8_t)(x & 0x0F);

		for (uint16_t n = 0; n < length && x < screenWidth; n++)
		{
			uint8_t ch = text[n];
			uint8_t width = charWidth[ch];
			if (width == 0)
				continue;
			if (width > 8)
				width = 8;

			TextByteAlignment alignment = (phase & 0x08) != 0 ? TextByteAlignment_Right : TextByteAlignment_Left;
			uint8_t shift = (uint8_t)(phase & 0x07);
			DrawCharacterTransparentPatchedAsmFn draw = (DrawCharacterTransparentPatchedAsmFn)transparentTextStubCode[alignment];
			draw(out, textRotationTable[shift], charset + 8 * ch);
			x += width;
			phase = (uint8_t)(phase + width);
			out += (phase >> 4) << 3;
			phase &= 0x0F;
		}
		return;
	}

	paper &= 0x0F;
	uint8_t attributes = (uint8_t)((paper << 4) | ink);
	if (lastSolidAttributes != attributes)
	{
		if (!EmitSolidTextStub(solidTextStubCode[TextByteAlignment_Left], solidTextStubWords, ink, paper, false) ||
			!EmitSolidTextStub(solidTextStubCode[TextByteAlignment_Right], solidTextStubWords, ink, paper, true))
			return;
		lastSolidAttributes = attributes;
	}

	uint8_t* out = (uint8_t*)textScreen + 160 * y + ((x >> 4) << 3);
	uint8_t phase = (uint8_t)(x & 0x0F);

	for (uint16_t n = 0; n < length && x < screenWidth; n++)
	{
		uint8_t ch = text[n];
		uint8_t width = charWidth[ch];
		if (width == 0)
			continue;
		if (width > 8)
			width = 8;

		TextByteAlignment alignment = (phase & 0x08) != 0 ? TextByteAlignment_Right : TextByteAlignment_Left;
		uint8_t shift = (uint8_t)(phase & 0x07);
		DrawCharacterSolidPatchedAsmFn draw = (DrawCharacterSolidPatchedAsmFn)solidTextStubCode[alignment];
		const uint16_t* rotation = textRotationTable[shift];
		uint8_t widthMask8 = (uint8_t)(0xFF << (8 - width));
		uint16_t cover = rotation[widthMask8];
		draw(out, rotation, charset + 8 * ch, cover);
		x += width;
		phase = (uint8_t)(phase + width);
		out += (phase >> 4) << 3;
		phase &= 0x0F;
	}
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_DrawTextSpan(x, y, &ch, 1, ink, paper);
}

#endif