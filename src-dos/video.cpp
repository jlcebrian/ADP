#ifdef _DOS

#include <ddb_vid.h>
#include <vid_screen.h>
#include <ddb_data.h>
#include <ddb_scr.h>
#include <ddb_xmsg.h>
#include <dmg_font.h>
#include <vid_font.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#include "dmg.h"
#include "sb.h"
#include "mixer.h"
#include "timer.h"
#include "vid_cga.h"
#include "vid_common.h"
#include "vid_ega.h"
#include "vid_modex.h"
#include "vid_vesa.h"

#include <i86.h>
#include <conio.h>
#include <dos.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t*   pictureData = 0;
#if HAS_PCX
static uint8_t*   pcxPictureData = 0;
static uint32_t   pcxPictureSize = 0;
static int        pcxPictureWidth = 0;
static int        pcxPictureHeight = 0;
static uint32_t   pcxPalette[256];
#endif
static uint8_t*   audioData = 0;
static DMG_Entry* bufferedEntry;
static int        bufferedEntryIndex;
static bool       bufferedPictureIndexed = false;
static bool       initialized = false;
static bool       quit;
static uint8_t    defaultCharWidth = 6;
static bool       screen2XMode = false;
static DDB_Version screenVersion = DDB_VERSION_1;
#ifndef DEBUG_DOS_PICTURE_TIMINGS
#define DEBUG_DOS_PICTURE_TIMINGS 0
#endif
#if DEBUG_DOS_PICTURE_TIMINGS
static uint16_t   pictureLoadTraceCount = 0;
static uint16_t   pictureDisplayTraceCount = 0;
#endif

#if HAS_PCX
static bool IsPCXScreenFile(const char* fileName);
#endif

DDB_Machine    screenMachine = DDB_MACHINE_IBMPC;
DDB_ScreenMode screenMode = ScreenMode_VGA16;

bool exitGame = false;
bool supportsOpenFileDialog = false;

const char* VID_DescribeVideoModeError(DDB_Error error, DDB_ScreenMode mode)
{
	if (error == DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED)
	{
		switch (mode)
		{
			case ScreenMode_CGA:    return "No CGA card found";
			case ScreenMode_EGA:    return "No EGA card found";
			case ScreenMode_VGA16:
			case ScreenMode_VGA:    return "No VGA card found";
			case ScreenMode_SHiRes: return "No VESA SVGA adapter found";
			default:                return "No compatible video card found";
		}
	}

	if (error == DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED)
	{
		switch (mode)
		{
			case ScreenMode_HiRes:  return "640x200 mode not supported";
			case ScreenMode_SHiRes: return "640x400 mode not supported";
			case ScreenMode_CGA:    return "CGA mode not supported";
			case ScreenMode_EGA:    return "EGA mode not supported";
			case ScreenMode_VGA16:  return "VGA mode not supported";
			case ScreenMode_VGA:    return "SVGA mode not supported";
			default:                return "Selected video mode not supported";
		}
	}

	return 0;
}

static bool SetDOSVideoMode(DDB_ScreenMode mode)
{
	DebugPrintf("DOSVID: SetDOSVideoMode(mode=%u)\n", (unsigned)mode);
	switch (mode)
	{
		case ScreenMode_CGA:
			DebugPrintf("DOSVID: trying CGA\n");
			if (!CGA_IsAvailable())
			{
				DebugPrintf("DOSVID: CGA not available\n");
				DDB_SetError(DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED);
				return false;
			}
			return CGA_SetVideoMode(mode);

		case ScreenMode_EGA:
			DebugPrintf("DOSVID: trying EGA\n");
			if (!EGA_IsAvailable())
			{
				DebugPrintf("DOSVID: EGA not available\n");
				DDB_SetError(DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED);
				return false;
			}
			return EGA_SetVideoMode();

		case ScreenMode_VGA16:
		case ScreenMode_VGA:
			DebugPrintf("DOSVID: trying ModeX/VGA path\n");
			ModeX_SetVideoMode(MODE_320x200);
			return VID_CommonGetInfo() != 0;

		case ScreenMode_SHiRes:
			DebugPrintf("DOSVID: trying SHiRes/VESA path\n");
#if !defined(__386__)
			DebugPrintf("DOSVID: SHiRes rejected on non-386 build\n");
			DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
			return false;
#else
			if (!VESA_SetVideoMode())
			{
				DebugPrintf("DOSVID: VESA_SetVideoMode() returned false\n");
				DDB_SetError(DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED);
				return false;
			}
			DebugPrintf("DOSVID: SHiRes/VESA mode activated\n");
			return true;
#endif

		default:
			DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
			return false;
	}
}

static bool DOS_HasVGABIOS()
{
	union REGS regs;
	regs.w.ax = 0x1A00;
#if defined(__386__)
	int386(0x10, &regs, &regs);
#else
	int86(0x10, &regs, &regs);
#endif
	return regs.h.al == 0x1A;
}

static uint32_t DOS_DataFileModeFlag(DDB_ScreenMode mode)
{
	switch (mode)
	{
		case ScreenMode_CGA:    return DDB_DataFileMode_CGA;
		case ScreenMode_EGA:    return DDB_DataFileMode_EGA;
		case ScreenMode_VGA16:  return DDB_DataFileMode_VGA16;
		case ScreenMode_VGA:    return DDB_DataFileMode_VGA;
		case ScreenMode_HiRes:  return DDB_DataFileMode_HiRes;
		case ScreenMode_SHiRes: return DDB_DataFileMode_SHiRes;
		default:                return 0;
	}
}

uint32_t VID_GetSupportedDataFileModes()
{
	uint32_t mask = 0;

	if (CGA_IsAvailable())
		mask |= DOS_DataFileModeFlag(ScreenMode_CGA);
	if (EGA_IsAvailable())
		mask |= DOS_DataFileModeFlag(ScreenMode_EGA);
	if (DOS_HasVGABIOS())
	{
		mask |= DOS_DataFileModeFlag(ScreenMode_VGA16);
		mask |= DOS_DataFileModeFlag(ScreenMode_VGA);
	#if defined(__386__)
		if (VESA_IsAvailable())
			mask |= DOS_DataFileModeFlag(ScreenMode_SHiRes);
	#endif
	}

	return mask;
}

static bool IsVGAMode()
{
	return screenMode == ScreenMode_VGA16 || screenMode == ScreenMode_VGA || screenMode == ScreenMode_SHiRes;
}

static void ApplyIBMPCTextMetrics(bool enable2X)
{
	uint8_t baseColumnWidth = (screenVersion == DDB_VERSION_PAWS || screenMachine == DDB_MACHINE_PCW) ? 8 : 6;
	lineHeight = enable2X ? 16 : 8;
	screenCellWidth = enable2X ? 16 : 8;
	columnWidth = enable2X ? (uint8_t)(baseColumnWidth * 2) : baseColumnWidth;
	defaultCharWidth = columnWidth;
	screen2XMode = enable2X;
	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;
#if HAS_HIRES_FONT
	// A metrics change reloads the charset; drop any stale native 16-bit font
	// until the loader stores a fresh one.
	charset16Available = false;
#endif
}

static bool TryDisplayDOSNativeSCRExact(const char* fileName, DDB_Machine target, bool* handled)
{
	*handled = false;
	#if HAS_PCX
	// Some games store PCX data in files using .SCR names.
	// Let VID_DisplaySCRFile route those through the PCX path.
	if (IsPCXScreenFile(fileName))
		return true;
	#endif

	if (target != DDB_MACHINE_IBMPC)
		return true;
	if (screenMode != ScreenMode_CGA && screenMode != ScreenMode_EGA)
		return true;

	const VID_AdapterInfo* info = VID_CommonGetInfo();
	if (info == NULL)
		return true;
	if (info->nativeImageMode != ImageMode_CGA && info->nativeImageMode != ImageMode_Planar)
		return true;

	File* file = File_Open(fileName, ReadOnly);
	if (file == NULL)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}

	uint64_t size = File_GetSize(file);
	bool isCGAFile = size <= 16384;
	bool isEGAFile = size > 16384 && size <= 32048;

	if (info->nativeImageMode == ImageMode_CGA)
	{
		if (!isCGAFile)
		{
			File_Close(file);
			return true;
		}

		if (size < 16000)
		{
			File_Close(file);
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		uint8_t* fileData = Allocate<uint8_t>("Temporary CGA SCR file", (uint32_t)size);
		if (fileData == NULL)
		{
			File_Close(file);
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}

		if (File_Read(file, fileData, size) != size)
		{
			File_Close(file);
			Free(fileData);
			DDB_SetError(DDB_ERROR_READING_FILE);
			return false;
		}
		File_Close(file);

		uint32_t nativeSize = VID_CommonGetNativeImageSize(screenWidth, screenHeight);
		if (nativeSize == 0)
		{
			Free(fileData);
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		uint8_t* native = Allocate<uint8_t>("Temporary CGA native SCR", nativeSize);
		if (native == NULL)
		{
			Free(fileData);
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}

		for (int y = 0; y < screenHeight; y++)
		{
			const uint8_t* srcRow = fileData + (uint32_t)(80 * (y >> 1) + 8192 * (y & 1));
			uint8_t* dstRow = native + (uint32_t)y * 80;
			MemCopy(dstRow, srcRow, 80);
		}

		VID_CommonBlitNativeImage(native, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight);
		Free(native);
		Free(fileData);
		*handled = true;
		return true;
	}

	if (!isEGAFile)
	{
		File_Close(file);
		return true;
	}

	if (size < 32001)
	{
		File_Close(file);
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}

	uint8_t* fileData = Allocate<uint8_t>("Temporary EGA SCR file", (uint32_t)size);
	if (fileData == NULL)
	{
		File_Close(file);
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}

	if (File_Read(file, fileData, size) != size)
	{
		File_Close(file);
		Free(fileData);
		DDB_SetError(DDB_ERROR_READING_FILE);
		return false;
	}
	File_Close(file);

	VID_CommonBlitNativeImage(fileData + 1, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight);
	Free(fileData);
	*handled = true;
	return true;
}

void VID_SetWindowTitle(const char* title)
{
	(void)title;
}

void VID_SetWindowIcon(const char* fileName)
{
	(void)fileName;
}

#if DEBUG_ALLOCS
extern void DOS_GetStackWatermark(uint32_t* usedBytes, uint32_t* totalBytes);
#endif

// ----------------------------------------------------------------------------
//  File stuff
// ----------------------------------------------------------------------------

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

// Charset/font loading lives in the shared src-common/vid_font.cpp; the DOS
// renderer needs no post-load work, so its VID_ActivateCharset hook is empty.
void VID_ActivateCharset() {}

#if HAS_HIRES_FONT
uint8_t charset16[256 * 32];
bool    charset16Available = false;

// Called by the shared font loader when a 2x mode is active: store native 16-bit
// V4 glyphs (crisp), or the 8-bit glyphs with 2x-widened advances; return false
// to let the loader fall back to the plain 8-bit path.
bool VID_StoreFont2X(const DMG_Font* font, const char* filename)
{
	if (!screen2XMode)
		return false;

	if (DMG_IsSINTACFontV4(filename))
	{
		MemCopy(charset16, font->bitmap16, sizeof(font->bitmap16));
		MemCopy(charWidth, font->width16, sizeof(font->width16));
		charset16Available = true;
	}
	else
	{
		MemCopy(charset, font->bitmap8, sizeof(font->bitmap8));
		for (int n = 0; n < 256; n++)
			charWidth[n] = (uint8_t)(font->width8[n] * 2);
		charset16Available = false;
	}
	return true;
}
#endif

#if HAS_PCX
static bool FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == NULL)
		return false;
	File_Close(file);
	return true;
}

static void BuildFileNameWithExtension(const char* fileName, const char* extension, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, fileName);
	char* dot = (char*)StrRChr(output, '.');
	if (dot == NULL)
		dot = output + StrLen(output);
	StrCopy(dot, output + outputSize - dot, extension);
}

static bool FindNamedPCXFile(const char* fileName, char* output, size_t outputSize)
{
	BuildFileNameWithExtension(fileName, ".VGA", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".vga", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".PCX", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".pcx", output, outputSize);
	return FileExists(output);
}

static void FreeBufferedPCXPicture()
{
	if (pcxPictureData != NULL)
	{
		Free(pcxPictureData);
		pcxPictureData = NULL;
	}
	pcxPictureSize = 0;
	pcxPictureWidth = 0;
	pcxPictureHeight = 0;
}

static bool HasExternalPCXGraphics(const char* fileName)
{
	char introScreen[FILE_MAX_PATH];
	if (FindNamedPCXFile(fileName, introScreen, sizeof(introScreen)))
		return true;

	for (int picno = 0; picno < 256; picno++)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (VID_GetExternalPictureFileName((uint8_t)picno, pictureFileName, sizeof(pictureFileName)))
			return true;
	}

	return false;
}

static bool IsPCXScreenFile(const char* fileName)
{
	const char* dot = StrRChr(fileName, '.');
	if (dot != NULL && (StrIComp(dot, ".vga") == 0 || StrIComp(dot, ".pcx") == 0))
		return true;

	uint8_t header[128];
	File* file = File_Open(fileName, ReadOnly);
	if (file == NULL)
		return false;
	bool ok = File_Read(file, header, sizeof(header)) == sizeof(header);
	File_Close(file);
	return ok && header[0] == 0x0A && header[2] == 1 && header[3] == 8 && header[65] == 1;
}
#endif

static uint32_t GetIndexedXImageSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return 0;
	return ((uint32_t)(width + 3) & ~3u) * (uint32_t)height;
}

static bool ConvertIndexedToIndexedX(const uint8_t* input, int width, int height, uint8_t* output, uint32_t outputSize)
{
	uint32_t requiredSize = GetIndexedXImageSize(width, height);
	if (input == NULL || output == NULL || outputSize < requiredSize || width <= 0 || height <= 0)
		return false;

	uint32_t bands = ((uint32_t)width + 3u) >> 2;
	uint32_t rowStride = bands * 4u;
	for (int y = 0; y < height; y++)
	{
		const uint8_t* srcRow = input + (uint32_t)y * (uint32_t)width;
		uint8_t* dstRow = output + (uint32_t)y * rowStride;
		for (uint32_t band = 0; band < bands; band++)
		{
			uint32_t pixelBase = band << 2;
			dstRow[band + bands * 0u] = pixelBase + 0u < (uint32_t)width ? srcRow[pixelBase + 0u] : 0;
			dstRow[band + bands * 1u] = pixelBase + 1u < (uint32_t)width ? srcRow[pixelBase + 1u] : 0;
			dstRow[band + bands * 2u] = pixelBase + 2u < (uint32_t)width ? srcRow[pixelBase + 2u] : 0;
			dstRow[band + bands * 3u] = pixelBase + 3u < (uint32_t)width ? srcRow[pixelBase + 3u] : 0;
		}
	}

	return true;
}

static uint32_t GetCGAImageSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return 0;
	return (((uint32_t)width + 3u) >> 2) * (uint32_t)height;
}

static bool ConvertIndexedToCGA(const uint8_t* input, int width, int height, uint8_t* output, uint32_t outputSize)
{
	uint32_t requiredSize = GetCGAImageSize(width, height);
	if (input == NULL || output == NULL || outputSize < requiredSize || width <= 0 || height <= 0)
		return false;

	uint32_t dstStride = ((uint32_t)width + 3u) >> 2;
	for (int y = 0; y < height; y++)
	{
		const uint8_t* srcRow = input + (uint32_t)y * (uint32_t)width;
		uint8_t* dstRow = output + (uint32_t)y * dstStride;
		for (uint32_t x = 0, byteX = 0; x < (uint32_t)width; x += 4u, byteX++)
		{
			uint8_t c0 = srcRow[x] & 0x03;
			uint8_t c1 = x + 1u < (uint32_t)width ? (srcRow[x + 1u] & 0x03) : 0;
			uint8_t c2 = x + 2u < (uint32_t)width ? (srcRow[x + 2u] & 0x03) : 0;
			uint8_t c3 = x + 3u < (uint32_t)width ? (srcRow[x + 3u] & 0x03) : 0;
			dstRow[byteX] = (uint8_t)((c0 << 6) | (c1 << 4) | (c2 << 2) | c3);
		}
	}

	return true;
}

static uint32_t GetPlanar4ImageSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return 0;
	return (((uint32_t)width + 15u) >> 4) * 2u * (uint32_t)height * 4u;
}

static bool ConvertIndexedToPlanar4(const uint8_t* input, int width, int height, uint8_t* output, uint32_t outputSize)
{
	uint32_t requiredSize = GetPlanar4ImageSize(width, height);
	if (input == NULL || output == NULL || outputSize < requiredSize || width <= 0 || height <= 0)
		return false;

	MemClear(output, requiredSize);
	uint32_t stride = (((uint32_t)width + 15u) >> 4) * 2u;
	uint32_t planeSize = stride * (uint32_t)height;
	for (int y = 0; y < height; y++)
	{
		const uint8_t* srcRow = input + (uint32_t)y * (uint32_t)width;
		for (int plane = 0; plane < 4; plane++)
		{
			uint8_t* dstRow = output + planeSize * (uint32_t)plane + (uint32_t)y * stride;
			uint8_t planeBit = (uint8_t)(1 << plane);
			for (int x = 0; x < width; x++)
			{
				if ((srcRow[x] & planeBit) != 0)
					dstRow[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
			}
		}
	}

	return true;
}

#if HAS_PCX
static bool LoadNativePCXData(const char* fileName, uint8_t** output, uint32_t* outputSize, int* width, int* height, uint32_t* paletteOut)
{
	if (output == NULL || outputSize == NULL || width == NULL || height == NULL || paletteOut == NULL)
		return false;

	*output = NULL;
	*outputSize = 0;
	*width = 0;
	*height = 0;

	uint32_t indexedSize = 0;
	int indexedWidth = 0;
	int indexedHeight = 0;
	if (DMG_DecompressPCX(fileName, NULL, &indexedSize, &indexedWidth, &indexedHeight, paletteOut) ||
		DMG_GetError() != DMG_ERROR_BUFFER_TOO_SMALL)
	{
		return false;
	}

	uint8_t* indexed = Allocate<uint8_t>("PCX indexed picture", indexedSize);
	if (indexed == NULL)
	{
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return false;
	}

	if (!DMG_DecompressPCX(fileName, indexed, &indexedSize, &indexedWidth, &indexedHeight, paletteOut))
	{
		Free(indexed);
		return false;
	}

	const VID_AdapterInfo* info = VID_CommonGetInfo();
	if (info != NULL && info->nativeImageMode == ImageMode_Indexed)
	{
		*output = indexed;
		*outputSize = indexedSize;
		*width = indexedWidth;
		*height = indexedHeight;
		return true;
	}

	uint32_t nativeSize = GetIndexedXImageSize(indexedWidth, indexedHeight);
	uint8_t* native = Allocate<uint8_t>("PCX IndexedX picture", nativeSize);
	if (native == NULL)
	{
		Free(indexed);
		DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
		return false;
	}

	if (!ConvertIndexedToIndexedX(indexed, indexedWidth, indexedHeight, native, nativeSize))
	{
		Free(native);
		Free(indexed);
		DMG_SetError(DMG_ERROR_INVALID_IMAGE);
		return false;
	}

	Free(indexed);
	*output = native;
	*outputSize = nativeSize;
	*width = indexedWidth;
	*height = indexedHeight;
	return true;
}
#endif

static bool RequireAlignedPictureX(int x)
{
	return VID_ScreenRequireAlignedX(x);
}

// ----------------------------------------------------------------------------
//  Video related variables
// ----------------------------------------------------------------------------
static uint32_t palette[256];
static uint8_t  paletteR[256];
static uint8_t  paletteG[256];
static uint8_t  paletteB[256];
static uint8_t  fg;
static int      cx;
static int      cy;

#if defined(__386__)
struct DPMI_MemoryInfo
{
	uint32_t largestBlockAvail;
	uint32_t maxUnlockedPage;
	uint32_t largestLockablePage;
	uint32_t linearAddressSpace;
	uint32_t numFreePagesAvail;
	uint32_t numPhysicalPagesFree;
	uint32_t totalPhysicalPages;
	uint32_t freeLinearAddressSpace;
	uint32_t sizeOfPageFile;
	uint32_t reserved[3];
};

static DPMI_MemoryInfo dpmiMemInfo;
#endif

static void GetDOSFreeMemoryInfo(uint32_t* totalFree, uint32_t* largestBlock)
{
#if defined(__386__)
	union REGS regs;
	struct SREGS sregs;

	MemSet(&dpmiMemInfo, 0xFF, sizeof(dpmiMemInfo));
	MemClear(&regs, sizeof(regs));
	MemClear(&sregs, sizeof(sregs));

	regs.x.eax = 0x00000500UL;
	sregs.es = FP_SEG(&dpmiMemInfo);
	regs.x.edi = FP_OFF(&dpmiMemInfo);

	int386x(0x31, &regs, &regs, &sregs);

	if (dpmiMemInfo.largestBlockAvail != 0xFFFFFFFFUL)
	{
		if (largestBlock != NULL)
			*largestBlock = dpmiMemInfo.largestBlockAvail;

		if (totalFree != NULL)
		{
			if (dpmiMemInfo.numFreePagesAvail != 0xFFFFFFFFUL)
				*totalFree = dpmiMemInfo.numFreePagesAvail * 4096UL;
			else
				*totalFree = dpmiMemInfo.largestBlockAvail;
		}
	}
	else
	{
		if (totalFree != NULL)
			*totalFree = 0;
		if (largestBlock != NULL)
			*largestBlock = 0;
	}
#else
	unsigned segments[128];
	unsigned segmentCount = 0;
	uint32_t total = 0;
	uint32_t largest = 0;

	for (;;)
	{
		unsigned paragraphs = 0;
		if (_dos_allocmem(0xFFFFu, &paragraphs) == 0)
		{
			// Unexpected full-size success. Account for it and stop.
			segments[segmentCount++] = paragraphs;
			total += 0xFFFFUL * 16UL;
			if (largest < 0xFFFFUL * 16UL)
				largest = 0xFFFFUL * 16UL;
			break;
		}

		if (paragraphs == 0)
			break;

		uint32_t blockBytes = (uint32_t)paragraphs * 16UL;
		if (blockBytes > largest)
			largest = blockBytes;

		if (segmentCount == sizeof(segments) / sizeof(segments[0]))
			break;

		unsigned segment = 0;
		if (_dos_allocmem(paragraphs, &segment) != 0)
			break;

		segments[segmentCount++] = segment;
		total += blockBytes;
	}

	while (segmentCount > 0)
		_dos_freemem(segments[--segmentCount]);

	if (totalFree != NULL)
		*totalFree = total;
	if (largestBlock != NULL)
		*largestBlock = largest;
#endif
}

static uint32_t GetDesiredDOSImageCacheSize(uint32_t totalFree, uint32_t largestBlock)
{
	uint32_t available = largestBlock != 0 ? largestBlock : totalFree;
	if (available >= 256UL * 1024UL)
		return 128UL * 1024UL;
	if (available >= 160UL * 1024UL)
		return 96UL * 1024UL;
	if (available >= 128UL * 1024UL)
		return 64UL * 1024UL;
	if (available >= 96UL * 1024UL)
		return 48UL * 1024UL;
	if (available >= 64UL * 1024UL)
		return 32UL * 1024UL;
	if (available >= 48UL * 1024UL)
		return 16UL * 1024UL;

	DebugPrintf("Warning: not enough memory available (%lu bytes) for image cache\n", available);
	return 0;
}

#if DEBUG_ALLOCS
#endif

// ----------------------------------------------------------------------------
//  Video driver
// ----------------------------------------------------------------------------

void VID_VSync()
{
	/* Wait for vertical retrace */
	while (!(inp(0x3DA) & 0x08))			// IST1
		;		
	while (inp(0x3DA) & 0x08)				// IST1
		;		
}

static void SetHardwarePaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	outp(0x3C8, index);					// PAL WRITE
	outp(0x3C9, r >> 2);				// PAL DATA
	outp(0x3C9, g >> 2);
	outp(0x3C9, b >> 2);
}

void VID_SetPaletteColor (uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	palette[index] = 0xFF000000UL | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
	paletteR[index] = r;
	paletteG[index] = g;
	paletteB[index] = b;
	if (IsVGAMode())
		SetHardwarePaletteColor(index, r, g, b);
}

void VID_ActivatePalette()
{
	if (!IsVGAMode())
		return;
	for (int n = 0; n < 256; n++)
		SetHardwarePaletteColor((uint8_t)n, paletteR[n], paletteG[n], paletteB[n]);
}

void VID_WaitForKey()
{
	while (!VID_AnyKey())
		;
}

bool VID_AnyKey ()
{
	return kbhit();
}

static void ApplyPalette256(const uint32_t* colors)
{
	for (int n = 0; n < 256; n++)
	{
		uint8_t r = (colors[n] >> 16) & 0xFF;
		uint8_t g = (colors[n] >> 8) & 0xFF;
		uint8_t b = colors[n] & 0xFF;
		VID_SetPaletteColor((uint8_t)n, r, g, b);
	}
}

static void StagePaletteEntries(const uint32_t* colors, uint16_t count, uint16_t firstColor)
{
	uint16_t paletteSize = VID_GetPaletteSize();
	if (firstColor > paletteSize)
		return;
	if (count > paletteSize - firstColor)
		count = paletteSize - firstColor;

	for (uint16_t i = 0; i < count; i++)
	{
		uint32_t color = colors[i];
		uint16_t index = firstColor + i;
		palette[index] = 0xFF000000UL | (color & 0x00FFFFFFUL);
		paletteR[index] = (uint8_t)((color >> 16) & 0xFF);
		paletteG[index] = (uint8_t)((color >> 8) & 0xFF);
		paletteB[index] = (uint8_t)(color & 0xFF);
	}
}

bool VID_LoadDataFile (const char* fileName)
{
	#if HAS_PCX
		FreeBufferedPCXPicture();
		VID_SetExternalPictureBase(0);
	#endif
	bufferedPictureIndexed = false;

	columnWidth = 6;
	lineHeight = 8;
	screenCellWidth = 8;
	screen2XMode = false;
	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;
	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);

	if (dmg != NULL)
	{
		DMG_Close(dmg);
		dmg = NULL;
	}

	char resolvedDataFile[FILE_MAX_PATH];
	DDB_ScreenMode resolvedDataMode = screenMode;
	bool resolved = DDB_ResolveDataFile(fileName, DDB_MACHINE_IBMPC, screenMode, resolvedDataFile, sizeof(resolvedDataFile), &resolvedDataMode, 0);
	if (!resolved && screenMode != ScreenMode_Default)
		resolved = DDB_ResolveDataFile(fileName, DDB_MACHINE_IBMPC, ScreenMode_Default, resolvedDataFile, sizeof(resolvedDataFile), &resolvedDataMode, 0);
	if (resolved)
	{
		dmg = DMG_Open(resolvedDataFile, true);
		if (dmg != NULL)
		{
			dmg->screenMode = resolvedDataMode;
			DebugPrintf("DOSVID: loaded data file %s version=%u colorMode=%u size=%ux%u flags=0x%02X resolvedMode=%u\n",
				resolvedDataFile,
				(unsigned)dmg->version,
				(unsigned)dmg->colorMode,
				(unsigned)dmg->targetWidth,
				(unsigned)dmg->targetHeight,
				(unsigned)dmg->dat5Flags,
				(unsigned)resolvedDataMode);
		}
		if (resolvedDataMode != screenMode)
		{
			if (!SetDOSVideoMode(resolvedDataMode))
			{
				VID_Finish();
				return false;
			}
			const VID_AdapterInfo* info = VID_CommonGetInfo();
			screenWidth = info != 0 ? info->width : 320;
			screenHeight = info != 0 ? info->height : 200;
			lineHeight = info != 0 ? info->cellHeight : 8;
			columnWidth = info != 0 ? info->cellWidth : defaultCharWidth;
			defaultCharWidth = columnWidth;
		}
		screenMode = resolvedDataMode;
	}

	if (dmg != NULL && screenMachine == DDB_MACHINE_IBMPC)
	{
		ApplyIBMPCTextMetrics(
			dmg->version == DMG_Version5 &&
			dmg->targetWidth == 640 &&
			dmg->targetHeight == 400 &&
			(dmg->dat5Flags & DMG_DAT5_FLAG_2X) != 0);
	}
	if (dmg == NULL)
	{
		#if HAS_PCX
		VID_SetExternalPictureBase(fileName);
		if (HasExternalPCXGraphics(fileName))
		{
			columnWidth = 8;
			if (!SCR_LoadSINTACFont(ChangeExtension(fileName, ".FNT")) &&
				!SCR_LoadSINTACFont(ChangeExtension(fileName, ".fnt")) &&
				!SCR_LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
				!SCR_LoadCharset(charset, ChangeExtension(fileName, ".chr")))
			{
				// Keep the default fixed-width charset restored above.
			}
			return true;
		}
		VID_SetExternalPictureBase(0);
		#endif

		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		VID_Finish();
		return false;
	}

	if (!SCR_LoadSINTACFont(ChangeExtension(fileName, ".FNT")) &&
		!SCR_LoadSINTACFont(ChangeExtension(fileName, ".fnt")) &&
		!SCR_LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!SCR_LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		// Keep the default fixed-width charset restored above.
	}

	#if HAS_XMSG
	DDB_InitializeXMessageCache(32768);
	#endif

	uint32_t totalFree = 0;
	uint32_t largestBlock = 0;
	GetDOSFreeMemoryInfo(&totalFree, &largestBlock);
	DebugPrintf("Free RAM: %lu bytes (%lu largest block)\n", totalFree, largestBlock);
	uint32_t imageCacheBytes = GetDesiredDOSImageCacheSize(totalFree, largestBlock);
	if (imageCacheBytes == 0)
	{
		DebugPrintf("Unable to get image cache size\n");
		VID_Finish();
		abort();
	}
	if (imageCacheBytes != 0)
		DMG_SetupImageCache(dmg, imageCacheBytes);

	return true;
}

bool VID_IsBackBufferEnabled()
{
	return true;
}

void VID_EnableBackBuffer()
{
	// Always enabled
}

void VID_Clear (int x, int y, int width, int height, uint8_t color, VID_ClearMode mode)
{
	VID_CommonClear(x, y, width, height, color, mode);
}

void VID_Finish()
{
	if (!initialized)
		return;

	#if HAS_PCX
	FreeBufferedPCXPicture();
	VID_SetExternalPictureBase(0);
	#endif

	SB_Stop();
	Timer_Stop();

	VID_Clear(0, 0, screenWidth, screenHeight, 0);
	VID_VSync();

	CGA_Shutdown();
	ModeX_SetVideoMode(MODE_TEXT);
	initialized = false;
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode screenMode)
{	
	#if DEBUG_DOS_PICTURE_TIMINGS
	uint16_t traceIndex = pictureDisplayTraceCount;
	bool traceThisPicture = traceIndex < 96;
	uint32_t tDisplayStart = 0;
	uint32_t tAfterBlit = 0;
	uint32_t tAfterPalette = 0;
	uint32_t tDisplayEnd = 0;

	if (traceThisPicture)
		VID_GetMilliseconds(&tDisplayStart);

	if (traceThisPicture)
		DebugPrintf("DOSVID: DisplayPicture(x=%d y=%d w=%d h=%d mode=%u indexed=%u)\n",
			x, y, w, h, (unsigned)screenMode, bufferedPictureIndexed ? 1u : 0u);
	pictureDisplayTraceCount++;
	#endif

	(void)screenMode;
	if (!RequireAlignedPictureX(x))
	{
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
			DebugPrintf("DOSVID: DisplayPicture skipped due to X alignment (x=%d)\n", x);
		#endif
		return;
	}

	#if HAS_PCX
	if (pcxPictureData != NULL)
	{
		if (w > pcxPictureWidth)
			w = pcxPictureWidth;
		if (h > pcxPictureHeight)
			h = pcxPictureHeight;
		bool useScratchPresentation = IsVGAMode() && VID_CommonBeginFixedPicturePresentation();
		VID_CommonBlitNativeImage(pcxPictureData, pcxPictureWidth, pcxPictureHeight, x, y, w, h);
		if (useScratchPresentation)
		{
			StagePaletteEntries(pcxPalette, 256, 0);
			VID_CommonEndFixedPicturePresentation(pcxPalette, 256, 0);
		}
		else
			ApplyPalette256(pcxPalette);
		return;
	}
	#endif

	DMG_Entry* entry = bufferedEntry;
	if (entry == NULL)
	{
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
			DebugPrintf("DOSVID: DisplayPicture skipped (no buffered entry)\n");
		#endif
		return;
	}
	if (pictureData == NULL)
	{
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
			DebugPrintf("DOSVID: DisplayPicture skipped (pictureData is NULL for pic %u)\n", (unsigned)bufferedEntryIndex);
		#endif
		return;
	}

	if (w > entry->width)
		w = entry->width;
	if (h > entry->height)
		h = entry->height;
	if (w <= 0 || h <= 0)
		return;

	if (screenMode == ScreenMode_CGA)
		CGA_SetPaletteRed(DMG_GetCGAMode(entry) == CGA_Red);
		
	bool updatePalette = (entry->flags & DMG_FLAG_FIXED) != 0;
	bool useScratchPresentation = false;

	if (bufferedPictureIndexed)
	{
		int srcX = 0;
		int srcY = 0;

		if (x < 0)
		{
			srcX = -x;
			w += x;
			x = 0;
		}
		if (y < 0)
		{
			srcY = -y;
			h += y;
			y = 0;
		}
		if (x >= screenWidth || y >= screenHeight || w <= 0 || h <= 0)
			return;

		if (w > entry->width - srcX)
			w = entry->width - srcX;
		if (h > entry->height - srcY)
			h = entry->height - srcY;
		if (w > screenWidth - x)
			w = screenWidth - x;
		if (h > screenHeight - y)
			h = screenHeight - y;
		if (w <= 0 || h <= 0)
			return;

		useScratchPresentation = updatePalette && VID_CommonBeginFixedPicturePresentation();
		const uint8_t* src = pictureData + (uint32_t)srcY * entry->width + srcX;
		VID_CommonBlitIndexedImage(src, entry->width, x, y, w, h);
	}
	else
	{
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
			DebugPrintf("DOSVID: DisplayPicture native blit size=%dx%d entry=%dx%d\n",
				w, h, entry->width, entry->height);
		#endif
		useScratchPresentation = updatePalette && VID_CommonBeginFixedPicturePresentation();
		VID_CommonBlitNativeImage(pictureData, entry->width, entry->height, x, y, w, h);
	}

	#if DEBUG_DOS_PICTURE_TIMINGS
	if (traceThisPicture)
		VID_GetMilliseconds(&tAfterBlit);
	#endif

	if (updatePalette)
	{
		uint32_t* picturePalette = DMG_GetEntryPalette(dmg, bufferedEntryIndex);
		if (dmg->version == DMG_Version1)
			picturePalette[15] = 0xFFFFFFFF;
		if (IsVGAMode())
		{
			int paletteCount = DMG_GetEntryPaletteSize(dmg, bufferedEntryIndex);
			int firstColor = DMG_GetEntryFirstColor(dmg, bufferedEntryIndex);
			if (paletteCount < 0)
				paletteCount = 0;
			if (firstColor < 0)
				firstColor = 0;
			if (firstColor > 255)
				firstColor = 255;
			if (paletteCount > 256 - firstColor)
				paletteCount = 256 - firstColor;

			if (useScratchPresentation)
				StagePaletteEntries(picturePalette, (uint16_t)paletteCount, (uint16_t)firstColor);
			else
				VID_SetPaletteEntries(picturePalette, (uint16_t)paletteCount, (uint16_t)firstColor, false, true);
		}
	}

	#if DEBUG_DOS_PICTURE_TIMINGS
	if (traceThisPicture)
		VID_GetMilliseconds(&tAfterPalette);
	#endif

	if (useScratchPresentation)
	{
		uint32_t* picturePalette = DMG_GetEntryPalette(dmg, bufferedEntryIndex);
		uint16_t paletteCount = DMG_GetEntryPaletteSize(dmg, bufferedEntryIndex);
		uint16_t firstColor = DMG_GetEntryFirstColor(dmg, bufferedEntryIndex);
		if (firstColor > 255)
			firstColor = 255;
		if (paletteCount > 256 - firstColor)
			paletteCount = 256 - firstColor;
		VID_CommonEndFixedPicturePresentation(picturePalette, paletteCount, firstColor);
	}

	#if DEBUG_DOS_PICTURE_TIMINGS
	if (traceThisPicture)
	{
		VID_GetMilliseconds(&tDisplayEnd);
		DebugPrintf("DOSVID: DisplayPicture timing blit=%lu ms palette=%lu ms tail=%lu ms total=%lu ms fixed=%u\n",
			(unsigned long)(tAfterBlit - tDisplayStart),
			(unsigned long)(tAfterPalette - tAfterBlit),
			(unsigned long)(tDisplayEnd - tAfterPalette),
			(unsigned long)(tDisplayEnd - tDisplayStart),
			updatePalette ? 1u : 0u);
	}
	#endif
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	bool handledNative = false;
	if (!TryDisplayDOSNativeSCRExact(fileName, target, &handledNative))
		return false;
	if (handledNative)
		return true;

	#if HAS_PCX
	if (IsPCXScreenFile(fileName))
	{
		uint8_t* output = NULL;
		uint32_t bufferSize = 0;
		int width = 0;
		int height = 0;
		if (!LoadNativePCXData(fileName, &output, &bufferSize, &width, &height, pcxPalette))
		{
			return false;
		}

		VID_CommonBlitNativeImage(output, width, height, 0, 0, width, height);
		ApplyPalette256(pcxPalette);
		Free(output);
		return true;
	}
	#endif

	size_t required = screenWidth * screenHeight;
	uint32_t palette[16];
	uint8_t* output = Allocate<uint8_t>("Temporary SCR buffer", required);
	if (output == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	uint8_t* buffer = Allocate<uint8_t>("Temporary SCR buffer", 32768);
	if (buffer == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		Free(output);
		return false;
	}

	bool decoded = SCR_GetScreen(fileName, target, buffer, 32768, output,
			screenWidth, screenHeight, palette, 0);
	if (decoded)
	{
		const VID_AdapterInfo* info = VID_CommonGetInfo();
		if (info != NULL && info->nativeImageMode == ImageMode_CGA)
		{
			uint32_t nativeSize = VID_CommonGetNativeImageSize(screenWidth, screenHeight);
			if (nativeSize == 0)
			{
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				Free(output);
				Free(buffer);
				return false;
			}
			uint8_t* native = Allocate<uint8_t>("Temporary CGA SCR buffer", nativeSize);
			if (native == NULL)
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				Free(output);
				Free(buffer);
				return false;
			}
			if (!ConvertIndexedToCGA(output, screenWidth, screenHeight, native, nativeSize))
			{
				Free(native);
				Free(output);
				Free(buffer);
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				return false;
			}
			VID_CommonBlitNativeImage(native, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight);
			Free(native);
		}
		else if (info != NULL && info->nativeImageMode == ImageMode_Planar)
		{
			uint32_t nativeSize = VID_CommonGetNativeImageSize(screenWidth, screenHeight);
			if (nativeSize == 0)
			{
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				Free(output);
				Free(buffer);
				return false;
			}
			uint8_t* native = Allocate<uint8_t>("Temporary EGA SCR buffer", nativeSize);
			if (native == NULL)
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				Free(output);
				Free(buffer);
				return false;
			}
			if (!ConvertIndexedToPlanar4(output, screenWidth, screenHeight, native, nativeSize))
			{
				Free(native);
				Free(output);
				Free(buffer);
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				return false;
			}
			VID_CommonBlitNativeImage(native, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight);
			Free(native);
		}
			else if (info != NULL && info->nativeImageMode == ImageMode_IndexedX)
			{
				uint32_t nativeSize = VID_CommonGetNativeImageSize(screenWidth, screenHeight);
				if (nativeSize == 0)
				{
					DDB_SetError(DDB_ERROR_INVALID_FILE);
					Free(output);
					Free(buffer);
					return false;
				}
				uint8_t* native = Allocate<uint8_t>("Temporary IndexedX SCR buffer", nativeSize);
				if (native == NULL)
				{
					DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
					Free(output);
					Free(buffer);
					return false;
				}
				if (!ConvertIndexedToIndexedX(output, screenWidth, screenHeight, native, nativeSize))
				{
					Free(native);
					Free(output);
					Free(buffer);
					DDB_SetError(DDB_ERROR_INVALID_FILE);
					return false;
				}

				if (fadeIn)
				{
					for (int n = 0; n < 16; n++)
						VID_SetPaletteColor(n, 0, 0, 0);
					VID_VSync();
				}

				VID_CommonBlitNativeImage(native, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight);

				if (fadeIn)
				{
					VID_VSync();
					for (int n = 0; n < 16; n++)
					{
						uint8_t r = (palette[n] >> 16) & 0xFF;
						uint8_t g = (palette[n] >>  8) & 0xFF;
						uint8_t b = (palette[n] >>  0) & 0xFF;
						VID_SetPaletteColor(n, r, g, b);
					}
				}
				Free(native);
			}
			else
		{
			VID_CommonBlitIndexedImage(output, screenWidth, 0, 0, screenWidth, screenHeight);
		}
	}

	Free(output);
	Free(buffer);
	return decoded;
}

void VID_DrawCharacter (int x, int y, uint8_t c, uint8_t ink, uint8_t paper)
{
	VID_CommonDrawTextSpan(x, y, &c, 1, ink, paper);
}

void VID_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	VID_CommonDrawTextSpan(x, y, text, length, ink, paper);
}

void VID_DrawText(int x, int y, const char* text, uint8_t ink, uint8_t paper)
{
	VID_DrawTextSpan(x, y, (const uint8_t*)text, (uint16_t)StrLen(text), ink, paper);
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* modifiers)
{
	union REGS regs;
	regs.h.ah = 0x02;
#if defined(__386__)
	int386(0x16, &regs, &regs);
#else
	int86(0x16, &regs, &regs);
#endif

	*key = 0;
	*ext = 0;

	if (kbhit())
	{
		*key = getch();
		if (*key == 0)
		{
			*ext = getch();

			if (*ext == 0x44 && interpreter)
			{
				if (++interpreter->keyClick == 3)
					interpreter->keyClick = 0;
			}
		}
	}

	*modifiers = 0;
	if (regs.h.al & 0x03)
		*modifiers |= SCR_KEYMOD_SHIFT;
	if (regs.h.al & 0x04)
		*modifiers |= SCR_KEYMOD_CTRL;
	if (regs.h.al & 0x08)
		*modifiers |= SCR_KEYMOD_ALT;

#if DEBUG_ALLOCS
	if (*ext == 0x3C)	// F2
	{
		uint32_t totalFree = 0;
		uint32_t largestBlock = 0;
		uint32_t stackUsed = 0;
		uint32_t stackTotal = 0;
		GetDOSFreeMemoryInfo(&totalFree, &largestBlock);
		DOS_GetStackWatermark(&stackUsed, &stackTotal);
		DumpMemory(totalFree, largestBlock, stackUsed, stackTotal);
		if (key) *key = 0;
		if (ext) *ext = 0;
		*ext = 0;
		*key = 0;
	}
#endif
}

void VID_GetMilliseconds (uint32_t* time)
{
	*time = Timer_GetMilliseconds();

	// union REGS regs;
	// regs.h.ah = 0x00;
	// int386(0x1A, &regs, &regs);
	// *time = ((regs.w.cx << 16) + regs.w.dx) * 55;
}

uint16_t VID_GetPaletteSize()
{
	const VID_ScreenAdapterInfo* info = VID_ScreenGetInfo();
	return info != 0 ? info->paletteSize : 0;
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	if (!IsVGAMode())
	{
		if (r != NULL) *r = paletteR[color];
		if (g != NULL) *g = paletteG[color];
		if (b != NULL) *b = paletteB[color];
		return;
	}
	outp(0x3C7, color);

	if (r != NULL)
	{
		uint8_t v = inp(0x3C9);
		*r = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}

	if (g != NULL)
	{
		uint8_t v = inp(0x3C9);
		*g = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}

	if (b != NULL)
	{
		uint8_t v = inp(0x3C9);
		*b = (v << 2) | (v >> 4);
	}
	else
	{
		inp(0x3C9);
	}
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	#if HAS_PCX
	if (pcxPictureData != NULL)
	{
		if (fixed != NULL)
			*fixed = true;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = pcxPictureWidth;
		if (h != NULL)
			*h = pcxPictureHeight;
		return;
	}
	#endif

	if (bufferedEntry == NULL)
	{
		if (fixed != NULL)
			*fixed = false;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = 0;
		if (h != NULL)
			*h = 0;
	}
	else
	{
		if (fixed != NULL)
			*fixed = (bufferedEntry->flags & DMG_FLAG_FIXED) != 0;
		if (x != NULL)
			*x = bufferedEntry->x;
		if (y != NULL)
			*y = bufferedEntry->y;
		if (w != NULL)
			*w = bufferedEntry->width;
		if (h != NULL)
			*h = bufferedEntry->height;
	}
}

void VID_LoadPicture (uint8_t picno, DDB_ScreenMode mode)
{
	#if DEBUG_DOS_PICTURE_TIMINGS
	uint16_t traceIndex = pictureLoadTraceCount;
	bool traceThisPicture = traceIndex < 96;
	uint32_t tLoadStart = 0;
	if (traceThisPicture)
		VID_GetMilliseconds(&tLoadStart);

	if (traceThisPicture)
		DebugPrintf("DOSVID: LoadPicture(pic=%u mode=%u)\n", (unsigned)picno, (unsigned)mode);
	pictureLoadTraceCount++;
	#endif

	#if HAS_PCX
	FreeBufferedPCXPicture();
	bufferedEntry = NULL;
	pictureData = NULL;
	bufferedPictureIndexed = false;

	if (dmg == NULL)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (!VID_GetExternalPictureFileName(picno, pictureFileName, sizeof(pictureFileName)))
			return;

		if (!LoadNativePCXData(pictureFileName, &pcxPictureData, &pcxPictureSize, &pcxPictureWidth, &pcxPictureHeight, pcxPalette))
			FreeBufferedPCXPicture();
		return;
	}
	#else
	if (dmg == NULL)
		return;
	#endif

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Image)
	{
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
		{
			uint32_t tLoadEnd;
			VID_GetMilliseconds(&tLoadEnd);
			DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=missing\n", (unsigned long)(tLoadEnd - tLoadStart));
		}
		#endif
		return;
	}

	bufferedEntry = entry;
	bufferedEntryIndex = picno;
	const VID_AdapterInfo* info = VID_CommonGetInfo();
	if (info != NULL && info->nativeImageMode == ImageMode_Indexed)
	{
		pictureData = DMG_GetEntryData(dmg, picno, ImageMode_Indexed);
		if (pictureData != NULL)
		{
			bufferedPictureIndexed = true;
			DMG_SetError(DMG_ERROR_NONE);
			#if DEBUG_DOS_PICTURE_TIMINGS
			if (traceThisPicture)
			{
				uint32_t tLoadEnd;
				VID_GetMilliseconds(&tLoadEnd);
				DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=indexed\n", (unsigned long)(tLoadEnd - tLoadStart));
			}
			#endif
			return;
		}

		// Keep compatibility with assets that only decode through native mode.
		pictureData = DMG_GetEntryDataNative(dmg, picno);
		if (pictureData != NULL)
		{
			bufferedPictureIndexed = false;
			DMG_SetError(DMG_ERROR_NONE);
			#if DEBUG_DOS_PICTURE_TIMINGS
			if (traceThisPicture)
			{
				uint32_t tLoadEnd;
				VID_GetMilliseconds(&tLoadEnd);
				DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=native-fallback\n", (unsigned long)(tLoadEnd - tLoadStart));
			}
			#endif
			return;
		}

		bufferedEntry = NULL;
		bufferedPictureIndexed = false;
		#if DEBUG_DOS_PICTURE_TIMINGS
		if (traceThisPicture)
		{
			uint32_t tLoadEnd;
			VID_GetMilliseconds(&tLoadEnd);
			DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=indexed-fail\n", (unsigned long)(tLoadEnd - tLoadStart));
		}
		#endif
		return;
	}

	bufferedPictureIndexed = false;
	pictureData = DMG_GetEntryDataNative(dmg, picno);
	if (pictureData == NULL)
	{
		if (info != NULL && (info->nativeImageMode == ImageMode_CGA || info->nativeImageMode == ImageMode_Planar))
		{
			bufferedEntry = NULL;
			bufferedPictureIndexed = false;
			return;
		}
		pictureData = DMG_GetEntryData(dmg, picno, ImageMode_Indexed);
		if (pictureData != NULL)
		{
			DMG_SetError(DMG_ERROR_NONE);
			bufferedPictureIndexed = true;
			#if DEBUG_DOS_PICTURE_TIMINGS
			if (traceThisPicture)
			{
				uint32_t tLoadEnd;
				VID_GetMilliseconds(&tLoadEnd);
				DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=indexed-fallback\n", (unsigned long)(tLoadEnd - tLoadStart));
			}
			#endif
		}
		else
		{
			bufferedEntry = NULL;
			bufferedPictureIndexed = false;
			#if DEBUG_DOS_PICTURE_TIMINGS
			if (traceThisPicture)
			{
				uint32_t tLoadEnd;
				VID_GetMilliseconds(&tLoadEnd);
				DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=decode-fail\n", (unsigned long)(tLoadEnd - tLoadStart));
			}
			#endif
		}
	}
	#if DEBUG_DOS_PICTURE_TIMINGS
	else if (traceThisPicture)
	{
		uint32_t tLoadEnd;
		VID_GetMilliseconds(&tLoadEnd);
		DebugPrintf("DOSVID: LoadPicture timing total=%lu ms stage=native\n", (unsigned long)(tLoadEnd - tLoadStart));
	}
	#endif
}

void VID_OpenFileDialog (bool existing, char* filename, size_t bufferSize)
{
	// Not supported
}

void VID_PlaySample (uint8_t no, int* duration)
{
	if (dmg == NULL) return;

	DMG_Entry* entry = DMG_GetEntry(dmg, no);
	if (entry == NULL || entry->type != DMGEntry_Audio)
		return;

	// The SB mixer plays unsigned 8-bit mono and resamples to the output
	// rate itself, so we only need the sample folded to 8-bit and capped to
	// the mixer frequency; the shared converter does this in place.
	DMG_AudioTarget sink;
	sink.maxRate = sbMixFrequency;
	sink.bitDepth = 8;
	sink.signedOutput = false;

	uint32_t sampleBytes;
	uint32_t sampleHz;
	audioData = DMG_GetEntryAudioConverted(dmg, no, &sink, &sampleBytes, &sampleHz);
	if (audioData == 0)
		return;

	MIX_PlaySample(audioData, sampleBytes, sampleHz, 256);

	if (duration != NULL)
		*duration = sampleBytes * 1000 / sampleHz;
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int volume)
{
	MIX_PlaySample((uint8_t*)buffer, samples, hz, volume);
}

void VID_StopSampleIfOverlaps(const void* buffer, uint32_t size)
{
	MIX_StopSampleIfOverlaps(buffer, size);
}

void VID_Quit ()
{
	quit = true;
}

void VID_RestoreScreen ()
{
	VID_CommonRestoreScreen();
}

void VID_SaveScreen ()
{
	VID_CommonSaveScreen();
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonScroll(x, y, w, h, lines, paper);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	VID_CommonSetTarget(op, front);
}

void VID_ClearBuffer (bool front)
{
	VID_CommonClearBuffer(front, 0);
}

void VID_SwapScreen ()
{
	VID_CommonSwapBuffers();
}

void VID_MainLoop (DDB_Interpreter* i, void (*callback)(int elapsed))
{
	uint32_t prev;
	uint32_t time;
	uint32_t elapsed = 0;

	interpreter = i;
	VID_GetMilliseconds(&prev);

	while (!quit)
	{
		VID_GetMilliseconds(&time);
		elapsed = time - prev;
		if (elapsed < 16)
			VID_VSync();

		VID_GetMilliseconds(&time);
		elapsed = time - prev;
		prev = time;

		callback(elapsed);
	}

	quit = false;
}

bool VID_Initialize(DDB_Machine machine, DDB_Version version, DDB_ScreenMode mode)
{
	DebugPrintf("DOSVID: VID_Initialize(machine=%u mode=%u)\n", (unsigned)machine, (unsigned)mode);

	if (initialized)
		return false;

	screenMachine = machine;
	screenMode = mode;
	screenVersion = version;

	if (machine != DDB_MACHINE_IBMPC && machine != DDB_MACHINE_ATARIST)
	{
		DebugPrintf("DOSVID: rejecting unsupported machine=%u\n", (unsigned)machine);
		DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
		return false;
	}

	if (mode != ScreenMode_CGA &&
		mode != ScreenMode_EGA &&
		mode != ScreenMode_VGA16 &&
		mode != ScreenMode_VGA &&
		mode != ScreenMode_SHiRes)
	{
		DebugPrintf("DOSVID: rejecting unsupported mode value=%u\n", (unsigned)mode);
		DDB_SetError(DDB_ERROR_VIDEO_MODE_NOT_SUPPORTED);
		return false;
	}

	if (mode == ScreenMode_VGA16 || mode == ScreenMode_VGA || mode == ScreenMode_SHiRes)
	{
		DebugPrintf("DOSVID: VGA BIOS presence check for mode %u\n", (unsigned)mode);
		if (!DOS_HasVGABIOS())
		{
			DebugPrintf("DOSVID: VGA BIOS presence check failed\n");
			DDB_SetError(DDB_ERROR_VIDEO_HARDWARE_NOT_SUPPORTED);
			return false;
		}
	}

	// A sound card failure should never prevent the game from
	// starting: fall back to running without sound instead.
	if (SB_InitializeConfigured())
		if (!SB_Start())
		{
			DebugPrintf("DOSVID: SB_Start failed, continuing without sound\n");
			SB_Exit();
		}

	Timer_Start();

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);
	DebugPrintf("DOSVID: calling SetDOSVideoMode from VID_Init with mode=%u\n", (unsigned)mode);
	if (!SetDOSVideoMode(mode))
	{
		DebugPrintf("DOSVID: SetDOSVideoMode failed, error=%d\n", (int)DDB_GetError());
		return false;
	}
	const VID_AdapterInfo* info = VID_CommonGetInfo();
	screenWidth = info != 0 ? info->width : 320;
	screenHeight = info != 0 ? info->height : 200;
	ApplyIBMPCTextMetrics(mode == ScreenMode_SHiRes);

	// Ensure DAC state is initialized when entering 256-color modes (e.g. VESA SHiRes).
	VID_SetDefaultPalette();
	VID_ActivatePalette();

	initialized = true;
	return true;
}

void VID_SetDisplayPlanesHint(uint8_t planes)
{
	(void)planes;
}

void VID_SetTextInputMode (bool enabled)
{
	// Not supported
}


bool VID_BackupScreen()
{
	return false;
}

bool VID_RestoreBackupScreen()
{
	return false;
}

#endif
