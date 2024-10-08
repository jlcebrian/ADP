#if HAS_SNAPSHOTS

#define NOZIP_IMPLEMENTATION

#include <nozip.h>
#include <ddb.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_bito.h>

static uint8_t* snapshotRAM = 0;
static uint32_t snapshotSize = 0;
static uint8_t* snapshotDDB = 0;

static bool CheckExtension(const char* filename, const char* ext)
{
	const char* p = StrRChr(filename, '.');
	if (!p) return false;
	return StrIComp(p + 1, ext) == 0;
}

static bool AllocateSnapshot(uint32_t size)
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
//  STA Snapshot support (BlueMSX)
// ----------------------------------------------------------------------------

static bool LoadSnapshotFromSTA(File* file)
{
	ZIP_Entry* entries;
	int count = ZIP_Read(&entries, file);

	for (int n = 0; n < count; n++)
	{
		if (StrComp(entries[n].filename, "mapperNormalRam_00") == 0 &&
			entries[n].uncompressed_size == 65556)
		{
			snapshotRAM = Allocate<uint8_t>("Snapshot", 65556);
			if (snapshotRAM == 0)
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				return false;
			}
			snapshotSize = 65536;
			if (!ZIP_Extract(file, entries + n, snapshotRAM, 65556))
			{
				Free(snapshotRAM);
				snapshotRAM = 0;
				DDB_SetError(DDB_ERROR_READING_FILE);
				return false;
			}
			MemMove(snapshotRAM, snapshotRAM+20, 65536);
			return true;
		}
	}
	return false;
}

// ----------------------------------------------------------------------------
//  VSF Snapshot support
// ----------------------------------------------------------------------------

static bool LoadSnapshotFromVSF(File* file)
{
	static const char* VSFHeader = "VICE Snapshot File\x1A";

	char header[58];
	
	File_Seek(file, 0);
	if (File_Read(file, header, 58) != 58) 
	{
		DDB_SetError(DDB_ERROR_READING_FILE);
		return false;
	}
	if (StrComp(header, VSFHeader, 0x13) != 0)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}
	
	bool is128k = false;
	if (StrComp(header+0x15, "C128", 4) == 0)
		is128k = true;
	else if (StrComp(header+0x15, "C64", 3) != 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	uint64_t fileSize = File_GetSize(file);
	uint64_t position = File_GetPosition(file);
	for (;;)
	{
		if (position >= fileSize)
			return false;

		char moduleHeader[22];
		if (File_Read(file, moduleHeader, 22) != 22)
		{
			DDB_SetError(DDB_ERROR_READING_FILE);
			return false;
		}
		uint32_t moduleSize = read32LE((uint8_t*)moduleHeader + 18);
		if (StrComp(moduleHeader, "C64MEM") == 0)
		{
			File_Seek(file, position + 0x1A);
			snapshotRAM = Allocate<uint8_t>("Snapshot", 65536);
			if (snapshotRAM == 0)
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				return false;
			}
			snapshotSize = 65536;
			if (File_Read(file, snapshotRAM, 65536) != 65536)
			{
				Free(snapshotRAM);
				snapshotRAM = 0;
				DDB_SetError(DDB_ERROR_READING_FILE);
				return false;
			}
			return true;
		}
		else if (StrComp(moduleHeader, "C128MEM") == 0)
		{
			File_Seek(file, position + 0x1A);
			snapshotRAM = Allocate<uint8_t>("Snapshot", 131072);
			if (snapshotRAM == 0)
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				return false;
			}
			snapshotSize = 131072;
			if (File_Read(file, snapshotRAM, 131072) != 131072)
			{
				Free(snapshotRAM);
				snapshotRAM = 0;
				DDB_SetError(DDB_ERROR_READING_FILE);
				return false;
			}
			return true;
		}
		position += moduleSize;
		File_Seek(file, position);
	}
}

// ----------------------------------------------------------------------------
//  SNA Snapshot support
// ----------------------------------------------------------------------------

static bool LoadCPCSnapshotFromSNA(File* file)
{
	char header[8];
	
	File_Seek(file, 0);
	if (File_Read(file, header, 8) != 8)
		return false;
	if (StrComp(header, "MV - SNA", 8) != 0)
		return false;

	uint16_t memsize;
	File_Seek(file, 0x6b);
	if (File_Read(file, &memsize, 2) != 2)
	{
		DDB_SetError(DDB_ERROR_READING_FILE);
		return false;
	}
	memsize = fix16(memsize, true);
	if (memsize != 64 && memsize != 128)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	snapshotRAM = Allocate<uint8_t>("Snapshot", 65536);
	if (snapshotRAM == 0)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	snapshotSize = 65536;
	File_Seek(file, 0x100);
	if (File_Read(file, snapshotRAM, 65536) != 65536)
	{
		Free(snapshotRAM);
		snapshotRAM = 0;
		DDB_SetError(DDB_ERROR_READING_FILE);
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------------
//  ZX Spectrum SNA Snapshot support
// ----------------------------------------------------------------------------

#pragma pack(push, 1)
struct SNASnapshotHeader
{
	uint8_t	I;
	uint16_t HLp, DEp, BCp, AFp, HL, DE, BC, IY, IX;
	uint8_t interrupt, R;
	uint16_t AF, SP;
	uint8_t IM, border;
};
struct SNAP128KHeader
{
	uint16_t PC;
	uint8_t  pager, trDOSPage;
};
#pragma pack(pop)

static bool LoadSnapshotFromSNA (File* file)
{
	uint64_t fileSize = File_GetSize(file);

	if (fileSize == 131103 || fileSize == 147487)
	{
		// This is a 128K snapshot
		SNAP128KHeader header;

		AllocateSnapshot(0x30000);
		File_Seek(file, sizeof(SNASnapshotHeader));
		File_Read(file, snapshotRAM + 16384, 49152);
		File_Read(file, &header, sizeof(header));

		uint8_t page = header.pager & 7;
		MemCopy(snapshotRAM + 0x24000 /* page  5 */, snapshotRAM + 0x4000, 0x4000);
		MemCopy(snapshotRAM + 0x18000 /* page  2 */, snapshotRAM + 0x8000, 0x4000);
		MemCopy(snapshotRAM + 0x10000 + page*0x4000, snapshotRAM + 0xC000, 0x4000);
		for (int i = 0; i < 8; i++) 
		{
			if (i != page && i != 2 && i != 5)
				File_Read(file, snapshotRAM + 0x10000 + i * 0x4000, 0x4000);
		}

		// Select page 0
		MemCopy(snapshotRAM + 0xC000, snapshotRAM + 0x10000, 0x4000);

		return true;
	}

	if (fileSize != 49179)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	AllocateSnapshot(65536);
	File_Seek(file, sizeof(SNASnapshotHeader));
	File_Read(file, snapshotRAM + 16384, 49152);
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
	uint8_t  out7ffd;
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
		switch (page)
		{
			case  3: return snapshotRAM + 0x10000;
			case  4: return snapshotRAM + 0x14000;
			case  5: return snapshotRAM + 0x18000;
			case  6: return snapshotRAM + 0x1C000;
			case  7: return snapshotRAM + 0x20000;
			case  8: return snapshotRAM + 0x24000;
			case  9: return snapshotRAM + 0x28000;
			case 10: return snapshotRAM + 0x2C000;
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

	File_Seek(file, 0);

	uint8_t* data = Allocate<uint8_t>("Snapshot file", (size_t)fileSize);
	if (data == 0)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
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
	AllocateSnapshot((IsZ8048K(version, mode) ? 64 : 192) * 1024);

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
			if (length == 0xFFFF)
			{
				compressed = false;
				length = 16384;
			}
			if (ptr + length > end)
				break;
			if (out != 0)
			{
				if (compressed)
					DecompressZ80Block(ptr, ptr + length, out, out + 16384);
				else
					DecompressZ80Block(ptr, ptr + length, out, out + 16384);
					// MemCopy(out, ptr, length);
			}
			ptr += length;
		}
	}

	if (!IsZ8048K(version, mode))
	{
		// Copy the 128K pages into 48K address space
		MemCopy(snapshotRAM + 0x4000, snapshotRAM + 0x24000, 0x4000);	// Page 5
		MemCopy(snapshotRAM + 0x8000, snapshotRAM + 0x18000, 0x4000);	// Page 2
		MemCopy(snapshotRAM + 0xC000, snapshotRAM + 0x10000, 0x4000);	// Page 0
	}

	Free(data);
	return true;
}

// ----------------------------------------------------------------------------
//  TAP Tape file support
// ----------------------------------------------------------------------------

#pragma pack(push, 1)
struct TAPBlockHeader
{
	uint8_t flag;
	uint8_t type;
	uint8_t name[10];
	uint16_t length;
	uint16_t param0;
	uint16_t param1;
	uint8_t checksum;
};
#pragma pack(pop)

static bool LoadSnapshotFromTAP (File* file)
{
	uint64_t fileSize = File_GetSize(file);
	uint64_t position = File_GetPosition(file);
	int count = 0;

	AllocateSnapshot(64 * 1024);

	while (position < fileSize)
	{
		uint16_t headerSize;
		if (File_Read(file, &headerSize, 2) != 2) 
		{
			DDB_SetError(DDB_ERROR_READING_FILE);
			return false;
		}
		fix16(headerSize, true);
		position += headerSize + 2;
		if (headerSize != sizeof(TAPBlockHeader))
		{
			File_Seek(file, position);
			continue;
		}

		TAPBlockHeader header;
		if (File_Read(file, &header, headerSize) != headerSize)
		{
			DDB_SetError(DDB_ERROR_READING_FILE);
			return false;
		}
		if (header.flag != 0)
			continue;

		const uint8_t* ptr = (const uint8_t*)&header;
		uint8_t checksum = 0;
		for (int i = 0; i < headerSize-1; i++)
			checksum ^= ptr[i];
		if (checksum != header.checksum)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			return false;
		}

		if (header.flag == 0 && header.type == 3)	// Header block for a CODE file
		{
			uint16_t dataAddress = fix16(header.param0, true);
			uint16_t length = fix16(header.length, true);
			if (dataAddress < 0x4000 || dataAddress + length > 65536)
			{
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				return false;
			}

			File_Seek(file, position);
			uint16_t dataSize;
			if (File_Read(file, &dataSize, 2) != 2) 
			{
				DDB_SetError(DDB_ERROR_READING_FILE);
				return false;
			}
			fix16(dataSize, true);
			if (dataSize != length + 2)
			{
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				return false;
			}

			File_Seek(file, position + 3);
			if (File_Read(file, snapshotRAM + dataAddress, length) != length)
			{
				DDB_SetError(DDB_ERROR_READING_FILE);
				return false;
			}
			position += dataSize + 2;
			File_Seek(file, position);
			count++;
		}
	}

	return count > 0;
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
//  RAW file support
// ----------------------------------------------------------------------------

// This loads a binary file into memory and tries to search for a valid DDB
// file inside. It is not very reliable, but may be the only option for
// some games which are stored in uncompressed formats.

bool LoadSnapshotFromRAW(File* file)
{
	uint64_t fileSize = (size_t)File_GetSize(file);
	if (fileSize > 128 * 1024)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	snapshotSize = fileSize > 65536 ? (size_t)fileSize : 65536;
	if (!AllocateSnapshot(snapshotSize))
		return false;

	if (File_Read(file, snapshotRAM, fileSize) != fileSize)
	{
		DDB_SetError(DDB_ERROR_READING_FILE);
		return false;
	}

	for (size_t offset = 0; offset < snapshotSize - 32; offset++)
	{
		uint8_t *ptr = snapshotRAM + offset;
		if ((ptr[0] == 1 || ptr[0] == 2) && (ptr[2] == 0x5F))
		{
			DDB_Machine platform = (DDB_Machine)(ptr[1] >> 4);
			uint16_t baseOffset  = 0;
			switch (platform)
			{
				case DDB_MACHINE_SPECTRUM: baseOffset = 0x8400; break;
				case DDB_MACHINE_C64:      baseOffset = 0x3880; break;
				case DDB_MACHINE_CPC:      baseOffset = 0x2880; break;
				case DDB_MACHINE_MSX:      baseOffset = 0x0100; break;
				case DDB_MACHINE_PLUS4:    baseOffset = 0x7080; break;
				default: break;
			}
			if (baseOffset != 0)
			{
				// Check the vocabulary table
				uint16_t voc = read16(ptr + 0x16, true);
				if (voc < baseOffset)
					continue;
				uint8_t* vocdata = ptr + voc - baseOffset;
				bool valid = true;
				bool spaces = false;
				bool endingZero = false;
				int wordCount = 0;
				while (vocdata < snapshotRAM + snapshotSize)
				{
					if (vocdata[0] == 0)	// Vocabulary must end with a zero
					{
						endingZero = true;
						break;
					}
					if (vocdata[6] > 6)		// Word type must be 0-6
					{
						valid = false;
						break;
					}
					if (vocdata[4] == 0xDF)	// Vocabulary must have at least one short word
						spaces = true;
					vocdata += 7;
					wordCount++;
				}
				if (wordCount < 16 || !spaces || !valid)
					continue;

				// Move the data into its base address
				uint16_t remaining = 65536 - baseOffset;
				snapshotDDB = snapshotRAM + baseOffset;
				MemMove(snapshotDDB, ptr, remaining);
				return true;
			}
		}
	}

	return false;
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
	else if (CheckExtension(filename, "sna"))
	{
		if (LoadCPCSnapshotFromSNA(file))
		{
			if (machine) *machine = DDB_MACHINE_CPC;
			if (ram) *ram = snapshotRAM;
			if (size) *size = snapshotSize;
			return true;
		}
		if (LoadSnapshotFromSNA(file))
		{
			if (machine) *machine = DDB_MACHINE_SPECTRUM;
			if (ram) *ram = snapshotRAM;
			if (size) *size = snapshotSize;
			return true;
		}
		return false;
	}
	else if (CheckExtension(filename, "tzx"))
	{
		if (!LoadSnapshotFromTZX(file))
			return false;

		if (machine) *machine = DDB_MACHINE_SPECTRUM;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}
	else if (CheckExtension(filename, "sta"))
	{
		if (!LoadSnapshotFromSTA(file))
			return false;

		if (machine) *machine = DDB_MACHINE_MSX;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}
	else if (CheckExtension(filename, "vsf"))
	{
		if (!LoadSnapshotFromVSF(file))
			return false;

		if (machine) *machine = DDB_MACHINE_C64;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}
	else if (CheckExtension(filename, "tap"))
	{
		if (!LoadSnapshotFromTAP(file))
			return false;

		if (machine) *machine = DDB_MACHINE_SPECTRUM;
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;
		return true;
	}
	else if (CheckExtension(filename, "cas") ||
			 CheckExtension(filename, "bin") ||
			 CheckExtension(filename, "rom") ||
			 CheckExtension(filename, "raw"))
	{
		if (!LoadSnapshotFromRAW(file))
			return false;

		if (machine) *machine = (DDB_Machine)(snapshotDDB[1] >> 4);
		if (ram) *ram = snapshotRAM;
		if (size) *size = snapshotSize;

		return true;
	}
	else
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_SUPPORTED);
		return false;
	}

	return false;
}

#endif