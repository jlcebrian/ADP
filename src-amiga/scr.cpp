#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_mem.h>

#ifdef _AMIGA

#include "video.h"

static void FadeInPalette(const uint32_t* targetPalette, uint16_t count);

static const uint32_t IFF_AMIGA_VIEWMODE_HAM = 0x00000800u;
static uint32_t ilbmTargetPalette[256];
static uint32_t fadeInPaletteBuffer[256];

static uint16_t ReadBE16(const uint8_t* ptr)
{
	return (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
}

static uint32_t ReadBE32(const uint8_t* ptr)
{
	return ((uint32_t)ptr[0] << 24) |
		((uint32_t)ptr[1] << 16) |
		((uint32_t)ptr[2] << 8) |
		(uint32_t)ptr[3];
}

static bool MatchTag(const uint8_t* ptr, const char* tag)
{
	return ptr[0] == (uint8_t)tag[0] &&
		ptr[1] == (uint8_t)tag[1] &&
		ptr[2] == (uint8_t)tag[2] &&
		ptr[3] == (uint8_t)tag[3];
}

static bool HasILBMHeader(const uint8_t* data, uint32_t size)
{
	return size >= 12 && MatchTag(data, "FORM") && MatchTag(data + 8, "ILBM");
}

static bool DecodeILBMBodyToPlanes(
	const uint8_t* body,
	uint32_t bodySize,
	uint8_t compression,
	uint16_t width,
	uint16_t height,
	uint8_t planes,
	uint8_t** outputPlanes)
{
	if (planes == 0 || planes > MAX_PLANES || width != SCR_WIDTHPX || height != SCR_HEIGHTPX)
		return false;

	uint32_t rowBytes = ((uint32_t)width + 15u) >> 4;
	rowBytes <<= 1;
	uint32_t decodedSize = rowBytes * planes * height;
	uint8_t* decoded = Allocate<uint8_t>("ILBM body", decodedSize);
	if (decoded == 0)
		return false;

	bool ok = true;
	if (compression == 0)
	{
		if (bodySize < decodedSize)
			ok = false;
		else
			MemCopy(decoded, body, decodedSize);
	}
	else if (compression == 1)
	{
		const uint8_t* src = body;
		const uint8_t* end = body + bodySize;
		uint8_t* dst = decoded;
		uint8_t* dstEnd = decoded + decodedSize;
		while (src < end && dst < dstEnd)
		{
			int8_t control = (int8_t)*src++;
			if (control >= 0)
			{
				uint32_t count = (uint32_t)control + 1;
				if ((uint32_t)(end - src) < count || (uint32_t)(dstEnd - dst) < count)
				{
					ok = false;
					break;
				}
				MemCopy(dst, src, count);
				src += count;
				dst += count;
			}
			else if (control != -128)
			{
				uint32_t count = (uint32_t)(1 - control);
				if (src >= end || (uint32_t)(dstEnd - dst) < count)
				{
					ok = false;
					break;
				}
				MemSet(dst, *src++, count);
				dst += count;
			}
		}
		ok = ok && dst == dstEnd;
	}
	else
	{
		ok = false;
	}

	if (ok)
	{
		for (uint8_t planeIndex = 0; planeIndex < planes; planeIndex++)
			MemClear(outputPlanes[planeIndex], SCR_BPNEXTB);

		for (uint16_t y = 0; y < height; y++)
		{
			const uint8_t* row = decoded + y * rowBytes * planes;
			for (uint8_t planeIndex = 0; planeIndex < planes; planeIndex++)
				MemCopy(outputPlanes[planeIndex] + y * SCR_STRIDEB, row + planeIndex * rowBytes, rowBytes);
		}
	}

	Free(decoded);
	return ok;
}

static bool DisplayAmigaILBMFile(const uint8_t* data, uint32_t fileSize, bool fadeIn)
{
	const uint8_t* ptr = data + 12;
	const uint8_t* end = data + fileSize;
	const uint8_t* body = 0;
	uint32_t bodySize = 0;
	uint16_t ilbmWidth = 0;
	uint16_t ilbmHeight = 0;
	uint8_t ilbmPlanes = 0;
	uint8_t ilbmCompression = 0;
	uint32_t camg = 0;
	bool hasCAMG = false;
	uint16_t colors = 0;
	bool ok = true;
	uint8_t** scratchPlanes = VID_GetIntroScratchPlanes();
	MemClear(ilbmTargetPalette, sizeof(ilbmTargetPalette));

	while (ptr + 8 <= end)
	{
		uint32_t chunkSize = ReadBE32(ptr + 4);
		const uint8_t* chunkData = ptr + 8;
		if (chunkData + chunkSize > end)
		{
			ok = false;
			break;
		}

		if (MatchTag(ptr, "BMHD"))
		{
			if (chunkSize < 20)
			{
				ok = false;
				break;
			}
			ilbmWidth = ReadBE16(chunkData + 0);
			ilbmHeight = ReadBE16(chunkData + 2);
			ilbmPlanes = chunkData[8];
			uint8_t masking = chunkData[9];
			ilbmCompression = chunkData[10];
			if (masking != 0 || ilbmWidth != SCR_WIDTHPX || ilbmHeight != SCR_HEIGHTPX)
			{
				ok = false;
				break;
			}
		}
		else if (MatchTag(ptr, "CMAP"))
		{
			colors = (uint16_t)(chunkSize / 3);
			if (colors == 0 || colors > 256)
			{
				ok = false;
				break;
			}
			for (uint16_t i = 0; i < colors; i++)
			{
				ilbmTargetPalette[i] =
					((uint32_t)chunkData[i * 3 + 0] << 16) |
					((uint32_t)chunkData[i * 3 + 1] << 8) |
					(uint32_t)chunkData[i * 3 + 2];
			}
		}
		else if (MatchTag(ptr, "CAMG"))
		{
			if (chunkSize < 4)
			{
				ok = false;
				break;
			}
			camg = ReadBE32(chunkData);
			hasCAMG = true;
		}
		else if (MatchTag(ptr, "BODY"))
		{
			body = chunkData;
			bodySize = chunkSize;
		}

		ptr = chunkData + chunkSize + (chunkSize & 1u);
	}

	bool isHAM = hasCAMG && (camg & IFF_AMIGA_VIEWMODE_HAM) != 0;
	bool doFadeIn = fadeIn && !isHAM;
	if (ok && (body == 0 || ilbmWidth == 0 || ilbmHeight == 0 || colors == 0))
		ok = false;
	if (ok)
	{
		if (isHAM)
			ok = ilbmPlanes == 6 && colors == 16;
		else if (ilbmPlanes == 4)
			ok = colors <= 16;
		else if (ilbmPlanes == 5)
			ok = colors <= 32;
		else if (ilbmPlanes == 8)
			ok = colors <= 256;
		else
			ok = false;
	}
	if (ok && !VID_IsIntroScreenModeCompatible(ilbmPlanes, isHAM))
		ok = false;
	if (ok && !DecodeILBMBodyToPlanes(body, bodySize, ilbmCompression, ilbmWidth, ilbmHeight, ilbmPlanes, scratchPlanes))
		ok = false;

	if (!ok)
	{
		if (DDB_GetError() == DDB_ERROR_NONE)
			DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	VID_PresentIntroScreen(ilbmTargetPalette, colors, doFadeIn);
	if (doFadeIn)
		FadeInPalette(ilbmTargetPalette, colors);

	return true;
}

static const int fadeSteps = 8;
static const uint16_t fadeScale[fadeSteps] = { 256, 219, 183, 146, 110, 73, 37, 0 };

static void FadeInPalette(const uint32_t* targetPalette, uint16_t count)
{
	for (int frame = 1; frame < fadeSteps; frame++)
	{
		uint16_t scale = fadeScale[fadeSteps - 1 - frame];
		for (uint16_t n = 0; n < count; n++)
		{
			uint32_t v0 = ((targetPalette[n] & 0xFF00FFUL) * scale) >> 8;
			uint32_t v1 = ((targetPalette[n] & 0x00FF00UL) * scale) >> 8;
			fadeInPaletteBuffer[n] = (v0 & 0xFF00FFUL) | (v1 & 0x00FF00UL);
		}
		VID_SetPaletteEntries(fadeInPaletteBuffer, count, 0, false, true);
	}
	VID_SetPaletteEntries(targetPalette, count, 0, false, true);
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	// Special case: handle Amiga directly
	if (target == DDB_MACHINE_AMIGA)
	{
		File* file = File_Open(fileName, ReadOnly);
		if (!file) return false;
		uint32_t fileSize = File_GetSize(file);
		uint8_t* fileData = Allocate<uint8_t>("SCR file", fileSize);
		if (fileData == 0)
		{
			File_Close(file);
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}
		bool ok = File_Read(file, fileData, fileSize) == fileSize;
		File_Close(file);
		if (!ok)
		{
			Free(fileData);
			return false;
		}

		if (HasILBMHeader(fileData, fileSize))
		{
			bool displayed = DisplayAmigaILBMFile(fileData, fileSize, fadeIn);
			Free(fileData);
			return displayed;
		}

		if (fileSize < 32034 || !VID_IsIntroScreenModeCompatible(TEXT_PLANES, false))
		{
			Free(fileData);
			if (DDB_GetError() == DDB_ERROR_NONE)
				DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		uint16_t* palette = (uint16_t*)(fileData + 2);
		uint32_t targetPalette[16];
		uint32_t blackPalette[16] = { 0 };
		for (int n = 0; n < 16; n++)
		{
			uint16_t c = palette[n];
			targetPalette[n] =
				((uint32_t)((c >> 8) & 0x0F) * 0x11 << 16) |
				((uint32_t)((c >> 4) & 0x0F) * 0x11 << 8) |
				(uint32_t)(c & 0x0F) * 0x11;
		}
		uint8_t** scratchPlanes = VID_GetIntroScratchPlanes();
		MemCopy(scratchPlanes[0], fileData + 34, 32000);
		// On a display with more than four planes (HAM6), the extra planes
		// must be clear so every pixel selects a base palette color.
		for (int n = 4; n < 8 && scratchPlanes[n] != 0; n++)
			MemClear(scratchPlanes[n], SCR_BPNEXTB);
		Free(fileData);
		VID_PresentIntroScreen(targetPalette, 16, fadeIn);
		if (fadeIn)
			FadeInPalette(targetPalette, 16);
		return true;
	}

	uint32_t palette[16];
	uint32_t blackPalette[16] = { 0 };
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", 320*200);
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	size_t bufferSize = 32768;

	if (SCR_GetScreen(fileName, target, buffer, bufferSize, 
	                  output, 320, 200, palette, 0))
	{
		uint8_t *in = output;

		if (fadeIn)
			VID_SetPaletteRange(blackPalette, 16, 0, true, true);

		for (unsigned offset = 0; offset < 200*SCR_STRIDEB; offset += SCR_STRIDEB)
		{
			uint16_t *out0 = (uint16_t*)(plane[0] + offset);
			uint16_t *out1 = (uint16_t*)(plane[1] + offset);
			uint16_t *out2 = (uint16_t*)(plane[2] + offset);
			uint16_t *out3 = (uint16_t*)(plane[3] + offset);
			for (int x = 0; x < 320; x += 16)
			{
				uint32_t p0 = 0;
				uint32_t p1 = 0;
				uint32_t mask0 = 0x00008000;
				uint32_t mask1 = 0x80000000;

				do
				{
					const uint8_t  c = *in++;
					if (c & 0x01) p0 |= mask1;
					if (c & 0x02) p0 |= mask0;
					if (c & 0x04) p1 |= mask1;
					if (c & 0x08) p1 |= mask0;
					mask0 >>= 1;
					mask1 >>= 1;
				}
				while (mask0);

				*out0++ = p0 >> 16;
				*out1++ = p0;
				*out2++ = p1 >> 16;
				*out3++ = p1;
			}
		}
		
		if (fadeIn)
			FadeInPalette(palette, 16);
		else
			VID_SetPaletteRange(palette, 16, 0, false, true);
	}

	Free(buffer);
	Free(output);
	return true;
}

#endif
