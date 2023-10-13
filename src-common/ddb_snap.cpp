#if HAS_SNAPSHOTS

#include "ddb.h"
#include "os_file.h"
#include "os_lib.h"
#include "os_mem.h"
#include "os_bito.h"

static uint8_t* snapshotRAM = 0;
static size_t   snapshotSize = 0;

static bool CheckExtension(const char* filename, const char* ext)
{
	const char* p = StrRChr(filename, '.');
	if (!p) return false;
	return StrIComp(p + 1, ext) == 0;
}

static bool AllocateSnapshot(size_t size)
{
	if (snapshotRAM != 0 && snapshotSize >= size)
		return true;
	snapshotRAM = Allocate<uint8_t>("Snapshot RAM", size);
	if (snapshotRAM == 0)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	snapshotSize = size;
	return true;
}

// ----------------------------------------------------------------------------
//  Z80 Snapshot support
// ----------------------------------------------------------------------------

#pragma pack(push, 1)
struct Z80SnapshotHeader
{
	uint8_t	A, F;
	uint8_t	C, B;
	uint8_t	L, H;
	uint8_t	PC_L, PC_H;
	uint8_t	SP_L, SP_H;
	uint8_t	I, R;
	uint8_t flags;
	uint8_t	E, D;
	uint8_t	C_, B_;
	uint8_t	E_, D_;
	uint8_t	L_, H_;
	uint8_t	A_, F_;
	uint8_t	IY_L, IY_H;
	uint8_t	IX_L, IX_H;
	uint8_t	IFF1, IFF2;
	uint8_t	flags2;	
};
struct Z80SnapshotExtraHeader
{
	uint16_t extraHeaderLength;
	uint16_t PC;
	uint8_t  mode;
	uint8_t  _out7ffd;
	uint8_t  _if1paged;
	uint8_t  flags;
	
	// There are more info after this, but we don't care about it
};
#pragma pack(pop)

const int Z80FLAG_COMPRESSED = 0x20;

static void DecompressZ80Block (const uint8_t* ptr, const uint8_t* end, uint8_t* out, uint8_t* outEnd)
{
	while (ptr < end && out < outEnd)
	{
		if (ptr[0] == 0x00 && ptr <= end-4 && ptr[1] == 0xED && ptr[2] == 0xED && ptr[3] == 0x00)
			break;
		if (ptr[0] == 0xED && ptr <= end-4 && ptr[1] == 0xED)
		{
			uint8_t count = ptr[2];
			if (count == 0 || ptr == end-3)
				break;

			uint8_t value = ptr[3];
			for (int i = 0; i < count && out < outEnd; i++)
				*out++ = value;
			ptr += 4;
		}
		else
		{
			*out++ = *ptr++;
		}
	}
}

static bool IsZ8048K (int version, int mode)
{
	return version == 1 || (version == 2 && mode < 3) || (version == 3 && mode < 4);
}

static uint8_t* GetZ80RAMPage (int version, int mode, int page)
{
	// We don't care about SAMRAM stuff
	if (IsZ8048K(version, mode))
	{
		switch (page)
		{
			case 4: return snapshotRAM + 0x8000;
			case 5: return snapshotRAM + 0xC000;
			case 8: return snapshotRAM + 0x4000;
			default: return 0;
		}
	}
	else
	{
		// Consider that page 0 is selected
		switch (page)
		{
			case 2: return snapshotRAM + 0x8000;
			case 5: return snapshotRAM + 0x4000;
			case 0: return snapshotRAM + 0xC000;
			case 1: return snapshotRAM + 0x10000;
			case 3: return snapshotRAM + 0x14000;
			case 4: return snapshotRAM + 0x18000;
			case 6: return snapshotRAM + 0x1C000;
			case 7: return snapshotRAM + 0x20000;
			default: return 0;
		}
	}

}

static bool LoadSnapshotFromZ80 (File* file)
{
	int version = 1;

	uint64_t fileSize = File_GetSize(file);
	if (fileSize > 200 * 1024)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	uint8_t* data = Allocate<uint8_t>("Snapshot file", (size_t)fileSize);
	if (File_Read(file, data, fileSize) != fileSize)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}

	Z80SnapshotHeader* header = (Z80SnapshotHeader*)data;
	Z80SnapshotExtraHeader* extraHeader = (Z80SnapshotExtraHeader*)(data + 30);
	uint8_t* start = data + 30;
	int mode = 0;
	if (header->flags == 0xFF)
		header->flags = 1;
	if (header->PC_H == 0 && header->PC_L == 0)
	{
		extraHeader->extraHeaderLength = fix16(extraHeader->extraHeaderLength, true);
		mode = extraHeader->mode;
		start += extraHeader->extraHeaderLength + 2;
		version = 2;
		if (extraHeader->extraHeaderLength > 23)
			version = 3;
	}

	// This allocates space for the ROM just for convenience
	AllocateSnapshot((IsZ8048K(version, mode) ? 64 : 144) * 1024);

	if (version == 1)
	{
		DecompressZ80Block(start, data + fileSize, snapshotRAM + 16384, snapshotRAM + 65536);
	}
	else
	{
		uint8_t* ptr = start;
		uint8_t* end = data + fileSize;
		while (ptr < end)
		{
			uint32_t length = read16(ptr, true);
			uint8_t page = ptr[2];
			uint8_t* out = GetZ80RAMPage(version, mode, page);
			bool compressed = true;

			ptr += 3;
			if (ptr + length > end)
				break;
			if (length == 0xFFFF)
			{
				compressed = false;
				length = 16384;
			}
			if (out != 0)
			{
				if (compressed)
					DecompressZ80Block(ptr, ptr + length, out, out + 16384);
				else
					MemCopy(out, ptr, length);
			}
			ptr += length;
		}
	}

	Free(data);
	return true;
}

// ----------------------------------------------------------------------------
//  TZX Tape file support
// ----------------------------------------------------------------------------
//
// We only support the bare minimum set of features from a TZX file.
// No turbo or fancy loaders! We just want a dump of the DDB data,
// without having to emulate the Spectrum to handle custom loaders,
// so we just load any CODE blocks in the tape and hope for the best.
// Unfortunately, a BASIC loader will often load code blocks in
// a different address compared to the one specified in its header,
// so even that may not work.
//
// There is currently a hack to load Cozumel data specifically (ugh).
// Jabato won't work at all since it has a custom loader.

static bool LoadSnapshotFromTZX (File* file)
{
	char header[10];

	File_Read(file, header, 10);
	if (StrComp(header, "ZXTape!\x1A", 8) != 0)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}

	AllocateSnapshot(128 * 1024);

	bool expectingDataBlock = false;
	bool loadingScreenFound = false;
	uint32_t dataAddress = 0;
	int blockCount = 0;

	uint64_t fileSize = File_GetSize(file);
	while (File_GetPosition(file) < fileSize)
	{
		uint8_t blockType;
		if (File_Read(file, &blockType, 1) != 1)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		switch (blockType)
		{
			case 0x10: // Normal speed data block
			{
				uint16_t pause;
				uint16_t length;
				File_Read(file, &pause, 2);
				File_Read(file, &length, 2);
				pause = fix16(pause, true);
				length = fix16(length, true);
				if (expectingDataBlock)
				{
					if (loadingScreenFound == false && length == 6914)
					{
						loadingScreenFound = true;
						dataAddress = 16384;
					}

					uint8_t flags, checksum;
					File_Read(file, &flags, 1);
					File_Read(file, snapshotRAM + dataAddress, length-2);
					File_Read(file, &checksum, 1);
					expectingDataBlock = false;
					blockCount++;
				}
				else if (length == 19)
				{
					uint8_t data[19];
					if (File_Read(file, data, 19) != 19)
					{
						DDB_SetError(DDB_ERROR_INVALID_FILE);
						return false;
					}
					if (data[0] == 0 && data[1] == 3)	// Header block for a CODE file
					{
						dataAddress = read16(data + 14, true);
						expectingDataBlock = true;
					}
				}
				else
				{
					if (length >= 32768 && blockCount > 0)
					{
						// Desperate attempt to find game data in a headerless block (Cozumel)
						dataAddress = 65535 - (length-2);
						uint8_t flags, checksum;
						File_Read(file, &flags, 1);
						File_Read(file, snapshotRAM + dataAddress, length-2);
						File_Read(file, &checksum, 1);
					}
					else
					{
						File_Seek(file, File_GetPosition(file) + length);
					}
				}
				break;
			}

			case 0x2A: // Stop the tape in 48K mode
			{
				File_Seek(file, File_GetPosition(file) + 4);
				break;
			}

			case 0x2B: // Set signal level
			{
				File_Seek(file, File_GetPosition(file) + 5);
				break;
			}

			case 0x30: // Text description
			{
				uint8_t length;
				File_Read(file, &length, 1);
				File_Seek(file, File_GetPosition(file) + length + 1);
				break;
			}

			case 0x31: // Message block
			{
				uint8_t length;
				File_Read(file, &length, 1);
				File_Seek(file, File_GetPosition(file) + length + 2);
				break;
			}

			case 0x32: // Archive info
			{
				uint16_t blockLength;
				File_Read(file, &blockLength, 2);
				blockLength = fix16(blockLength, true);
				File_Seek(file, File_GetPosition(file) + blockLength);
				break;
			}
			
			case 0x33: // Hardware type
			{
				uint8_t machineCount;
				File_Read(file, &machineCount, 1);
				File_Seek(file, File_GetPosition(file) + machineCount * 3);
				break;
			}

			default:
				DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
				return false;
		}
	}

	return blockCount > 0;
}

// ----------------------------------------------------------------------------
//  Loader
// ----------------------------------------------------------------------------

bool DDB_LoadSnapshot (File* file, const char* filename, uint8_t** ram, size_t* size, DDB_Machine* machine)
{
	if (file == 0)
		return 0;

	if (CheckExtension(filename, "z80"))
	{
		if (!LoadSnapshotFromZ80(file))
			return false;

		if (machine) *machine = DDB_MACHINE_SPECTRUM;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}
	if (CheckExtension(filename, "tzx"))
	{
		if (!LoadSnapshotFromTZX(file))
			return false;

		if (machine) *machine = DDB_MACHINE_SPECTRUM;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}

	File_Close(file);
	return false;
}

#endif