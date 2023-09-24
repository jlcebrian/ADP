#include <os_mem.h>

#define BLOCK_SIZE 65536

Arena* AllocateArena (const char* reason)
{
	Arena* arena = Allocate<Arena>(reason);
	if (arena == 0)
		return 0;

	arena->block = Allocate<uint8_t>(reason, BLOCK_SIZE);
	arena->size  = BLOCK_SIZE;
	arena->used  = 0;
	arena->next  = 0;
	arena->name  = reason;
	return arena;
}

void FreeArena(Arena* arena)
{
	while (arena != 0)
	{
		Arena* next = arena->next;
		Free(arena->block);
		Free(arena);
		arena = next;
	}
}

void* AllocateBlock (Arena* arena, size_t bytes, bool zero)
{
	while (arena != 0)
	{
		if (arena->size - arena->used > bytes)
		{
			void* ptr = arena->block + arena->used;
			arena->used += bytes;
			return ptr;
		}
		if (arena->next == 0)
		{
			Arena* next = Allocate<Arena>(arena->name);
			if (next == 0)
				return 0;

			size_t size = bytes > BLOCK_SIZE ? ((bytes + 15)&~15) : BLOCK_SIZE;
			next->block = Allocate<uint8_t>(arena->name, size);
			if (next->block == 0)
			{
				Free(next);
				return 0;
			}
			next->size = size;
			next->used = bytes;
			next->name = arena->name;
			next->next = 0;
			arena->next = next;
			return next->block;
		}
	}

	return 0;
}