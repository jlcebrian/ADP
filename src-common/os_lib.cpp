#include <os_lib.h>
#include <os_file.h>

#if defined(_ATARIST) && defined(_DEBUGPRINT)
#include <stdarg.h>
#include <stdio.h>
#include <osbind.h>

void DebugPrintfImpl(const char* format, ...)
{
	char formatted[512];
	va_list args;
	va_start(args, format);
	vsnprintf(formatted, sizeof(formatted), format, args);
	va_end(args);

	for (size_t readPos = 0; formatted[readPos] != 0; ++readPos)
	{
		if (formatted[readPos] == '\n')
			Bconout(1, '\r');
		Bconout(1, formatted[readPos]);
	}
}
#endif

static char const digits[16] =
{
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F',
};

void IntToHex2(int value, char* buffer)
{
	buffer[0] = digits[(value >> 4) & 0x0F];
	buffer[1] = digits[value & 0x0F];
}

char *LongToChar(long value, char *buffer, int radix)
{
	char *p;
	int   neg = 0;
	char  tmpbuf[8 * sizeof(long) + 2];
	short i = 0;

	if (radix < 2)
		return buffer;
	if (value < 0)
	{
		neg = 1;
		value = -value;
	}
	do
	{
		tmpbuf[i++] = digits[value % radix];
	}
	while ((value /= radix) != 0);
	if (neg)
	{
		tmpbuf[i++] = '-';
	}
	p = buffer;
	while (--i >= 0)
	{
		*p++ = tmpbuf[i];
	}
	*p = '\0';

	return p;
}

const char* ChangeExtension(const char* fileName, const char* extension)
{
	static char newFileName[256];

	StrCopy(newFileName, 256, fileName);
	newFileName[256-1] = 0;
	char* ptr = (char *)StrRChr(newFileName, '.');
	if (ptr == 0)
		ptr = newFileName + StrLen(newFileName);
	StrCopy(ptr, newFileName+256-ptr, extension);
	return newFileName;
}

#if _STDCLIB

#ifdef _UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#if defined(_ATARIST)
static uint8_t* osArenaBase = 0;
static size_t osArenaSize = 0;
static size_t osArenaUsed = 0;

static bool OSArenaContains(const void* ptr)
{
	const uint8_t* bytePtr = (const uint8_t*)ptr;
	return osArenaBase != 0 && bytePtr >= osArenaBase && bytePtr < osArenaBase + osArenaSize;
}

size_t OSReserveArena(size_t size)
{
	if (osArenaBase != 0)
		return osArenaSize;

	size &= ~(size_t)15;
	while (size >= 32768)
	{
		uint8_t* block = (uint8_t*)malloc(size);
		if (block != 0)
		{
			osArenaBase = block;
			osArenaSize = size;
			osArenaUsed = 0;
			return size;
		}
		size -= 4096;
		size &= ~(size_t)15;
	}

	return 0;
}

void OSReleaseArena()
{
	if (osArenaBase != 0)
		free(osArenaBase);
	osArenaBase = 0;
	osArenaSize = 0;
	osArenaUsed = 0;
}

size_t OSGetArenaSize()
{
	return osArenaSize;
}

size_t OSGetFree()
{
	if (osArenaBase == 0 || osArenaUsed >= osArenaSize)
		return 0;
	return osArenaSize - osArenaUsed;
}

#else

size_t OSReserveArena(size_t size)
{
	(void)size;
	return 0;
}

void OSReleaseArena()
{
}

size_t OSGetArenaSize()
{
	return 0;
}

size_t OSGetFree()
{
	return 0;
}
#endif

void *MemClear(void *mem, size_t size)
{
	return memset(mem, 0, size);
}

void *MemSet(void *mem, uint8_t val, size_t size)
{
	return memset(mem, val, size);
}

void *MemMove(void *dst, const void *src, size_t size)
{
	return memmove(dst, src, size);
}

void *MemCopy(void *dst, const void *src, size_t size)
{
	return memcpy(dst, src, size);
}

int MemComp(void *dst, const void *src, size_t size)
{
	return memcmp(dst, src, size);
}

size_t StrCopy(char *dst, uint32_t dstSize, const char *src)
{
	char* ret = strncpy(dst, src, dstSize);
	return ret == 0 ? 0 : strlen(dst)+1;
}

const char *StrRChr(const char *ptr, char c)
{
	return strrchr(ptr, c);
}

size_t StrCat(char *dst, uint32_t dstSize, const char *src)
{
	char* ret = strncat(dst, src, dstSize);
	return ret == 0 ? 0 : strlen(dst)+1;
}

int StrComp(const char *dst, const char *src)
{
	return strcmp(dst, src);
}

int StrIComp(const char *dst, const char *src)
{
#ifdef HAS_STRCASECMP
	return strcasecmp(dst, src);
#else
	return stricmp(dst, src);
#endif
}

int StrComp(const char *dst, const char *src, size_t maxSize)
{
	return strncmp(dst, src, maxSize);
}

uint32_t RandInt(uint32_t min, uint32_t max)
{
	return min + rand() % (max - min + 1);
}

void* OSAlloc(size_t size, OSMemoryPool pool)
{
	(void)pool;
	#if defined(_ATARIST)
	size = (size + 15) & ~(size_t)15;
	if (osArenaBase != 0)
	{
		if (osArenaUsed + size > osArenaSize)
		{
			DebugPrintf("OS arena exhausted: requested=%lu used=%lu size=%lu\n",
				(unsigned long)size,
				(unsigned long)osArenaUsed,
				(unsigned long)osArenaSize);
			return 0;
		}

		void* ptr = osArenaBase + osArenaUsed;
		osArenaUsed += size;
		return ptr;
	}
	#endif

	return malloc(size);
}

void OSFree(void* ptr)
{
	if (ptr == 0)
		return;

	#if defined(_ATARIST)
	if (OSArenaContains(ptr))
		return;
	#endif

	free(ptr);
}

void Abort()
{
	abort();
}

#else

void Abort()
{
#ifdef _DEBUGPRINT
	DebugPrintf("Abort()\n");
#endif
	while (true)
		;
}

#endif
