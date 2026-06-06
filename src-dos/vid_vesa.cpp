#ifdef _DOS

#include "vid_vesa.h"
#include "video.h"

#include <ddb_scr.h>

#include <os_lib.h>

#include <i86.h>
#include <dos.h>

#define VESA_WIDTH 640
#define VESA_HEIGHT 400

#define VESA_MODE_ATTR_SUPPORTED 0x0001
#define VESA_MODE_ATTR_GRAPHICS  0x0010
#define VESA_MODE_ATTR_NO_WINDOW 0x0040
#define VESA_MODE_ATTR_LFB       0x0080
#define VESA_WIN_ATTR_READ       0x02
#define VESA_WIN_ATTR_WRITE      0x04
#define VESA_MEMORY_PACKED_PIXEL 0x04
#define VESA_MAX_PAGES 3
#define VESA_BANK_NONE 0xFFFF

#pragma pack(push, 1)
struct VBE_ControllerInfo
{
	char signature[4];
	uint16_t version;
	uint32_t oemStringPtr;
	uint32_t capabilities;
	uint32_t videoModePtr;
	uint16_t totalMemory;
	uint8_t reserved[236];
	uint8_t oemData[256];
};

struct VBE_ModeInfo
{
	uint16_t modeAttributes;
	uint8_t winAAttributes;
	uint8_t winBAttributes;
	uint16_t winGranularity;
	uint16_t winSize;
	uint16_t winASegment;
	uint16_t winBSegment;
	uint32_t winFuncPtr;
	uint16_t bytesPerScanLine;
	uint16_t xResolution;
	uint16_t yResolution;
	uint8_t xCharSize;
	uint8_t yCharSize;
	uint8_t numberOfPlanes;
	uint8_t bitsPerPixel;
	uint8_t numberOfBanks;
	uint8_t memoryModel;
	uint8_t bankSize;
	uint8_t numberOfImagePages;
	uint8_t reserved0;
	uint8_t redMaskSize;
	uint8_t redFieldPosition;
	uint8_t greenMaskSize;
	uint8_t greenFieldPosition;
	uint8_t blueMaskSize;
	uint8_t blueFieldPosition;
	uint8_t rsvdMaskSize;
	uint8_t rsvdFieldPosition;
	uint8_t directColorModeInfo;
	uint32_t physBasePtr;
	uint32_t offScreenMemOffset;
	uint16_t offScreenMemSize;
	uint8_t reserved1[206];
};
#pragma pack(pop)

enum VESA_Backend
{
	VESA_Backend_None,
	VESA_Backend_LFB,
	VESA_Backend_Banked
};

static VESA_Backend vesaBackend = VESA_Backend_None;
static uint16_t vesaMode = 0;
static unsigned vesaPageCount = 0;
static unsigned vesaVisiblePage = 0;
static uint32_t vesaPageSize = 0;
static uint16_t vesaLineSize = VESA_WIDTH;
static uint32_t vesaPageOffset[VESA_MAX_PAGES];
static uint32_t vesaDisplayStartY[VESA_MAX_PAGES];
static bool vesaCanDisplayStart = false;
static bool vesaProbed = false;
static bool vesaAvailable = false;

static uint32_t vesaLFBLinear = 0;
static uint32_t vesaLFBMapSize = 0;

static uint16_t vesaReadWindow = 0;
static uint16_t vesaWriteWindow = 0;
static uint16_t vesaReadWindowSegment = 0xA000;
static uint16_t vesaWriteWindowSegment = 0xA000;
static uint32_t vesaWindowGranularityBytes = 65536UL;
static uint32_t vesaWindowSizeBytes = 65536UL;
static uint8_t vesaWindowGranularityShift = 16;
static uint32_t vesaWindowGranularityMask = 65535UL;
static bool vesaWindowGranularityPower2 = true;
static uint16_t vesaCurrentReadBank = VESA_BANK_NONE;
static uint16_t vesaCurrentWriteBank = VESA_BANK_NONE;

static bool VESA_QueryControllerInfo(VBE_ControllerInfo* info);
static bool VESA_QueryModeInfo(uint16_t mode, VBE_ModeInfo* info);
static bool VESA_ProbeMode();
static bool VESA_HasController();
static bool VESA_SetMode(uint16_t mode);
static bool VESA_SetDisplayStart(unsigned page);
static bool VESA_SetBank(uint16_t window, uint16_t bank);
static bool VESA_MapPhysical(uint32_t physical, uint32_t size, uint32_t* linear);
static void VESA_UnmapPhysical(uint32_t linear);
static void VESA_InitDerivedModeState();

static dos_ptr8 VESA_GetPagePtr(unsigned page);
static void VESA_PresentPage(unsigned page);
static void VESA_PresentRect(unsigned page, int x, int y, int w, int h);
static void VESA_PresentActiveRectIfFront(int x, int y, int w, int h);
static void VESA_CopyPage(unsigned srcPage, unsigned dstPage);
static void VESA_ClearPage(unsigned page, uint8_t color);
static void VESA_SetTarget(SCR_Operation op, bool front);
static void VESA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
static void VESA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper);
static void VESA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
static void VESA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h);
static void VESA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h);

static VID_Adapter vesaAdapter =
{
	{
		VESA_WIDTH,
		VESA_HEIGHT,
		8,
		8,
		8,
		256,
		ImageMode_Indexed,
		1
	},
	{
		VESA_GetPagePtr,
		VESA_PresentPage,
		VESA_CopyPage,
		VESA_ClearPage,
		VESA_SetTarget,
		VESA_Clear,
		VESA_Scroll,
		VESA_DrawTextSpan,
		VESA_BlitNativeImage,
		VESA_BlitIndexedImage
	}
};

const VID_Adapter* VESA_GetAdapter()
{
	return &vesaAdapter;
}

static dos_ptr8 VESA_RealPtr(uint16_t segment, uint32_t off)
{
#if defined(__386__)
	return (dos_ptr8)(((uint32_t)segment << 4) + off);
#else
	return (dos_ptr8)MK_FP(segment, (uint16_t)off);
#endif
}

static dos_ptr8 VESA_LFBPtr(uint32_t off)
{
	return (dos_ptr8)(vesaLFBLinear + off);
}

#if defined(__386__)
#pragma pack(push, 1)
struct VESA_RealModeRegs
{
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t reserved;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint16_t flags;
	uint16_t es;
	uint16_t ds;
	uint16_t fs;
	uint16_t gs;
	uint16_t ip;
	uint16_t cs;
	uint16_t sp;
	uint16_t ss;
};
#pragma pack(pop)

static bool VESA_RealModeInt(uint8_t interruptNo, VESA_RealModeRegs* rmRegs)
{
	if (rmRegs == 0)
		return false;
	union REGS regs;
	struct SREGS sregs;
	MemClear(&regs, sizeof(regs));
	MemClear(&sregs, sizeof(sregs));
	regs.w.ax = 0x0300;
	regs.h.bl = interruptNo;
	regs.h.bh = 0;
	regs.w.cx = 0;
	sregs.es = FP_SEG(rmRegs);
	regs.x.edi = FP_OFF(rmRegs);
	int386x(0x31, &regs, &regs, &sregs);
	return !regs.x.cflag;
}

static bool VESA_AllocDOSInfoBuffer(uint32_t size, void** ptr, uint16_t* selector, uint16_t* realSegment)
{
	if (ptr == 0 || selector == 0 || realSegment == 0)
		return false;

	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.x.eax = 0x0100;
	regs.x.ebx = (size + 15U) >> 4;
	int386(0x31, &regs, &regs);
	if (regs.x.cflag)
		return false;

	*selector = (uint16_t)regs.x.edx;
	*realSegment = (uint16_t)(regs.x.eax & 0xFFFF);
	*ptr = (void*)(((uint32_t)*realSegment) << 4);
	return true;
}

static void VESA_FreeDOSInfoBuffer(uint16_t selector)
{
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.x.eax = 0x0101;
	regs.x.edx = selector;
	int386(0x31, &regs, &regs);
}
#endif

static bool VESA_SetBank(uint16_t window, uint16_t bank)
{
#if defined(__386__)
	VESA_RealModeRegs rmRegs;
	MemClear(&rmRegs, sizeof(rmRegs));
	rmRegs.eax = 0x4F05;
	rmRegs.ebx = window;
	rmRegs.edx = bank;
	if (!VESA_RealModeInt(0x10, &rmRegs))
		return false;
	return (uint16_t)rmRegs.eax == 0x004F;
#else
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.w.ax = 0x4F05;
	regs.w.bx = window;
	regs.w.dx = bank;
	int86(0x10, &regs, &regs);
	return regs.w.ax == 0x004F;
#endif
}

static bool VESA_SelectReadBank(uint16_t bank)
{
	if (bank == vesaCurrentReadBank)
		return true;
	if (!VESA_SetBank(vesaReadWindow, bank))
		return false;
	vesaCurrentReadBank = bank;
	if (vesaReadWindow == vesaWriteWindow)
		vesaCurrentWriteBank = bank;
	return true;
}

static bool VESA_SelectWriteBank(uint16_t bank)
{
	if (bank == vesaCurrentWriteBank)
		return true;
	if (!VESA_SetBank(vesaWriteWindow, bank))
		return false;
	vesaCurrentWriteBank = bank;
	if (vesaReadWindow == vesaWriteWindow)
		vesaCurrentReadBank = bank;
	return true;
}

static uint32_t VESA_PageOffset(unsigned page)
{
	return vesaPageOffset[page];
}

static uint32_t VESA_Offset(unsigned page, int x, int y)
{
	return vesaPageOffset[page] + (uint32_t)y * vesaLineSize + (uint32_t)x;
}

static uint32_t VESA_DebugChecksumBytes(const uint8_t* data, uint32_t size)
{
	uint32_t sum = 0;
	if (data == 0)
		return 0;
	for (uint32_t n = 0; n < size; n++)
		sum = (sum * 33u) ^ data[n];
	return sum;
}

static bool VESA_IsPowerOfTwo(uint32_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static uint8_t VESA_Log2(uint32_t value)
{
	uint8_t shift = 0;
	while (value > 1)
	{
		value >>= 1;
		shift++;
	}
	return shift;
}

static void VESA_InitDerivedModeState()
{
	for (unsigned page = 0; page < VESA_MAX_PAGES; page++)
	{
		vesaPageOffset[page] = (uint32_t)page * vesaPageSize;
		vesaDisplayStartY[page] = vesaLineSize != 0 ? vesaPageOffset[page] / vesaLineSize : 0;
	}

	vesaWindowGranularityPower2 = VESA_IsPowerOfTwo(vesaWindowGranularityBytes);
	if (vesaWindowGranularityPower2)
	{
		vesaWindowGranularityShift = VESA_Log2(vesaWindowGranularityBytes);
		vesaWindowGranularityMask = vesaWindowGranularityBytes - 1;
	}
	else
	{
		vesaWindowGranularityShift = 0;
		vesaWindowGranularityMask = 0;
	}
}

static uint16_t VESA_BankForOffset(uint32_t offset)
{
	if (vesaWindowGranularityPower2)
		return (uint16_t)(offset >> vesaWindowGranularityShift);
	return (uint16_t)(offset / vesaWindowGranularityBytes);
}

static uint32_t VESA_WindowOffsetForBank(uint32_t offset, uint16_t bank)
{
	if (vesaWindowGranularityPower2)
		return offset & vesaWindowGranularityMask;
	return offset - (uint32_t)bank * vesaWindowGranularityBytes;
}

static int VESA_ClipRect(int* x, int* y, int* w, int* h, int* sx, int* sy)
{
	VID_CommonState* state = VID_CommonGetState();
	if (sx != 0)
		*sx = 0;
	if (sy != 0)
		*sy = 0;
	if (*x < state->minx)
	{
		int skip = state->minx - *x;
		if (sx != 0)
			*sx += skip;
		*w -= skip;
		*x = state->minx;
	}
	if (*y < state->miny)
	{
		int skip = state->miny - *y;
		if (sy != 0)
			*sy += skip;
		*h -= skip;
		*y = state->miny;
	}
	if (*x + *w > state->maxx + 1)
		*w = state->maxx - *x + 1;
	if (*y + *h > state->maxy + 1)
		*h = state->maxy - *y + 1;
	return *w > 0 && *h > 0;
}

static void VESA_LFBWriteSpan(uint32_t dst, const uint8_t* src, int32_t count)
{
	memcpy_bytes(VESA_LFBPtr(dst), src, count);
}

static void VESA_LFBReadSpan(uint8_t* dst, uint32_t src, int32_t count)
{
	memcpy_bytes(dst, VESA_LFBPtr(src), count);
}

static void VESA_LFBFillSpan(uint32_t dst, uint8_t value, int32_t count)
{
	memset8(VESA_LFBPtr(dst), value, count);
}

static void VESA_LFBCopySpan(uint32_t dst, uint32_t src, int32_t count)
{
	if (count > 0 && dst != src)
		MemMove(VESA_LFBPtr(dst), VESA_LFBPtr(src), (size_t)count);
}

static void VESA_BankedWriteSpan(uint32_t dst, const uint8_t* src, int32_t count)
{
	while (count > 0)
	{
		uint16_t bank = VESA_BankForOffset(dst);
		uint32_t windowOffset = VESA_WindowOffsetForBank(dst, bank);
		uint32_t chunk = vesaWindowSizeBytes - windowOffset;
		if (chunk > (uint32_t)count)
			chunk = (uint32_t)count;
		if (!VESA_SelectWriteBank(bank))
			return;
		memcpy_bytes(VESA_RealPtr(vesaWriteWindowSegment, windowOffset), src, (int32_t)chunk);
		dst += chunk;
		src += chunk;
		count -= (int32_t)chunk;
	}
}

static void VESA_BankedReadSpan(uint8_t* dst, uint32_t src, int32_t count)
{
	while (count > 0)
	{
		uint16_t bank = VESA_BankForOffset(src);
		uint32_t windowOffset = VESA_WindowOffsetForBank(src, bank);
		uint32_t chunk = vesaWindowSizeBytes - windowOffset;
		if (chunk > (uint32_t)count)
			chunk = (uint32_t)count;
		if (!VESA_SelectReadBank(bank))
			return;
		memcpy_bytes(dst, VESA_RealPtr(vesaReadWindowSegment, windowOffset), (int32_t)chunk);
		src += chunk;
		dst += chunk;
		count -= (int32_t)chunk;
	}
}

static void VESA_BankedFillSpan(uint32_t dst, uint8_t value, int32_t count)
{
	while (count > 0)
	{
		uint16_t bank = VESA_BankForOffset(dst);
		uint32_t windowOffset = VESA_WindowOffsetForBank(dst, bank);
		uint32_t chunk = vesaWindowSizeBytes - windowOffset;
		if (chunk > (uint32_t)count)
			chunk = (uint32_t)count;
		if (!VESA_SelectWriteBank(bank))
			return;
		memset8(VESA_RealPtr(vesaWriteWindowSegment, windowOffset), value, (int32_t)chunk);
		dst += chunk;
		count -= (int32_t)chunk;
	}
}

static int32_t VESA_BankedCopyChunk(uint32_t dst, uint32_t src, int32_t count, int32_t maxChunk)
{
	uint16_t dstBank = VESA_BankForOffset(dst);
	uint16_t srcBank = VESA_BankForOffset(src);
	uint32_t dstWindowOffset = VESA_WindowOffsetForBank(dst, dstBank);
	uint32_t srcWindowOffset = VESA_WindowOffsetForBank(src, srcBank);
	uint32_t dstChunk = vesaWindowSizeBytes - dstWindowOffset;
	uint32_t srcChunk = vesaWindowSizeBytes - srcWindowOffset;
	uint32_t chunk = dstChunk < srcChunk ? dstChunk : srcChunk;
	if (chunk > (uint32_t)count)
		chunk = (uint32_t)count;
	if (chunk > (uint32_t)maxChunk)
		chunk = (uint32_t)maxChunk;
	return (int32_t)chunk;
}

static void VESA_BankedCopySpan(uint32_t dst, uint32_t src, int32_t count)
{
	if (count <= 0 || dst == src)
		return;

	uint8_t temp[2048];
	if (dst < src && vesaReadWindow != vesaWriteWindow)
	{
		while (count > 0)
		{
			int32_t chunk = VESA_BankedCopyChunk(dst, src, count, count);
			uint16_t dstBank = VESA_BankForOffset(dst);
			uint16_t srcBank = VESA_BankForOffset(src);
			uint32_t dstWindowOffset = VESA_WindowOffsetForBank(dst, dstBank);
			uint32_t srcWindowOffset = VESA_WindowOffsetForBank(src, srcBank);
			if (!VESA_SelectReadBank(srcBank) || !VESA_SelectWriteBank(dstBank))
				return;
			memcpy_bytes(VESA_RealPtr(vesaWriteWindowSegment, dstWindowOffset),
				VESA_RealPtr(vesaReadWindowSegment, srcWindowOffset), chunk);
			dst += chunk;
			src += chunk;
			count -= chunk;
		}
	}
	else if (dst < src)
	{
		while (count > 0)
		{
			int32_t chunk = VESA_BankedCopyChunk(dst, src, count, (int32_t)sizeof(temp));
			VESA_BankedReadSpan(temp, src, chunk);
			VESA_BankedWriteSpan(dst, temp, chunk);
			dst += chunk;
			src += chunk;
			count -= chunk;
		}
	}
	else
	{
		while (count > 0)
		{
			int32_t chunk = count > (int32_t)sizeof(temp) ? (int32_t)sizeof(temp) : count;
			uint32_t offset = (uint32_t)(count - chunk);
			VESA_BankedReadSpan(temp, src + offset, chunk);
			VESA_BankedWriteSpan(dst + offset, temp, chunk);
			count -= chunk;
		}
	}
}

static void VESA_WriteSpan(uint32_t dst, const uint8_t* src, int32_t count)
{
	if (count <= 0)
		return;
	if (vesaBackend == VESA_Backend_LFB)
		VESA_LFBWriteSpan(dst, src, count);
	else
		VESA_BankedWriteSpan(dst, src, count);
}

static void VESA_ReadSpan(uint8_t* dst, uint32_t src, int32_t count)
{
	if (count <= 0)
		return;
	if (vesaBackend == VESA_Backend_LFB)
		VESA_LFBReadSpan(dst, src, count);
	else
		VESA_BankedReadSpan(dst, src, count);
}

static void VESA_FillSpan(uint32_t dst, uint8_t value, int32_t count)
{
	if (count <= 0)
		return;
	if (vesaBackend == VESA_Backend_LFB)
		VESA_LFBFillSpan(dst, value, count);
	else
		VESA_BankedFillSpan(dst, value, count);
}

static void VESA_CopySpan(uint32_t dst, uint32_t src, int32_t count)
{
	if (count <= 0 || dst == src)
		return;
	if (vesaBackend == VESA_Backend_LFB)
		VESA_LFBCopySpan(dst, src, count);
	else
		VESA_BankedCopySpan(dst, src, count);
}

static bool VESA_HasController()
{
	VBE_ControllerInfo controller;
	MemClear(&controller, sizeof(controller));
	DebugPrintf("VESA: checking controller availability\n");
	if (!VESA_QueryControllerInfo(&controller))
	{
		DebugPrintf("VESA: controller not available\n");
		return false;
	}
	DebugPrintf("VESA: controller detected, VBE version 0x%04X\n", (unsigned)controller.version);
	return true;
}

static bool VESA_QueryControllerInfo(VBE_ControllerInfo* info)
{
	if (info == 0)
		return false;
	DebugPrintf("VESA: INT10h AX=4F00 query\n");

	VBE_ControllerInfo* biosInfo = info;
	uint16_t biosSelector = 0;
	uint16_t biosSegment = 0;
#if defined(__386__)
	if (!VESA_AllocDOSInfoBuffer(sizeof(VBE_ControllerInfo), (void**)&biosInfo, &biosSelector, &biosSegment))
	{
		DebugPrintf("VESA: failed to allocate DOS info buffer for 4F00\n");
		return false;
	}
	MemClear(biosInfo, sizeof(VBE_ControllerInfo));
#endif

	union REGS regs;
	struct SREGS sregs;
	MemClear(&regs, sizeof(regs));
	MemClear(&sregs, sizeof(sregs));
	biosInfo->signature[0] = 'V';
	biosInfo->signature[1] = 'B';
	biosInfo->signature[2] = 'E';
	biosInfo->signature[3] = '2';

	regs.w.ax = 0x4F00;
#if defined(__386__)
	VESA_RealModeRegs rmRegs;
	MemClear(&rmRegs, sizeof(rmRegs));
	rmRegs.eax = 0x4F00;
	rmRegs.es = biosSegment;
	rmRegs.edi = 0;
	VESA_RealModeInt(0x10, &rmRegs);
	regs.w.ax = (uint16_t)rmRegs.eax;
	MemCopy(info, biosInfo, sizeof(VBE_ControllerInfo));
	VESA_FreeDOSInfoBuffer(biosSelector);
#else
	sregs.es = FP_SEG(info);
	regs.w.di = FP_OFF(info);
	int86x(0x10, &regs, &regs, &sregs);
#endif
	DebugPrintf("VESA: 4F00 returned AX=0x%04X signature=%c%c%c%c version=0x%04X memory=%u*64KB modes=%04X:%04X\n",
		(unsigned)regs.w.ax,
		info->signature[0], info->signature[1], info->signature[2], info->signature[3],
		(unsigned)info->version,
		(unsigned)info->totalMemory,
		(unsigned)(info->videoModePtr >> 16),
		(unsigned)(info->videoModePtr & 0xFFFF));
	if (regs.w.ax != 0x004F)
		return false;
	return (info->signature[0] == 'V' && info->signature[1] == 'E' && info->signature[2] == 'S' && info->signature[3] == 'A') ||
		(info->signature[0] == 'V' && info->signature[1] == 'B' && info->signature[2] == 'E' && info->signature[3] == '2');
}

static bool VESA_QueryModeInfo(uint16_t mode, VBE_ModeInfo* info)
{
	if (info == 0)
		return false;

	VBE_ModeInfo* biosInfo = info;
	uint16_t biosSelector = 0;
	uint16_t biosSegment = 0;
#if defined(__386__)
	if (!VESA_AllocDOSInfoBuffer(sizeof(VBE_ModeInfo), (void**)&biosInfo, &biosSelector, &biosSegment))
	{
		DebugPrintf("VESA: failed to allocate DOS info buffer for mode 0x%03X\n", (unsigned)mode);
		return false;
	}
	MemClear(biosInfo, sizeof(VBE_ModeInfo));
#endif

	union REGS regs;
	struct SREGS sregs;
	MemClear(&regs, sizeof(regs));
	MemClear(&sregs, sizeof(sregs));

	regs.w.ax = 0x4F01;
	regs.w.cx = mode;
#if defined(__386__)
	VESA_RealModeRegs rmRegs;
	MemClear(&rmRegs, sizeof(rmRegs));
	rmRegs.eax = 0x4F01;
	rmRegs.ecx = mode;
	rmRegs.es = biosSegment;
	rmRegs.edi = 0;
	VESA_RealModeInt(0x10, &rmRegs);
	regs.w.ax = (uint16_t)rmRegs.eax;
	if (regs.w.ax == 0x004F)
		MemCopy(info, biosInfo, sizeof(VBE_ModeInfo));
	VESA_FreeDOSInfoBuffer(biosSelector);
#else
	sregs.es = FP_SEG(info);
	regs.w.di = FP_OFF(info);
	int86x(0x10, &regs, &regs, &sregs);
#endif
	if (regs.w.ax != 0x004F)
		DebugPrintf("VESA: mode 0x%03X query failed, AX=0x%04X\n", (unsigned)mode, (unsigned)regs.w.ax);
	return regs.w.ax == 0x004F;
}

static bool VESA_ModeMatches(const VBE_ModeInfo* info)
{
	if ((info->modeAttributes & (VESA_MODE_ATTR_SUPPORTED | VESA_MODE_ATTR_GRAPHICS)) !=
		(VESA_MODE_ATTR_SUPPORTED | VESA_MODE_ATTR_GRAPHICS))
		return false;
	if (info->xResolution != VESA_WIDTH || info->yResolution != VESA_HEIGHT)
		return false;
	if (info->bitsPerPixel != 8 || info->numberOfPlanes != 1 || info->memoryModel != VESA_MEMORY_PACKED_PIXEL)
		return false;
	if (info->bytesPerScanLine < VESA_WIDTH)
		return false;
	return true;
}

static unsigned VESA_GetUsablePageCount(const VBE_ControllerInfo* controller, const VBE_ModeInfo* info, uint32_t pageSize)
{
	uint32_t memoryBytes = (uint32_t)controller->totalMemory * 65536UL;
	unsigned memoryPages = pageSize != 0 ? (unsigned)(memoryBytes / pageSize) : 0;
	unsigned modePages = (unsigned)info->numberOfImagePages + 1U;
	unsigned pages = memoryPages < modePages ? memoryPages : modePages;
	if (pages == 0)
		pages = 1;
	if (pages > VESA_MAX_PAGES)
		pages = VESA_MAX_PAGES;
	return pages;
}

static bool VESA_TrySelectLFB(uint16_t mode, const VBE_ControllerInfo* controller, const VBE_ModeInfo* info)
{
#if !defined(__386__)
	(void)mode;
	(void)controller;
	(void)info;
	return false;
#else
	if ((info->modeAttributes & VESA_MODE_ATTR_LFB) == 0 || info->physBasePtr == 0)
		return false;

	uint32_t pageSize = (uint32_t)info->bytesPerScanLine * VESA_HEIGHT;
	unsigned pages = VESA_GetUsablePageCount(controller, info, pageSize);
	uint32_t mapSize = pageSize * pages;
	uint32_t linear = 0;
	if (!VESA_MapPhysical(info->physBasePtr, mapSize, &linear))
	{
		DebugPrintf("VESA: LFB map failed for mode 0x%03X phys=0x%08lX size=%lu\n",
			(unsigned)mode, (unsigned long)info->physBasePtr, (unsigned long)mapSize);
		return false;
	}

	if (vesaLFBLinear != 0)
		VESA_UnmapPhysical(vesaLFBLinear);

	vesaBackend = VESA_Backend_LFB;
	vesaMode = mode;
	vesaLineSize = info->bytesPerScanLine;
	vesaPageSize = pageSize;
	vesaPageCount = pages;
	vesaLFBLinear = linear;
	vesaLFBMapSize = mapSize;
	vesaAvailable = true;
	VESA_InitDerivedModeState();
	DebugPrintf("VESA: selected LFB mode 0x%03X pitch=%u pages=%u phys=0x%08lX linear=0x%08lX\n",
		(unsigned)mode, (unsigned)vesaLineSize, vesaPageCount,
		(unsigned long)info->physBasePtr, (unsigned long)vesaLFBLinear);
	return true;
#endif
}

static bool VESA_TrySelectBanked(uint16_t mode, const VBE_ControllerInfo* controller, const VBE_ModeInfo* info)
{
	if ((info->modeAttributes & VESA_MODE_ATTR_NO_WINDOW) != 0)
		return false;
	if (info->winGranularity == 0 || info->winSize == 0)
		return false;

	uint16_t writeWindow = VESA_BANK_NONE;
	uint16_t readWindow = VESA_BANK_NONE;
	uint16_t writeSegment = 0;
	uint16_t readSegment = 0;

	if ((info->winAAttributes & VESA_WIN_ATTR_WRITE) != 0 && info->winASegment != 0)
	{
		writeWindow = 0;
		writeSegment = info->winASegment;
	}
	else if ((info->winBAttributes & VESA_WIN_ATTR_WRITE) != 0 && info->winBSegment != 0)
	{
		writeWindow = 1;
		writeSegment = info->winBSegment;
	}

	if ((info->winAAttributes & VESA_WIN_ATTR_READ) != 0 && info->winASegment != 0)
	{
		readWindow = 0;
		readSegment = info->winASegment;
	}
	else if ((info->winBAttributes & VESA_WIN_ATTR_READ) != 0 && info->winBSegment != 0)
	{
		readWindow = 1;
		readSegment = info->winBSegment;
	}

	if (writeWindow == VESA_BANK_NONE || readWindow == VESA_BANK_NONE)
		return false;

	uint32_t pageSize = (uint32_t)info->bytesPerScanLine * VESA_HEIGHT;
	vesaBackend = VESA_Backend_Banked;
	vesaMode = mode;
	vesaLineSize = info->bytesPerScanLine;
	vesaPageSize = pageSize;
	vesaPageCount = VESA_GetUsablePageCount(controller, info, pageSize);
	vesaReadWindow = readWindow;
	vesaWriteWindow = writeWindow;
	vesaReadWindowSegment = readSegment;
	vesaWriteWindowSegment = writeSegment;
	vesaWindowGranularityBytes = (uint32_t)info->winGranularity * 1024UL;
	vesaWindowSizeBytes = (uint32_t)info->winSize * 1024UL;
	vesaCurrentReadBank = VESA_BANK_NONE;
	vesaCurrentWriteBank = VESA_BANK_NONE;
	vesaAvailable = true;
	VESA_InitDerivedModeState();
	DebugPrintf("VESA: selected banked mode 0x%03X pitch=%u pages=%u readWin=%u writeWin=%u gran=%lu size=%lu\n",
		(unsigned)mode, (unsigned)vesaLineSize, vesaPageCount,
		(unsigned)vesaReadWindow, (unsigned)vesaWriteWindow,
		(unsigned long)vesaWindowGranularityBytes, (unsigned long)vesaWindowSizeBytes);
	return true;
}

static void VESA_LogMode(uint16_t mode, const VBE_ModeInfo* info)
{
	DebugPrintf(
		"VESA: mode 0x%03X attr=0x%04X %ux%u bpp=%u planes=%u model=%u pitch=%u pages=%u winA attr=0x%02X seg=0x%04X winB attr=0x%02X seg=0x%04X gran=%uKB win=%uKB phys=0x%08lX\n",
		(unsigned)mode,
		(unsigned)info->modeAttributes,
		(unsigned)info->xResolution,
		(unsigned)info->yResolution,
		(unsigned)info->bitsPerPixel,
		(unsigned)info->numberOfPlanes,
		(unsigned)info->memoryModel,
		(unsigned)info->bytesPerScanLine,
		(unsigned)info->numberOfImagePages + 1U,
		(unsigned)info->winAAttributes,
		(unsigned)info->winASegment,
		(unsigned)info->winBAttributes,
		(unsigned)info->winBSegment,
		(unsigned)info->winGranularity,
		(unsigned)info->winSize,
		(unsigned long)info->physBasePtr);
}

static bool VESA_TryCandidate(uint16_t mode, const VBE_ControllerInfo* controller, uint16_t* bankedMode, VBE_ModeInfo* bankedInfo)
{
	VBE_ModeInfo info;
	MemClear(&info, sizeof(info));
	if (!VESA_QueryModeInfo(mode, &info))
		return false;
	if (!VESA_ModeMatches(&info))
		return false;

	VESA_LogMode(mode, &info);
	if (VESA_TrySelectLFB(mode, controller, &info))
		return true;

	if (*bankedMode == 0 && VESA_TrySelectBanked(mode, controller, &info))
	{
		*bankedMode = mode;
		MemCopy(bankedInfo, &info, sizeof(info));
		vesaBackend = VESA_Backend_None;
		vesaAvailable = false;
	}
	return false;
}

static bool VESA_ProbeMode()
{
	if (vesaProbed)
		return vesaAvailable;
	vesaProbed = true;
	vesaAvailable = false;
	vesaBackend = VESA_Backend_None;
	DebugPrintf("VESA: probing for %ux%u 8-bpp mode\n", (unsigned)VESA_WIDTH, (unsigned)VESA_HEIGHT);

	VBE_ControllerInfo controller;
	MemClear(&controller, sizeof(controller));
	if (!VESA_QueryControllerInfo(&controller))
	{
		DebugPrintf("VESA: probe aborted, controller query failed\n");
		return false;
	}

	uint16_t bankedMode = 0;
	VBE_ModeInfo bankedInfo;
	MemClear(&bankedInfo, sizeof(bankedInfo));

	uint16_t modeCount = 0;
#if !defined(__386__)
	if (controller.videoModePtr != 0)
	{
		uint16_t modeOff = (uint16_t)(controller.videoModePtr & 0xFFFF);
		uint16_t modeSeg = (uint16_t)(controller.videoModePtr >> 16);
		dos_ptr8 modeBytes = VESA_RealPtr(modeSeg, modeOff);
		for (modeCount = 0; modeCount < 512; modeCount++)
		{
			uint16_t candidate = ((uint16_t*)modeBytes)[modeCount];
			if (candidate == 0xFFFF)
				break;
			if (candidate < 0x100)
				continue;
			if (VESA_TryCandidate(candidate, &controller, &bankedMode, &bankedInfo))
				return true;
		}
		DebugPrintf("VESA: scanned %u modes from controller list\n", (unsigned)modeCount);
	}
#else
	DebugPrintf("VESA: skipping protected-mode mode-list dereference; using bounded 4F01 scan\n");
#endif

	if (bankedMode == 0)
	{
		DebugPrintf("VESA: no usable listed mode yet, scanning 0x100-0x1FF as validated fallback\n");
		for (uint16_t candidate = 0x100; candidate <= 0x1FF; candidate++)
			if (VESA_TryCandidate(candidate, &controller, &bankedMode, &bankedInfo))
				return true;
	}

	if (bankedMode != 0)
		return VESA_TrySelectBanked(bankedMode, &controller, &bankedInfo);

	DebugPrintf("VESA: no compatible 640x400x256 mode found\n");
	return false;
}

static bool VESA_SetMode(uint16_t mode)
{
#if defined(__386__)
	VESA_RealModeRegs rmRegs;
	MemClear(&rmRegs, sizeof(rmRegs));
	rmRegs.eax = 0x4F02;
	rmRegs.ebx = mode;
	if (!VESA_RealModeInt(0x10, &rmRegs))
		return false;
	return (uint16_t)rmRegs.eax == 0x004F;
#else
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.w.ax = 0x4F02;
	regs.w.bx = mode;
	int86(0x10, &regs, &regs);
	return regs.w.ax == 0x004F;
#endif
}

static bool VESA_SetDisplayStart(unsigned page)
{
	if (page >= vesaPageCount)
		return false;
	uint32_t startY = vesaDisplayStartY[page];
#if defined(__386__)
	VESA_RealModeRegs rmRegs;
	MemClear(&rmRegs, sizeof(rmRegs));
	rmRegs.eax = 0x4F07;
	rmRegs.ebx = 0x0080;
	rmRegs.ecx = 0;
	rmRegs.edx = startY;
	if (!VESA_RealModeInt(0x10, &rmRegs) || (uint16_t)rmRegs.eax != 0x004F)
	{
		MemClear(&rmRegs, sizeof(rmRegs));
		rmRegs.eax = 0x4F07;
		rmRegs.ebx = 0x0000;
		rmRegs.ecx = 0;
		rmRegs.edx = startY;
		if (!VESA_RealModeInt(0x10, &rmRegs))
			return false;
	}
	return (uint16_t)rmRegs.eax == 0x004F;
#else
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.w.ax = 0x4F07;
	regs.w.bx = 0x0080;
	regs.w.cx = 0;
	regs.w.dx = (uint16_t)startY;
	int86(0x10, &regs, &regs);
	if (regs.w.ax != 0x004F)
	{
		MemClear(&regs, sizeof(regs));
		regs.w.ax = 0x4F07;
		regs.w.bx = 0x0000;
		regs.w.cx = 0;
		regs.w.dx = (uint16_t)startY;
		int86(0x10, &regs, &regs);
	}
	return regs.w.ax == 0x004F;
#endif
}

static bool VESA_MapPhysical(uint32_t physical, uint32_t size, uint32_t* linear)
{
#if !defined(__386__)
	(void)physical;
	(void)size;
	(void)linear;
	return false;
#else
	if (linear == 0 || size == 0)
		return false;
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.w.ax = 0x0800;
	regs.w.bx = (uint16_t)(physical >> 16);
	regs.w.cx = (uint16_t)physical;
	regs.w.si = (uint16_t)(size >> 16);
	regs.w.di = (uint16_t)size;
	int386(0x31, &regs, &regs);
	if (regs.x.cflag)
		return false;
	*linear = ((uint32_t)regs.w.bx << 16) | regs.w.cx;
	return *linear != 0;
#endif
}

static void VESA_UnmapPhysical(uint32_t linear)
{
#if defined(__386__)
	if (linear == 0)
		return;
	union REGS regs;
	MemClear(&regs, sizeof(regs));
	regs.w.ax = 0x0801;
	regs.w.bx = (uint16_t)(linear >> 16);
	regs.w.cx = (uint16_t)linear;
	int386(0x31, &regs, &regs);
#else
	(void)linear;
#endif
}

bool VESA_IsAvailable()
{
#if !defined(__386__)
	return false;
#else
	return VESA_HasController();
#endif
}

bool VESA_SetVideoMode()
{
#if !defined(__386__)
	return false;
#else
	DebugPrintf("VESA: SetVideoMode entry\n");
	if (!VESA_ProbeMode())
		return false;

	uint16_t mode = vesaMode;
	if (vesaBackend == VESA_Backend_LFB)
		mode |= 0x4000;
	if (!VESA_SetMode(mode))
		return false;

	vesaVisiblePage = 0;
	vesaCanDisplayStart = false;
	vesaCurrentReadBank = VESA_BANK_NONE;
	vesaCurrentWriteBank = VESA_BANK_NONE;
	VID_CommonInit(&vesaAdapter, vesaPageSize, vesaLineSize, vesaPageCount,
		vesaPageCount > 2 ? 2 : VID_INVALID_PAGE);
	VESA_ClearPage(0, 0);
	if (vesaPageCount > 1)
		VESA_ClearPage(1, 0);
	if (vesaPageCount > 2)
		VESA_ClearPage(2, 0);
	if (vesaPageCount > 1)
	{
		vesaCanDisplayStart = VESA_SetDisplayStart(1);
		VESA_SetDisplayStart(0);
	}
	else
		vesaCanDisplayStart = VESA_SetDisplayStart(0);
	vesaVisiblePage = 0;
	DebugPrintf("VESA: video mode activated backend=%u pageSize=%lu pitch=%u pages=%u displayStart=%u\n",
		(unsigned)vesaBackend, (unsigned long)vesaPageSize, (unsigned)vesaLineSize, vesaPageCount,
		(unsigned)vesaCanDisplayStart);
	return true;
#endif
}

static dos_ptr8 VESA_GetPagePtr(unsigned page)
{
	if (page >= vesaPageCount)
		return 0;
	if (vesaBackend == VESA_Backend_LFB && vesaLFBLinear != 0)
		return VESA_LFBPtr(VESA_PageOffset(page));
	if (vesaBackend == VESA_Backend_Banked)
		return VESA_RealPtr(vesaWriteWindowSegment, 0);
	return 0;
}

static void VESA_PresentPage(unsigned page)
{
	if (page >= vesaPageCount)
		return;
	if (vesaCanDisplayStart)
	{
		if (VESA_SetDisplayStart(page))
			vesaVisiblePage = page;
		return;
	}
	if (page == 0 || page == vesaVisiblePage)
		return;
	VESA_CopySpan(VESA_PageOffset(0), VESA_PageOffset(page), (int32_t)vesaPageSize);
	vesaVisiblePage = 0;
}

static void VESA_PresentRect(unsigned page, int x, int y, int w, int h)
{
	if (page >= vesaPageCount || page == 0)
		return;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > VESA_WIDTH)
		w = VESA_WIDTH - x;
	if (y + h > VESA_HEIGHT)
		h = VESA_HEIGHT - y;
	if (w <= 0 || h <= 0)
		return;

	uint32_t dst = VESA_Offset(0, x, y);
	uint32_t src = VESA_Offset(page, x, y);
	for (int row = 0; row < h; row++)
	{
		VESA_CopySpan(dst, src, w);
		dst += vesaLineSize;
		src += vesaLineSize;
	}
}

static void VESA_PresentActiveRectIfFront(int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	if (!vesaCanDisplayStart && state->activePage == state->frontPage && state->frontPage != vesaVisiblePage)
		VESA_PresentRect(state->frontPage, x, y, w, h);
}

static void VESA_CopyPage(unsigned srcPage, unsigned dstPage)
{
	if (srcPage >= vesaPageCount || dstPage >= vesaPageCount || srcPage == dstPage)
		return;
	VESA_CopySpan(VESA_PageOffset(dstPage), VESA_PageOffset(srcPage), (int32_t)vesaPageSize);
	VID_CommonState* state = VID_CommonGetState();
	if (dstPage == state->frontPage && state->frontPage != vesaVisiblePage)
		VESA_PresentPage(state->frontPage);
}

static void VESA_ClearPage(unsigned page, uint8_t color)
{
	if (page >= vesaPageCount)
		return;
	uint32_t dst = VESA_PageOffset(page);
	for (int row = 0; row < VESA_HEIGHT; row++)
	{
		VESA_FillSpan(dst, color, VESA_WIDTH);
		dst += vesaLineSize;
	}
	VID_CommonState* state = VID_CommonGetState();
	if (page == state->frontPage && state->frontPage != vesaVisiblePage)
		VESA_PresentPage(state->frontPage);
}

static void VESA_SetTarget(SCR_Operation op, bool front)
{
	(void)op;
	VID_CommonSetActiveBuffer(front);
}

static void VESA_Clear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	(void)mode;
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= vesaPageCount)
		return;
	if (!VESA_ClipRect(&x, &y, &w, &h, 0, 0))
		return;
	uint32_t dst = VESA_Offset(page, x, y);
	for (int row = 0; row < h; row++)
	{
		VESA_FillSpan(dst, color, w);
		dst += vesaLineSize;
	}
	VESA_PresentActiveRectIfFront(x, y, w, h);
}

static void VESA_DrawCharacter(int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= vesaPageCount)
		return;

	int width = charWidth[ch];
	if (width <= 0)
		return;
	bool doubleText = lineHeight >= 16;
	int drawHeight = doubleText ? 16 : 8;
	int sourceWidth = doubleText ? ((width + 1) >> 1) : width;
	if (sourceWidth <= 0)
		return;

	int x1 = x;
	int x2 = x + width;
	if (x1 < state->minx) x1 = state->minx;
	if (x2 > state->maxx + 1) x2 = state->maxx + 1;
	if (x2 <= x1)
		return;

	const uint8_t* data = charset + 8 * ch;
	bool transparent = paper == 255;
	uint8_t rowBuffer[64];

	for (int cy = 0; cy < drawHeight; cy++)
	{
		int py = y + cy;
		if (py < state->miny || py > state->maxy)
			continue;

		int len = x2 - x1;
		if (len > (int)sizeof(rowBuffer))
			return;
		uint32_t dst = VESA_Offset(page, x1, py);
		if (transparent)
			VESA_ReadSpan(rowBuffer, dst, len);
		else
			MemSet(rowBuffer, paper, len);

		uint8_t bits = data[doubleText ? (cy >> 1) : cy];
		for (int px = x1; px < x2; px++)
		{
			int sx = doubleText ? ((px - x) >> 1) : (px - x);
			if (sx >= 0 && sx < sourceWidth && (bits & (0x80 >> sx)) != 0)
				rowBuffer[px - x1] = ink;
		}
		VESA_WriteSpan(dst, rowBuffer, len);
	}
}

static void VESA_WriteTransparentTextRun(unsigned page, uint32_t rowBase, int start, int end, uint8_t ink)
{
	if (end > start)
		VESA_FillSpan(rowBase + (uint32_t)start, ink, end - start);
	(void)page;
}

static void VESA_DrawTextSpanFast(unsigned page, int x, int y, const uint8_t* text, uint16_t length, int spanWidth, uint8_t ink, uint8_t paper)
{
	bool doubleText = lineHeight >= 16;
	int drawHeight = doubleText ? 16 : 8;
	static uint8_t rowBuffer[VESA_WIDTH];
	uint32_t rowBase = VESA_Offset(page, x, y);

	for (int cy = 0; cy < drawHeight; cy++)
	{
		if (paper != 255)
			MemSet(rowBuffer, paper, spanWidth);

		int px = 0;
		int runStart = -1;
		int runEnd = 0;
		for (uint16_t n = 0; n < length; n++)
		{
			uint8_t ch = text[n];
			int width = charWidth[ch];
			int sourceWidth = doubleText ? ((width + 1) >> 1) : width;
			uint8_t bits = charset[(ch << 3) + (doubleText ? (cy >> 1) : cy)];

			if (paper != 255)
			{
				for (int sx = 0; sx < sourceWidth; sx++)
				{
					if ((bits & (0x80 >> sx)) != 0)
					{
						if (doubleText)
						{
							int dx = px + (sx << 1);
							if (dx < spanWidth)
								rowBuffer[dx] = ink;
							if (dx + 1 < spanWidth)
								rowBuffer[dx + 1] = ink;
						}
						else
							rowBuffer[px + sx] = ink;
					}
				}
			}
			else
			{
				for (int sx = 0; sx < sourceWidth; sx++)
				{
					bool set = (bits & (0x80 >> sx)) != 0;
					int dx = px + (doubleText ? (sx << 1) : sx);
					int pixels = doubleText ? 2 : 1;
					if (set)
					{
						if (runStart < 0)
							runStart = dx;
						runEnd = dx + pixels;
						if (runEnd > spanWidth)
							runEnd = spanWidth;
					}
					else if (runStart >= 0)
					{
						VESA_WriteTransparentTextRun(page, rowBase, runStart, runEnd, ink);
						runStart = -1;
					}
				}
				if (runStart >= 0 && px + width > runEnd)
				{
					VESA_WriteTransparentTextRun(page, rowBase, runStart, runEnd, ink);
					runStart = -1;
				}
			}
			px += width;
		}

		if (paper != 255)
			VESA_WriteSpan(rowBase, rowBuffer, spanWidth);
		else if (runStart >= 0)
			VESA_WriteTransparentTextRun(page, rowBase, runStart, runEnd, ink);

		rowBase += vesaLineSize;
	}
}

static void VESA_DrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= vesaPageCount)
		return;

	int startX = x;
	bool doubleText = lineHeight >= 16;
	int drawHeight = doubleText ? 16 : 8;
	int spanWidth = 0;
	bool fast = x >= state->minx && y >= state->miny && y + drawHeight - 1 <= state->maxy;
	for (uint16_t n = 0; n < length; n++)
	{
		int width = charWidth[text[n]];
		int sourceWidth = doubleText ? ((width + 1) >> 1) : width;
		if (width <= 0 || sourceWidth <= 0 || sourceWidth > 8)
			fast = false;
		if (x + spanWidth + width > state->maxx + 1)
			fast = false;
		spanWidth += width;
	}

	if (fast && spanWidth > 0 && spanWidth <= VESA_WIDTH)
	{
		VESA_DrawTextSpanFast(page, x, y, text, length, spanWidth, ink, paper);
		x += spanWidth;
	}
	else
	{
		for (uint16_t n = 0; n < length && x < VESA_WIDTH; n++)
		{
			uint8_t ch = text[n];
			VESA_DrawCharacter(x, y, ch, ink, paper);
			x += charWidth[ch];
		}
	}
	VESA_PresentActiveRectIfFront(startX, y, x - startX, lineHeight);
}

static void VESA_BlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h)
{
	static uint16_t debugBlitCount = 0;
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || page >= vesaPageCount)
		return;

	int sx = 0;
	int sy = 0;
	if (!VESA_ClipRect(&x, &y, &w, &h, &sx, &sy))
		return;

	uint32_t dst = VESA_Offset(page, x, y);
	const uint8_t* src = pixels + (uint32_t)sy * srcW + sx;
	const uint8_t* srcStart = src;
	uint32_t dstStart = dst;
	uint32_t srcChecksum = 0;
	uint32_t vramChecksum = 0;
	bool debugThisBlit = debugBlitCount < 16;
	for (int row = 0; row < h; row++)
	{
		if (debugThisBlit)
			srcChecksum ^= VESA_DebugChecksumBytes(src, (uint32_t)w) + (uint32_t)row * 0x9E3779B9UL;
		VESA_WriteSpan(dst, src, w);
		dst += vesaLineSize;
		src += srcW;
	}
	if (debugThisBlit)
	{
		uint8_t temp[640];
		uint32_t readOffset = dstStart;
		for (int row = 0; row < h; row++)
		{
			VESA_ReadSpan(temp, readOffset, w);
			vramChecksum ^= VESA_DebugChecksumBytes(temp, (uint32_t)w) + (uint32_t)row * 0x9E3779B9UL;
			readOffset += vesaLineSize;
		}
		DebugPrintf("VESA: BlitIndexed page=%u dst=%lu src=%p srcW=%d x=%d y=%d w=%d h=%d srcSum=0x%08lX vramSum=0x%08lX first=%02X %02X %02X %02X\n",
			page,
			(unsigned long)dstStart,
			srcStart,
			srcW,
			x,
			y,
			w,
			h,
			(unsigned long)srcChecksum,
			(unsigned long)vramChecksum,
			w > 0 ? srcStart[0] : 0,
			w > 1 ? srcStart[1] : 0,
			w > 2 ? srcStart[2] : 0,
			w > 3 ? srcStart[3] : 0);
		debugBlitCount++;
	}
	VESA_PresentActiveRectIfFront(x, y, w, h);
}

static void VESA_BlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (pixels == 0 || srcW <= 0 || srcH <= 0 || page >= vesaPageCount)
		return;

	int sx = 0;
	int sy = 0;
	int bw = srcW;
	int bh = srcH;
	if (bw > w) bw = w;
	if (bh > h) bh = h;
	if (!VESA_ClipRect(&x, &y, &bw, &bh, &sx, &sy))
		return;

	uint32_t dst = VESA_Offset(page, x, y);
	const uint8_t* src = pixels + (uint32_t)sy * srcW + sx;
	for (int row = 0; row < bh; row++)
	{
		VESA_WriteSpan(dst, src, bw);
		dst += vesaLineSize;
		src += srcW;
	}
	VESA_PresentActiveRectIfFront(x, y, bw, bh);
}

static void VESA_Scroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonState* state = VID_CommonGetState();
	unsigned page = state->activePage;
	if (page >= vesaPageCount || lines <= 0)
		return;
	if (x < state->minx) x = state->minx;
	if (y < state->miny) y = state->miny;
	if (x + w > state->maxx + 1) w = state->maxx - x + 1;
	if (y + h > state->maxy + 1) h = state->maxy - y + 1;
	if (w <= 0 || h <= lines)
		return;

	uint32_t dst = VESA_Offset(page, x, y);
	uint32_t src = VESA_Offset(page, x, y + lines);
	for (int row = 0; row < h - lines; row++)
	{
		VESA_CopySpan(dst, src, w);
		dst += vesaLineSize;
		src += vesaLineSize;
	}
	for (int row = h - lines; row < h; row++)
	{
		VESA_FillSpan(dst, paper, w);
		dst += vesaLineSize;
	}
	VESA_PresentActiveRectIfFront(x, y, w, h);
}

#endif
