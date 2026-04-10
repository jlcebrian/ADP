#ifdef _DOS

#include <os_lib.h>
#include <alloca.h>
#include <i86.h>

static bool stackWatermarkInit = false;
static uint16_t stackWatermarkSegment = 0;
static uint16_t stackWatermarkBottom = 0;
static uint16_t stackWatermarkTop = 0;
static const uint8_t stackWatermarkPattern = 0xA5;

void DOS_InitStackWatermark()
{
#if defined(__386__)
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
