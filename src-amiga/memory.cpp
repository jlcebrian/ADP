#include <os_lib.h>

#ifdef _AMIGA

#include <proto/dos.h>
#include <proto/exec.h>

struct BlockHeader
{
	LONG signature;
	struct BlockHeader *prev;
	struct BlockHeader *next;
	size_t size;
};

const LONG  BlockHeaderSignature = 0XDEADBEEFu;
const size_t BlockHeaderSize = sizeof(BlockHeader);

BlockHeader* os_firstBlock = 0;
LONG         os_blockCount = 0;
LONG         os_totalAllocated = 0;

void *OSAlloc(size_t size)
{
	void* block = AllocMem(size + BlockHeaderSize, MEMF_ANY | MEMF_CLEAR);
	if (block == 0)
		return 0;

	void* data  = (void*)((ULONG)block + BlockHeaderSize);
	BlockHeader* header = (BlockHeader*)block;
	header->signature = BlockHeaderSignature;
	header->prev = 0;
	header->next = os_firstBlock;
	header->size = size;
	if (os_firstBlock != 0)
		os_firstBlock->prev = header;
	os_firstBlock = header;
	os_blockCount++;
	os_totalAllocated += size;

	return data;
}

void OSFree(void *ptr)
{
	void* block = (void*)((ULONG)ptr - BlockHeaderSize);
	BlockHeader* header = (BlockHeader*)block;
	if (header->signature != BlockHeaderSignature)
		return;

	if (header->prev != 0)
		header->prev->next = header->next;
	if (header->next != 0)
		header->next->prev = header->prev;
	if (header == os_firstBlock)
		os_firstBlock = header->next;
	os_blockCount--;
	os_totalAllocated -= header->size;

	FreeMem(block, header->size + BlockHeaderSize);
}

#endif