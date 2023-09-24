#include <os_types.h>
#include <os_lib.h>
#include <os_mem.h>
#include <ddb_vid.h>

#if DEBUG_ALLOCS

#define BLOCK_SIGNATURE 0xCAADDAAD

struct BlockInfo
{
	uint32_t signature;
	const char* reason;
	size_t size;
	BlockInfo* next;
	BlockInfo* prev;
};

BlockInfo* firstBlock = 0;
BlockInfo* lastBlock = 0;

static void PrintDebugText(int x, int y, const char* text)
{
	for (int n = 0; text[n]; n++)
		VID_DrawCharacter(x+6*n, y, text[n], 15, 255);
}

void CorruptedBlockChain()
{
	PrintDebugText(0, 0, "Corrupted memory block chain");
	int y = 8;

	char buf[16];
	BlockInfo*r = firstBlock;
	while (r->signature == BLOCK_SIGNATURE)
	{
		LongToChar((size_t)r, buf, 16);
		PrintDebugText(0, y, "-");
		PrintDebugText(12, y, buf);
		PrintDebugText(32, y, r->reason);
		r = r->next;
		y += 8;
		if (y == 200) y = 8;
	}
	LongToChar((size_t)r, buf, 16);
	PrintDebugText(0, y, "*");
	PrintDebugText(12, y, buf);
	PrintDebugText(32, y, "*** CORRUPTED ***");
	Abort();
}

void SanityCheck()
{
	BlockInfo* r = firstBlock;
	while (r)
	{
		if (r->signature != BLOCK_SIGNATURE)
			CorruptedBlockChain();
		r = r->next;
	}
}

void* AllocateBlock (const char* reason, size_t bytes, bool zero)
{
	SanityCheck();

	BlockInfo* r = (BlockInfo*)OSAlloc(bytes + sizeof(BlockInfo));
	if (r == 0)
		return r;

	r->signature = BLOCK_SIGNATURE;
	r->reason    = reason;
	r->size      = bytes;
	r->next      = 0;
	r->prev      = lastBlock;
	lastBlock    = r;

	if (r->prev)
		r->prev->next = r;
	if (firstBlock == 0)
		firstBlock = r;

	if (zero)
		MemClear(r+1, bytes);
	return r+1;
}

void Free(void* block)
{
	SanityCheck();

	BlockInfo* r = (BlockInfo*)block - 1;
	if (r->signature == BLOCK_SIGNATURE)
	{
		if (r->prev) r->prev->next = r->next;
		if (r->next) r->next->prev = r->prev;
		if (firstBlock == r) firstBlock = r->next;
		if (lastBlock == r) lastBlock = r->prev;
		OSFree(r);
		return;
	}

	//fputs("WRONG free: no block!\n\r", stderr);
	Abort();
}

void DumpMemory(uint32_t maxb)
{
	SanityCheck();

	BlockInfo* r = firstBlock;

	int y = 16;

	while (r)
	{
		if (r->signature != BLOCK_SIGNATURE)
			CorruptedBlockChain();

		BlockInfo* prev = r->prev;
		while (prev != 0)
		{
			if (prev->reason == r->reason)
				break;
			prev = prev->prev;
		}

		if (prev == 0)
		{
			uint32_t count = 1;
			uint32_t total = (uint32_t)r->size;
			BlockInfo* next = r->next;
			while (next != 0)
			{
				if (next->signature != BLOCK_SIGNATURE)
					CorruptedBlockChain();

				if (next->reason == r->reason)
				{
					count++;
					total += (uint32_t)next->size;
				}
				next = next->next;
			}

			VID_Clear(6, y, 230, 8, 0);
			
			char buf[16];
			LongToChar(count, buf, 10);
			PrintDebugText(8, y, r->reason);
			LongToChar(count, buf, 10);
			PrintDebugText(180-6*StrLen(buf), y, buf);
			LongToChar(total, buf, 10);
			PrintDebugText(234-6*StrLen(buf), y, buf);
			y += 8;
			if (y >= 180) break;
		}

		r = r->next;
	}

	VID_Clear(  5,  14,   3, y-12, 0);
	VID_Clear(234,  14,   3, y-12, 0);
	VID_Clear(  5,  13, 232,    3, 0);
	VID_Clear(  5,   y, 232,    3, 0);

	VID_Clear(  6,  15,   1, y-14, 15);
	VID_Clear(235,  15,   1, y-14, 15);
	VID_Clear(  6,  14, 230,    1, 15);
	VID_Clear(  6, y+1, 230,    1, 15);

	y += 10;

	char buf[64];
	void *allocs[256+64];
	int n;
	for (n = 0; n < 256; n++)
	{
		allocs[n] = AllocateBlock("MEMTEST", 1024);
		if (allocs[n] == 0)
			break;
	}
	for (int i = 0; i < n; i++)
		Free(allocs[i]);

	buf[0] = ' ';
	LongToChar(n, buf+1, 10);
	StrCat(buf, sizeof(buf), "K free ");
	if (maxb > 0)
	{
		StrCat(buf, sizeof(buf), "/ ");
		LongToChar(maxb, buf+StrLen(buf), 10);
		StrCat(buf, sizeof(buf), " maxb ");
	}
	for (int n = 0; buf[n]; n++)
		VID_DrawCharacter(6+6*n, 2, buf[n], 0, 15);
}

#endif