#pragma once

#include <os_types.h>
#include <os_lib.h>

// ----------------------------------------------------------------------

#ifndef DEBUG_ALLOCS
#define DEBUG_ALLOCS 0
#endif

#if DEBUG_ALLOCS

	extern void* AllocateBlock  (const char* reason, size_t bytes, bool zero = true);
	extern void  Free           (void* block);
	extern void  DumpMemory     (uint32_t maxb = 0);

#else

	static inline void* AllocateBlock(const char* reason, size_t bytes, bool zero = true)
	{
		bytes = (bytes + 3) & ~3;
		void* r = OSAlloc(bytes);
		if (r && zero) 
			MemClear(r, bytes);
		return r;
	}

	static inline void Free(void* block)
	{
		OSFree(block);
	}

#endif

template <class T>
T* Allocate(const char* reason, unsigned count = 1, bool zero = true)
{
	return (T*) AllocateBlock(reason, sizeof(T) * count, zero);
}

// ----------------------------------------------------------------------

struct Arena
{
	const char* name;
	uint8_t *block;
	size_t size;
	size_t used;
	size_t free;
	Arena* next;
};

extern Arena* AllocateArena (const char* reason);
extern void*  AllocateBlock (Arena* arena, size_t bytes, bool zero = true);
extern void   FreeArena     (Arena* arena);

template <class T>
T* Allocate(Arena* arena, unsigned count = 1, bool zero = true)
{
	return (T*) AllocateBlock(arena, sizeof(T) * count, zero);
}