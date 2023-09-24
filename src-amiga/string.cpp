#include <os_types.h>
#include <os_char.h>

#ifdef _AMIGA

#include "gcc8_c_support.h"

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
	uint8_t* dstPtr = (uint8_t*)dst;
	uint8_t* srcPtr = (uint8_t*)src;
	if (dstPtr < srcPtr)
	{
		while (size--)
			*dstPtr++ = *srcPtr++;
	}
	else
	{
		dstPtr += size;
		srcPtr += size;
		while (size--)
			*--dstPtr = *--srcPtr;
	}
	return dst;
}

void *MemCopy(void *dst, const void *src, size_t size)
{
	uint8_t* dstPtr = (uint8_t*)dst;
	uint8_t* srcPtr = (uint8_t*)src;
	while (size--)
		*dstPtr++ = *srcPtr++;
	return dst;
}

size_t StrLen(const char *str)
{
	size_t len = 0;
	while (*str++)
		len++;
	return len;
}

size_t StrCopy(char *dst, uint32_t dstSize, const char *src)
{
	if (dstSize == 0)
		return 0;

	size_t len = 0;
	dstSize--;
	while (*src && len < dstSize)
	{
		*dst++ = *src++;
		len++;
	}
	*dst = 0;
	len++;
	return len;
}

const char* StrRChr(const char* ptr, char c)
{
	const char* last = 0;
	while (*ptr)
	{
		if (*ptr == c)
			last = ptr;
		ptr++;
	}
	return last;
}

int StrIComp(const char *dst, const char *src)
{
	while (*dst && *src && ToLower(*dst) == ToLower(*src))
	{
		dst++;
		src++;
	}
	return *dst - *src;
}

int StrComp(const char *dst, const char *src)
{
	while (*dst && *src && *dst == *src)
	{
		dst++;
		src++;
	}
	return *dst - *src;
}

int StrComp(const char *dst, const char *src, size_t maxSize)
{
	if (maxSize == 0)
		return 0;
	while (*dst && *src && *dst == *src && maxSize--)
	{
		dst++;
		src++;
	}
	return *dst - *src;
}

#endif