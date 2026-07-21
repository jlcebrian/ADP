#ifdef _DOS

#include <os_lib.h>
#include <alloca.h>
#include <i86.h>

static bool stackWatermarkInit = false;
static uint16_t stackWatermarkSegment = 0;
static uint16_t stackWatermarkBottom = 0;
static uint16_t stackWatermarkTop = 0;
static const uint8_t stackWatermarkPattern = 0xA5;

#if defined(__386__)
static uint32_t stackWatermarkTop32 = 0;
static uint32_t stackWatermarkLow32 = 0;
// Keep in sync with STACK_SIZE in Makefile-dos (32-bit build links a 16K stack).
#define DOS_STACK_BYTES 16384
#endif

void DOS_InitStackWatermark()
{
#if defined(__386__)
	// Fill the reserved-but-unused portion of the 32-bit stack (below the
	// current frame) with a known pattern; DOS_GetStackWatermark later scans it
	// to report peak usage. Debug builds only.
	#if defined(_DEBUGPRINT) || defined(DEBUG_ALLOCS)
	if (stackWatermarkInit)
		return;
	uint32_t marker;
	uint32_t sp = (uint32_t)&marker;
	uint32_t top = sp - 64;                          // leave the live frame alone
	uint32_t low = sp - (DOS_STACK_BYTES - 1024);    // ~1K guard above the true bottom
	stackWatermarkTop32 = sp;
	stackWatermarkLow32 = low;
	for (uint32_t a = low; a < top; ++a)
		*(volatile uint8_t*)a = stackWatermarkPattern;
	stackWatermarkInit = true;
	#endif
	return;
#else
	if (stackWatermarkInit)
		return;

	uint8_t marker = 0;
	uint16_t stackSegment = FP_SEG(&marker);
	uint16_t stackPointer = FP_OFF(&marker);
	unsigned available = stackavail();
	const unsigned guardBytes = 256;

	if (available <= guardBytes + 32 || stackPointer <= guardBytes)
		return;

	uint16_t top = (uint16_t)(stackPointer - guardBytes);
	uint16_t tracked = (uint16_t)(available - guardBytes);
	uint16_t bottom = tracked < top ? (uint16_t)(top - tracked) : 0;
	uint8_t __far* ptr = (uint8_t __far*)MK_FP(stackSegment, bottom);

	for (uint16_t offset = bottom; offset < top; ++offset)
		*ptr++ = stackWatermarkPattern;

	stackWatermarkSegment = stackSegment;
	stackWatermarkBottom = bottom;
	stackWatermarkTop = top;
	stackWatermarkInit = true;
#endif
}

void DOS_GetStackWatermark(uint32_t* usedBytes, uint32_t* totalBytes)
{
	if (usedBytes != NULL)
		*usedBytes = 0;
	if (totalBytes != NULL)
		*totalBytes = 0;

#if defined(__386__)
	#if defined(_DEBUGPRINT) || defined(DEBUG_ALLOCS)
	if (!stackWatermarkInit)
		return;
	uint32_t firstTouched = stackWatermarkTop32;
	for (uint32_t a = stackWatermarkLow32; a < stackWatermarkTop32 - 64; ++a)
	{
		if (*(volatile uint8_t*)a != stackWatermarkPattern)
		{
			firstTouched = a;
			break;
		}
	}
	if (usedBytes != NULL)
		*usedBytes = stackWatermarkTop32 - firstTouched;
	if (totalBytes != NULL)
		*totalBytes = DOS_STACK_BYTES;
	#endif
	return;
#else
	if (!stackWatermarkInit || stackWatermarkTop <= stackWatermarkBottom)
		return;

	uint8_t __far* ptr = (uint8_t __far*)MK_FP(stackWatermarkSegment, stackWatermarkBottom);
	uint16_t firstTouched = stackWatermarkTop;

	for (uint16_t offset = stackWatermarkBottom; offset < stackWatermarkTop; ++offset, ++ptr)
	{
		if (*ptr != stackWatermarkPattern)
		{
			firstTouched = offset;
			break;
		}
	}

	if (usedBytes != NULL)
		*usedBytes = (uint32_t)(stackWatermarkTop - firstTouched);
	if (totalBytes != NULL)
		*totalBytes = (uint32_t)(stackWatermarkTop - stackWatermarkBottom);
#endif
}

#ifdef _DEBUGPRINT

#include <stdarg.h>
#include <stdio.h>

void DebugPrintfImpl(const char* format, ...)
{
	static FILE* debugLog;
	static bool debugLogInit;
	static char formatted[512];
	va_list args;

	if (!debugLogInit)
	{
		debugLogInit = true;
		debugLog = fopen("ADPDBG.LOG", "wt");
		if (debugLog != NULL)
			setvbuf(debugLog, NULL, _IONBF, 0);
	}
	if (debugLog == NULL)
		return;

	va_start(args, format);
	vsnprintf(formatted, sizeof(formatted), format, args);
	va_end(args);

	for (size_t readPos = 0; formatted[readPos] != 0; ++readPos)
	{
		if (formatted[readPos] == '\n')
			fputc('\r', debugLog);
		fputc(formatted[readPos], debugLog);
	}
	fflush(debugLog);
}

#endif

#endif
