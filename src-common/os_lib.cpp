#include <os_lib.h>
#include <os_file.h>

static char const digits[16] =
{
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F',
};

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

void* OSAlloc(size_t size)
{
	return malloc(size);
}

void OSFree(void* ptr)
{
	free(ptr);
}

void Abort()
{
	abort();
}

#endif