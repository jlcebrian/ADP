#include <ddb.h>
#include <ddb_paw.h>
#include <ddb_vid.h>
#include <ddb_test.h>
#include <ddb_scr.h>
#include <dmg.h>
#include <cli_parser.h>
#include <os_mem.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_bito.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static bool trace = false;
static bool force = false;
static bool showMemoryMap = false;
static bool dumpMessageSamples = true;

static void PrintWarning(const char* message);

#if HAS_PAWS

enum PAWSExtractResult
{
	PAWSExtract_NotHandled,
	PAWSExtract_Success,
	PAWSExtract_Error,
};

struct PAWSTapeComponent
{
	char stem[11];
	char suffix;
	uint16_t loadAddress;
	uint16_t length;
	const uint8_t* data;
};

struct PAWSCandidate
{
	char name[16];
	PAWSTapeComponent components[DDB_SDB_MAX_SEGMENTS];
	uint8_t count;
};

static bool HasExtension(const char* filename, const char* extension)
{
	const char* dot = StrRChr(filename, '.');
	return dot != 0 && StrIComp(dot, extension) == 0;
}

static void NormalizeTapeStem(const uint8_t* name, char* output, size_t outputSize)
{
	size_t length = 9;
	while (length > 0 && name[length - 1] == ' ')
		length--;
	if (length == 0)
	{
		StrCopy(output, outputSize, "PAWS");
		return;
	}
	if (length >= outputSize)
		length = outputSize - 1;
	for (size_t n = 0; n < length; n++)
	{
		uint8_t c = name[n];
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		output[n] = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ? (char)c : '_';
	}
	output[length] = 0;
}

static bool TapeChecksumValid(const uint8_t* data, size_t size)
{
	uint8_t checksum = 0;
	for (size_t n = 0; n < size; n++) checksum ^= data[n];
	return checksum == 0;
}

static bool AddTapePayload(const uint8_t* payload, size_t size,
	uint8_t* pendingName, uint16_t* pendingLength, uint16_t* pendingLoad,
	PAWSTapeComponent* components, int* componentCount)
{
	if (size == 19 && payload[0] == 0 && payload[1] == 3 && TapeChecksumValid(payload, size))
	{
		MemCopy(pendingName, payload + 2, 10);
		*pendingLength = read16LE(payload + 12);
		*pendingLoad = read16LE(payload + 14);
		return true;
	}
	if (*pendingLength != 0 && size == (size_t)*pendingLength + 2 && payload[0] == 0xFF &&
		TapeChecksumValid(payload, size))
	{
		char suffix = (char)pendingName[9];
		if (suffix >= 'A' && suffix <= 'L' && *componentCount < 64)
		{
			PAWSTapeComponent& component = components[(*componentCount)++];
			NormalizeTapeStem(pendingName, component.stem, sizeof(component.stem));
			component.suffix = suffix;
			component.loadAddress = *pendingLoad;
			component.length = *pendingLength;
			component.data = payload + 1;
		}
		*pendingLength = 0;
		return true;
	}
	*pendingLength = 0;
	return true;
}

static bool CollectTAPPayloads(const uint8_t* fileData, size_t fileSize,
	PAWSTapeComponent* components, int* componentCount)
{
	uint8_t pendingName[10] = { 0 };
	uint16_t pendingLength = 0, pendingLoad = 0;
	size_t position = 0;
	while (position + 2 <= fileSize)
	{
		uint16_t length = read16LE(fileData + position);
		position += 2;
		if (position + length > fileSize) return false;
		AddTapePayload(fileData + position, length, pendingName, &pendingLength,
			&pendingLoad, components, componentCount);
		position += length;
	}
	return position == fileSize;
}

static bool CollectTZXPayloads(const uint8_t* fileData, size_t fileSize,
	PAWSTapeComponent* components, int* componentCount)
{
	if (fileSize < 10 || MemComp((void*)fileData, "ZXTape!\x1A", 8) != 0) return false;
	uint8_t pendingName[10] = { 0 };
	uint16_t pendingLength = 0, pendingLoad = 0;
	size_t position = 10;
	while (position < fileSize)
	{
		uint8_t type = fileData[position++];
		uint32_t length = 0;
		bool dataBlock = false;
		switch (type)
		{
			case 0x10:
				if (position + 4 > fileSize) return false;
				length = read16LE(fileData + position + 2);
				position += 4;
				dataBlock = true;
				break;
			case 0x11:
				if (position + 18 > fileSize) return false;
				length = fileData[position + 15] | (fileData[position + 16] << 8) | (fileData[position + 17] << 16);
				position += 18;
				dataBlock = true;
				break;
			case 0x14:
				if (position + 10 > fileSize) return false;
				length = fileData[position + 7] | (fileData[position + 8] << 8) | (fileData[position + 9] << 16);
				position += 10;
				dataBlock = true;
				break;
			case 0x12: length = 4; break;
			case 0x13:
				if (position >= fileSize) return false;
				length = 1 + fileData[position] * 2;
				break;
			case 0x15:
				if (position + 8 > fileSize) return false;
				length = 8 + fileData[position + 5] + (fileData[position + 6] << 8) + (fileData[position + 7] << 16);
				break;
			case 0x18:
			case 0x19:
				if (position + 4 > fileSize) return false;
				length = 4 + read32LE(fileData + position);
				break;
			case 0x20: length = 2; break;
			case 0x21:
				if (position >= fileSize) return false;
				length = 1 + fileData[position];
				break;
			case 0x22: length = 0; break;
			case 0x23: length = 2; break;
			case 0x24: length = 2; break;
			case 0x25: length = 0; break;
			case 0x26:
				if (position + 2 > fileSize) return false;
				length = 2 + read16LE(fileData + position) * 2;
				break;
			case 0x27: length = 0; break;
			case 0x28:
				if (position + 2 > fileSize) return false;
				length = 2 + read16LE(fileData + position);
				break;
			case 0x2A: length = 4; break;
			case 0x2B: length = 5; break;
			case 0x30:
				if (position >= fileSize) return false;
				length = 1 + fileData[position];
				break;
			case 0x31:
				if (position + 2 > fileSize) return false;
				length = 2 + fileData[position + 1];
				break;
			case 0x32:
				if (position + 2 > fileSize) return false;
				length = 2 + read16LE(fileData + position);
				break;
			case 0x33:
				if (position >= fileSize) return false;
				length = 1 + fileData[position] * 3;
				break;
			case 0x34: length = 8; break;
			case 0x35:
				if (position + 20 > fileSize) return false;
				length = 20 + read32LE(fileData + position + 16);
				break;
			case 0x40:
				if (position + 4 > fileSize) return false;
				length = 4 + fileData[position + 1] + (fileData[position + 2] << 8) + (fileData[position + 3] << 16);
				break;
			case 0x5A: length = 9; break;
			default:
				return false;
		}
		if (position + length > fileSize) return false;
		if (dataBlock)
			AddTapePayload(fileData + position, length, pendingName, &pendingLength, &pendingLoad,
				components, componentCount);
		position += length;
	}
	return true;
}

static int CompareComponents(const void* a, const void* b)
{
	const PAWSTapeComponent* ca = (const PAWSTapeComponent*)a;
	const PAWSTapeComponent* cb = (const PAWSTapeComponent*)b;
	return (int)ca->suffix - (int)cb->suffix;
}

static int BuildTapeCandidates(PAWSTapeComponent* components, int componentCount,
	PAWSCandidate* candidates, int candidateCapacity)
{
	int candidateCount = 0;
	for (int n = 0; n < componentCount; n++)
	{
		int candidate = candidateCount - 1;
		bool repeatedStart = candidate >= 0 && components[n].suffix == 'A' &&
			candidates[candidate].count != 0;
		if (candidate < 0 || StrComp(candidates[candidate].name, components[n].stem) != 0 || repeatedStart)
		{
			if (candidateCount >= candidateCapacity) continue;
			candidate = candidateCount++;
			StrCopy(candidates[candidate].name, sizeof(candidates[candidate].name), components[n].stem);
		}
		if (candidates[candidate].count < DDB_SDB_MAX_SEGMENTS)
			candidates[candidate].components[candidates[candidate].count++] = components[n];
	}
	for (int c = 0; c < candidateCount; c++)
		qsort(candidates[c].components, candidates[c].count, sizeof(PAWSTapeComponent), CompareComponents);
	return candidateCount;
}

static bool CandidateSegments(PAWSCandidate* candidate, DDB_SDBSegment* segments,
	uint8_t* segmentCount, uint8_t* model)
{
	if (candidate->count < 2 || (candidate->count & 1) != 0) return false;
	*model = candidate->count > 2 ? DDB_SDB_128K : DDB_SDB_48K;
	for (uint8_t n = 0; n < candidate->count; n += 2)
	{
		PAWSTapeComponent& low = candidate->components[n];
		PAWSTapeComponent& high = candidate->components[n + 1];
		if (low.suffix != 'A' + n || high.suffix != 'B' + n || high.loadAddress < 0xC000 ||
			(uint32_t)high.loadAddress + high.length != 0x10000 ||
			high.loadAddress > 0xFFED) return false;
		uint16_t firstFree = read16LE(high.data + (0xFFED - high.loadAddress));
		uint16_t highStart = read16LE(high.data + (0xFFEF - high.loadAddress));
		uint16_t base = read16LE(high.data + (0xFFFD - high.loadAddress));
		uint8_t bank = high.data[0xFFFF - high.loadAddress];
		if (firstFree != low.loadAddress + low.length || highStart != high.loadAddress ||
			base != (n == 0 ? 0x9300 : 0xC000) ||
			low.loadAddress != (n == 0 ? 0x9300 : 0xC000) ||
			(*model == DDB_SDB_48K ? bank != 0 : bank > 7)) return false;
		segments[n] = { *model == DDB_SDB_48K ? (uint8_t)0xFF : bank,
			DDB_SDB_SEGMENT_LOW, low.loadAddress, low.length, low.data };
		segments[n + 1] = { *model == DDB_SDB_48K ? (uint8_t)0xFF : bank,
			DDB_SDB_SEGMENT_HIGH, high.loadAddress, high.length, high.data };
	}
	*segmentCount = candidate->count;
	return true;
}

static bool FileAlreadyExists(const char* filename)
{
	File* file = File_Open(filename, ReadOnly);
	if (file == 0) return false;
	File_Close(file);
	return true;
}

static void InputDirectory(const char* input, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, input);
	char* slash = (char*)StrRChr(output, '/');
	char* backslash = (char*)StrRChr(output, '\\');
	if (backslash > slash) slash = backslash;
	if (slash) slash[1] = 0;
	else output[0] = 0;
}

static bool WriteCandidateSDB(const char* filename, uint8_t model,
	const DDB_SDBSegment* segments, uint8_t segmentCount)
{
	if (!force && FileAlreadyExists(filename))
	{
		fprintf(stderr, "Error: Output file '%s' already exists (use -f to force overwrite)\n", filename);
		return false;
	}
	if (!DDB_PAWSWriteSDB(filename, model, segments, segmentCount))
	{
		fprintf(stderr, "Writing %s: Error: %s\n", filename, DDB_GetErrorString());
		return false;
	}
	printf("SDB file written to '%s'\n", filename);
	return true;
}

static bool SnapshotSegments(uint8_t* memory, size_t memorySize,
	DDB_SDBSegment* segments, uint8_t* segmentCount, uint8_t* model)
{
	if (memorySize < 0x10000 || read16LE(memory + 0xFFFD) != 0x9300) return false;
	uint16_t firstFree = read16LE(memory + 0xFFED);
	uint16_t highStart = read16LE(memory + 0xFFEF);
	if (firstFree < 0x944E || firstFree > highStart || highStart > 0xFFFF) return false;
	*model = memorySize > 0x10000 ? DDB_SDB_128K : DDB_SDB_48K;
	uint8_t count = 0;
	segments[count++] = { *model == DDB_SDB_48K ? (uint8_t)0xFF : (uint8_t)0,
		DDB_SDB_SEGMENT_LOW, 0x9300, (uint16_t)(firstFree - 0x9300), memory + 0x9300 };
	segments[count++] = { *model == DDB_SDB_48K ? (uint8_t)0xFF : (uint8_t)0,
		DDB_SDB_SEGMENT_HIGH, highStart, (uint16_t)(0x10000 - highStart), memory + highStart };
	if (*model == DDB_SDB_128K)
	{
		const uint8_t banks[] = { 1, 3, 4, 6, 7 };
		for (uint8_t bank : banks)
		{
			uint8_t* bankData = memory + 0x10000 + bank * 0x4000;
			if (read16LE(bankData + (0xFFFD - 0xC000)) != 0xC000 ||
				bankData[0xFFFF - 0xC000] != bank) continue;
			uint16_t pageFree = read16LE(bankData + (0xFFED - 0xC000));
			uint16_t pageHigh = read16LE(bankData + (0xFFEF - 0xC000));
			if (pageFree < 0xC000 || pageFree > pageHigh || pageHigh > 0xFFFF || count + 2 > DDB_SDB_MAX_SEGMENTS)
				return false;
			segments[count++] = { bank, DDB_SDB_SEGMENT_LOW, 0xC000,
				(uint16_t)(pageFree - 0xC000), bankData };
			segments[count++] = { bank, DDB_SDB_SEGMENT_HIGH, pageHigh,
				(uint16_t)(0x10000 - pageHigh), bankData + (pageHigh - 0xC000) };
		}
	}
	*segmentCount = count;
	return true;
}

static PAWSExtractResult ExtractPAWSMedia(const char* inputFileName, const char* outputName)
{
	bool tape = HasExtension(inputFileName, ".tap") || HasExtension(inputFileName, ".tzx");
	bool snapshot = HasExtension(inputFileName, ".sna") || HasExtension(inputFileName, ".z80");
	if (!tape && !snapshot) return PAWSExtract_NotHandled;

	if (tape)
	{
		File* file = File_Open(inputFileName, ReadOnly);
		if (file == 0) return PAWSExtract_Error;
		size_t size = (size_t)File_GetSize(file);
		uint8_t* data = Allocate<uint8_t>("PAWS tape", size);
		bool read = data != 0 && File_Read(file, data, size) == size;
		File_Close(file);
		if (!read) { if (data) Free(data); return PAWSExtract_Error; }
		PAWSTapeComponent components[64];
		int componentCount = 0;
		bool parsed = HasExtension(inputFileName, ".tap") ?
			CollectTAPPayloads(data, size, components, &componentCount) :
			CollectTZXPayloads(data, size, components, &componentCount);
		if (!parsed || componentCount == 0) { Free(data); return PAWSExtract_NotHandled; }
		PAWSCandidate candidates[16] = {};
		int candidateCount = BuildTapeCandidates(components, componentCount, candidates, 16);
		int validCandidateCount = 0;
		for (int c = 0; c < candidateCount; c++)
		{
			DDB_SDBSegment validationSegments[DDB_SDB_MAX_SEGMENTS];
			uint8_t validationCount = 0, validationModel = 0;
			if (CandidateSegments(&candidates[c], validationSegments, &validationCount, &validationModel))
				validCandidateCount++;
		}
		if (validCandidateCount == 0)
		{
			Free(data);
			return PAWSExtract_NotHandled;
		}
		char directory[FILE_MAX_PATH];
		bool outputIsFile = outputName != 0 && HasExtension(outputName, ".sdb");
		if (outputIsFile && validCandidateCount > 1)
		{
			fprintf(stderr, "Error: Multiple PAWS databases require an output directory\n");
			Free(data);
			return PAWSExtract_Error;
		}
		if (outputName && !outputIsFile)
		{
			StrCopy(directory, sizeof(directory), outputName);
			size_t length = StrLen(directory);
			if (length > 0 && directory[length - 1] != '/' && directory[length - 1] != '\\')
				StrCopy(directory + length, sizeof(directory) - length, "/");
		}
		else
			InputDirectory(inputFileName, directory, sizeof(directory));
		bool success = true;
		int written = 0;
		for (int c = 0; c < candidateCount; c++)
		{
			DDB_SDBSegment segments[DDB_SDB_MAX_SEGMENTS];
			uint8_t segmentCount = 0, model = 0;
			if (!CandidateSegments(&candidates[c], segments, &segmentCount, &model)) continue;
			char filename[FILE_MAX_PATH];
			if (outputIsFile)
				StrCopy(filename, sizeof(filename), ChangeExtension(outputName, ".sdb"));
			else
			{
				int occurrence = 1;
				for (int p = 0; p < c; p++)
					if (StrComp(candidates[p].name, candidates[c].name) == 0) occurrence++;
				if (occurrence == 1)
					snprintf(filename, sizeof(filename), "%s%s.sdb", directory, candidates[c].name);
				else
					snprintf(filename, sizeof(filename), "%s%s-%d.sdb", directory, candidates[c].name, occurrence);
			}
			if (WriteCandidateSDB(filename, model, segments, segmentCount)) written++;
			else success = false;
		}
		Free(data);
		return success && written > 0 ? PAWSExtract_Success : PAWSExtract_Error;
	}

	File* file = File_Open(inputFileName, ReadOnly);
	if (file == 0) return PAWSExtract_Error;
	uint8_t* memory = 0;
	size_t memorySize = 0;
	DDB_Machine machine;
	bool loaded = DDB_LoadSnapshot(file, inputFileName, &memory, &memorySize, &machine);
	File_Close(file);
	if (!loaded || machine != DDB_MACHINE_SPECTRUM) { if (memory) Free(memory); return PAWSExtract_NotHandled; }
	DDB_SDBSegment segments[DDB_SDB_MAX_SEGMENTS];
	uint8_t segmentCount = 0, model = 0;
	if (!SnapshotSegments(memory, memorySize, segments, &segmentCount, &model))
	{
		Free(memory);
		return PAWSExtract_NotHandled;
	}
	char filename[FILE_MAX_PATH];
	StrCopy(filename, sizeof(filename), ChangeExtension(outputName ? outputName : inputFileName, ".sdb"));
	bool success = WriteCandidateSDB(filename, model, segments, segmentCount);
	Free(memory);
	return success ? PAWSExtract_Success : PAWSExtract_Error;
}

#ifdef HAS_VIRTUALFILESYSTEM
static int MountPAWSTape(const char* inputFileName, char* firstName, size_t firstNameSize)
{
	if (!HasExtension(inputFileName, ".tap") && !HasExtension(inputFileName, ".tzx"))
		return 0;
	File* file = File_Open(inputFileName, ReadOnly);
	if (file == 0) return -1;
	size_t size = (size_t)File_GetSize(file);
	uint8_t* data = Allocate<uint8_t>("PAWS tape", size);
	bool read = data != 0 && File_Read(file, data, size) == size;
	File_Close(file);
	if (!read) { if (data) Free(data); return -1; }

	PAWSTapeComponent components[64];
	int componentCount = 0;
	bool parsed = HasExtension(inputFileName, ".tap") ?
		CollectTAPPayloads(data, size, components, &componentCount) :
		CollectTZXPayloads(data, size, components, &componentCount);
	if (!parsed || componentCount == 0) { Free(data); return 0; }
	PAWSCandidate candidates[16] = {};
	int candidateCount = BuildTapeCandidates(components, componentCount, candidates, 16);
	int mounted = 0;
	for (int c = 0; c < candidateCount; c++)
	{
		DDB_SDBSegment segments[DDB_SDB_MAX_SEGMENTS];
		uint8_t segmentCount = 0, model = 0;
		if (!CandidateSegments(&candidates[c], segments, &segmentCount, &model)) continue;
		int occurrence = 1;
		for (int p = 0; p < c; p++)
			if (StrComp(candidates[p].name, candidates[c].name) == 0) occurrence++;
		char name[FILE_MAX_PATH];
		if (occurrence == 1)
			snprintf(name, sizeof(name), "%s.sdb", candidates[c].name);
		else
			snprintf(name, sizeof(name), "%s-%d.sdb", candidates[c].name, occurrence);
		uint8_t* sdb = 0;
		size_t sdbSize = 0;
		if (!DDB_PAWSBuildSDB(model, segments, segmentCount, &sdb, &sdbSize) ||
			!File_MountMemoryFile(name, sdb, sdbSize))
		{
			if (sdb) Free(sdb);
			File_UnmountMemoryFiles();
			Free(data);
			return -1;
		}
		Free(sdb);
		if (mounted == 0 && firstName != 0)
			StrCopy(firstName, firstNameSize, name);
		mounted++;
	}
	Free(data);
	return mounted;
}
#endif

#endif

static bool SaveScreenshot(const char* fileName)
{
	return VID_SaveScreenshot(fileName);
}

static bool ParseScreenModeOption(const char* option, DDB_ScreenMode* mode)
{
	if (stricmp(option, "cga") == 0)
		*mode = ScreenMode_CGA;
	else if (stricmp(option, "ega") == 0)
		*mode = ScreenMode_EGA;
	else if (stricmp(option, "vga") == 0)
		*mode = ScreenMode_VGA16;
	else
		return false;
	return true;
}

typedef enum
{
	CLI_OPTION_TRACE = 1,
	CLI_OPTION_FORCE,
	CLI_OPTION_MEMORY_MAP,
	CLI_OPTION_TRANSCRIPT,
	CLI_OPTION_INPUT,
	CLI_OPTION_SCREEN,
	CLI_OPTION_CAPTURE,
	CLI_OPTION_INTERACTIVE,
	CLI_OPTION_SKIP_TIMED_PAUSES,
	CLI_OPTION_PART,
	CLI_OPTION_RANDOM_SEED,
	CLI_OPTION_PART_CAPTURE,
	CLI_OPTION_NO_MESSAGE_SAMPLES,
	CLI_OPTION_STRICT_PAW,
	CLI_OPTION_RAW_TOKENS,
	CLI_OPTION_HELP,
}
DDB_CLIOption;

static void IgnoreDDBWarning(const char* message)
{
	(void)message;
}

typedef struct
{
	const char* name;
	uint32_t offset;
}
DDB_MapEntry;

static uint32_t GetDDBHeaderSize(const DDB* ddb)
{
	return ddb->version == DDB_VERSION_1 ? 34 : 36;
}

static bool TryGetDDBOffset(const DDB* ddb, const void* ptr, uint32_t* offset)
{
	if (ptr == 0)
		return false;

	const uint8_t* bytes = (const uint8_t*)ptr;
	if (bytes < ddb->data || bytes > ddb->data + ddb->dataSize)
		return false;

	*offset = (uint32_t)(bytes - ddb->data);
	return true;
}

static void PrintDDBHeaderWord(const DDB* ddb, uint32_t headerOffset, const char* name, bool isPointer)
{
	if (headerOffset + 1 >= ddb->dataSize)
		return;

	uint16_t rawValue = read16(ddb->data + headerOffset, ddb->littleEndian);
	printf("  +0x%02X %-24s 0x%04X", (unsigned)headerOffset, name, rawValue);
	if (!isPointer)
	{
		printf("\n");
		return;
	}

	if (rawValue == 0)
	{
		printf(" -> null\n");
		return;
	}

	if (rawValue < ddb->baseOffset)
	{
		printf(" -> before base 0x%04X\n", ddb->baseOffset);
		return;
	}

	uint32_t fileOffset = rawValue - ddb->baseOffset;
	printf(" -> file +0x%04X", (unsigned)fileOffset);
	if (fileOffset >= ddb->dataSize)
		printf(" (outside file)");
	printf("\n");
}

static void AddDDBMapEntry(const DDB* ddb, DDB_MapEntry* entries, int* count, const char* name, const void* ptr)
{
	uint32_t offset;
	if (!TryGetDDBOffset(ddb, ptr, &offset))
		return;

	entries[*count].name = name;
	entries[*count].offset = offset;
	(*count)++;
}

static void SortDDBMapEntries(DDB_MapEntry* entries, int count)
{
	for (int i = 1; i < count; i++)
	{
		DDB_MapEntry entry = entries[i];
		int j = i - 1;
		while (j >= 0 && entries[j].offset > entry.offset)
		{
			entries[j + 1] = entries[j];
			j--;
		}
		entries[j + 1] = entry;
	}
}

static void PrintDDBMemoryMap(const DDB* ddb)
{
	const uint32_t headerSize = GetDDBHeaderSize(ddb);
	const uint32_t extensionPreviewSize = 256;
	DDB_MapEntry entries[16];
	int entryCount = 0;

	AddDDBMapEntry(ddb, entries, &entryCount, "Token block", ddb->tokens);
	AddDDBMapEntry(ddb, entries, &entryCount, "Process table", ddb->processTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Object names table", ddb->objNamTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Location descriptions table", ddb->locDescTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Messages table", ddb->msgTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "System messages table", ddb->sysMsgTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Connections table", ddb->conTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Vocabulary", ddb->vocabulary);
	AddDDBMapEntry(ddb, entries, &entryCount, "Object locations table", ddb->objLocTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Object words table", ddb->objWordsTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Object attributes table", ddb->objAttrTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Extended object attributes table", ddb->objExAttrTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "External data", ddb->externData);
	AddDDBMapEntry(ddb, entries, &entryCount, "EXTERN PSG table", ddb->externPsgTable);
	AddDDBMapEntry(ddb, entries, &entryCount, "Charsets", ddb->charsets);

	SortDDBMapEntries(entries, entryCount);

	uint32_t firstSectionOffset = ddb->dataSize;
	if (entryCount > 0)
		firstSectionOffset = entries[0].offset;

	printf("\nMemory map:\n");
	printf("  Header size: 0x%02X bytes\n", (unsigned)headerSize);
	printf("  Base offset: 0x%04X\n", ddb->baseOffset);

	printf("\nHeader fields:\n");
	PrintDDBHeaderWord(ddb, 0x08, "token block", true);
	PrintDDBHeaderWord(ddb, 0x0A, "process table", true);
	PrintDDBHeaderWord(ddb, 0x0C, "object names table", true);
	PrintDDBHeaderWord(ddb, 0x0E, "location descriptions", true);
	PrintDDBHeaderWord(ddb, 0x10, "messages table", true);
	PrintDDBHeaderWord(ddb, 0x12, "system messages", true);
	PrintDDBHeaderWord(ddb, 0x14, "connections table", true);
	PrintDDBHeaderWord(ddb, 0x16, "vocabulary", true);
	PrintDDBHeaderWord(ddb, 0x18, "object locations", true);
	PrintDDBHeaderWord(ddb, 0x1A, "object words", true);
	PrintDDBHeaderWord(ddb, 0x1C, "object attributes", true);
	if (ddb->version >= 2)
	{
		PrintDDBHeaderWord(ddb, 0x1E, "extra object attrs", true);
		PrintDDBHeaderWord(ddb, 0x20, "declared size", false);
		PrintDDBHeaderWord(ddb, 0x22, "extern data", true);
	}
	else
	{
		PrintDDBHeaderWord(ddb, 0x1E, "declared size", false);
		PrintDDBHeaderWord(ddb, 0x20, "extern data", true);
	}

	printf("\nHeader extension words:\n");
	if (firstSectionOffset <= headerSize)
	{
		printf("  (none)\n");
	}
	else
	{
		uint32_t previewEnd = headerSize + extensionPreviewSize;
		if (previewEnd > firstSectionOffset)
			previewEnd = firstSectionOffset;
		if (previewEnd > ddb->dataSize)
			previewEnd = ddb->dataSize;

		for (uint32_t offset = headerSize; offset + 1 < previewEnd; offset += 2)
		{
			uint16_t rawValue = read16(ddb->data + offset, ddb->littleEndian);
			printf("  +0x%02X extension word          0x%04X", (unsigned)offset, rawValue);
			if (rawValue != 0 && rawValue >= ddb->baseOffset)
			{
				uint32_t fileOffset = rawValue - ddb->baseOffset;
				printf(" -> file +0x%04X", (unsigned)fileOffset);
				if (fileOffset >= ddb->dataSize)
					printf(" (outside file)");
			}
			printf("\n");
		}

		if (previewEnd < firstSectionOffset)
			printf("  ... %u more bytes before the first named section\n", (unsigned)(firstSectionOffset - previewEnd));
	}

	printf("\nSections:\n");
	if (entryCount == 0)
	{
		printf("  0x0000-0x%04X (%5u bytes) Entire file\n", (unsigned)(ddb->dataSize - 1), (unsigned)ddb->dataSize);
		return;
	}

	if (headerSize > 0)
		printf("  0x0000-0x%04X (%5u bytes) Header\n", (unsigned)(headerSize - 1), (unsigned)headerSize);
	if (firstSectionOffset > headerSize)
		printf("  0x%04X-0x%04X (%5u bytes) Unnamed pre-section data\n", (unsigned)headerSize, (unsigned)(firstSectionOffset - 1), (unsigned)(firstSectionOffset - headerSize));

	for (int i = 0; i < entryCount; )
	{
		int j = i + 1;
		while (j < entryCount && entries[j].offset == entries[i].offset)
			j++;

		uint32_t start = entries[i].offset;
		uint32_t end = ddb->dataSize;
		if (j < entryCount)
			end = entries[j].offset;

		printf("  0x%04X-0x%04X (%5u bytes) %s", (unsigned)start, (unsigned)(end - 1), (unsigned)(end - start), entries[i].name);
		for (int n = i + 1; n < j; n++)
			printf(" / %s", entries[n].name);
		printf("\n");
		i = j;
	}
}

static DDB* LoadDDBFromInput(const char* inputFileName, const char** resolvedDDBFileName, bool* mountedDiskImage)
{
	*resolvedDDBFileName = inputFileName;
	*mountedDiskImage = false;

#ifdef HAS_VIRTUALFILESYSTEM
	if (File_MountDisk(inputFileName))
	{
		*mountedDiskImage = true;

		FindFileResults results;
		if (File_FindFirst("*", &results))
		{
			void (*warningHandler)(const char*) = IgnoreDDBWarning;
			DDB_SetWarningHandler(warningHandler);
			do
			{
				if (results.attributes & FileAttribute_Directory)
					continue;

				if (DDB_Check(results.fileName, 0, 0, 0))
				{
					DDB* ddb = DDB_Load(results.fileName);
					if (ddb == 0)
						continue;
					*resolvedDDBFileName = results.fileName;
					DDB_SetWarningHandler(PrintWarning);
					return ddb;
				}
			}
			while (File_FindNext(&results));
			DDB_SetWarningHandler(PrintWarning);
		}

		// No bare DDB file on the disk (e.g. a copy-protected 8-bit original
		// whose loader keeps the database embedded in a binary). Unmount and
		// carve the raw disk image for an embedded DDB.
		File_UnmountDisk();
		*mountedDiskImage = false;
		DDB* carved = DDB_Load(inputFileName);
		if (carved != 0)
		{
			*resolvedDDBFileName = inputFileName;
			return carved;
		}

		DDB_SetError(DDB_ERROR_NO_DDBS_FOUND);
		return 0;
	}
#endif

	return DDB_Load(inputFileName);
}

#if HAS_PCX
static bool LoadPCXStartupPalette(const char* fileName)
{
	if (!VID_HasExternalPictures())
		return false;

	char screenFile[FILE_MAX_PATH];
	StrCopy(screenFile, sizeof(screenFile), fileName);
	char* dot = (char*)StrRChr(screenFile, '.');
	if (dot == 0)
		dot = screenFile + StrLen(screenFile);
	StrCopy(dot, screenFile + sizeof(screenFile) - dot, ".VGA");

	File* file = File_Open(screenFile, ReadOnly);
	if (file == 0)
	{
		StrCopy(dot, screenFile + sizeof(screenFile) - dot, ".vga");
		file = File_Open(screenFile, ReadOnly);
		if (file == 0)
			return false;
	}
	File_Close(file);

	uint32_t palette[256];
	if (!DMG_ReadPCXPalette(screenFile, palette))
		return false;

	for (int n = 0; n < 256; n++)
		VID_SetPaletteColor32((uint8_t)n, palette[n]);
	VID_ActivatePalette();
	return true;
}
#endif

void TracePrintf(const char* format, ...)
{
	if (!trace)
		return;

	static char* buffer = 0;
	static size_t bufferSize = 0;

	va_list args;
	va_start(args, format);
	va_list argsCopy;
	va_copy(argsCopy, args);
	int result = vsnprintf(buffer, bufferSize, format, args);
	if (result >= bufferSize)
	{
		if (buffer != 0)
			Free(buffer);
		bufferSize = (result + 1023) & ~1023;
		buffer = Allocate<char>("Trace print buffer", bufferSize);
		if (buffer == 0)
		{
			bufferSize = 0;
			va_end(argsCopy);
			va_end(args);
			return;
		}
		result = vsnprintf(buffer, bufferSize, format, argsCopy);
	}
	va_end(argsCopy);
	va_end(args);

	fputs(buffer, stdout);
	fflush(stdout);
}

typedef enum
{
	ACTION_LIST,
	ACTION_RUN,
	ACTION_TEST,
	ACTION_EXTRACT,
	ACTION_PSG,
	ACTION_DUMP,
	ACTION_HELP
}
Action;

static Action action = ACTION_RUN;

void ShowHelp()
{
	printf("ADP DAAD Database Utility " VERSION_STR " \n\n");
	printf("Dumps, inspects or runs a game database or supported media image.\n\n");
	printf("Usage: ddb [global options] <action> <input>\n");
	printf("Usage: ddb [global options] <input>\n");
	printf("Usage: ddb [global options] extract <input> [output]\n\n");
	printf("Actions:\n\n");
	printf("    list, l      Show game information\n");
	printf("    run, r       Run the game (default action)\n");
	printf("    test, t      Run the game in test mode\n");
	printf("    extract, x   Extract game database from an input file\n");
	printf("    psg, p       Extract EXTERN PSG streams as WAV files\n");
	printf("    dump, d      Decompile/dump the database in .SCE text format\n");
	printf("    help, h      Show this help\n");
	printf("\nOptions:\n\n");
	printf("   -v, --trace             Show a trace of the program's execution\n");
    printf("   -f, --force             Force overwrite of output files\n");
    printf("   -m, --memory-map        Show a DDB memory map in list mode\n");
    printf("       --no-message-samples\n");
    printf("                           Do not show message samples beside process code in dump mode\n");
	printf("       --strict-paw       Emit classic PAWCOMP-compatible PAW source\n");
	printf("       --raw-tokens       Keep PAW dictionary references as {n} codes in dump mode\n");
    printf("   -o, --transcript FILE   Use FILE as transcript output file when running\n");
    printf("   -i, --input FILE        Use FILE as scripted input when running\n");
	printf("   -s, --screen MODE       Select screen mode for run/test: cga, ega, vga\n");
	printf("       --capture FILE      Save the final logical framebuffer when running\n");
	printf("       --interactive       Use live input after scripted test input ends\n");
	printf("       --skip-timed-pauses Skip finite PAUSE condacts when running\n");
	printf("       --random-seed N     Use N as the random number seed (default: 1)\n");
	printf("       --part N            Auto-select part N in the part selector (test mode)\n");
	printf("       --part-capture FILE Save a screenshot of the part selector (test mode)\n");
    printf("   -h, --help              Show this help\n\n");
}

static void PrintWarning(const char* message)
{
	fprintf(stderr, "WARNING: %s\n", message);
}

#if HAS_PSG

static const uint32_t TOOL_PSG_MAX_BODY_TICKS = DDB_PSG_TICK_HZ * 10;
static const uint32_t TOOL_PSG_MAX_TAIL_TICKS = DDB_PSG_TICK_HZ * 2;
static const uint32_t TOOL_PSG_FADE_TAIL_TICKS = DDB_PSG_TICK_HZ;

static bool ToolPSG_IsSilent(const uint8_t* buffer, uint32_t sampleCount)
{
	for (uint32_t n = 0; n < sampleCount; n++)
	{
		if (buffer[n] != 128)
			return false;
	}
	return true;
}

static void ToolPSG_ApplyFadeOut(uint8_t* buffer, uint32_t sampleCount)
{
	if (sampleCount == 0)
		return;

	for (uint32_t n = 0; n < sampleCount; n++)
	{
		int sample = (int)buffer[n] - 128;
		uint32_t remaining = sampleCount - 1 - n;
		buffer[n] = (uint8_t)(128 + (sample * (int)remaining) / (int)sampleCount);
	}
}

static bool ToolPSG_WriteWAV(const char* fileName, const uint8_t* samples, uint32_t sampleCount)
{
	File* file = File_Create(fileName);
	if (file == 0)
		return false;

	uint8_t header[44];
	MemClear(header, sizeof(header));
	MemCopy(header + 0, "RIFF", 4);
	write32(header + 4, 36 + sampleCount, true);
	MemCopy(header + 8, "WAVE", 4);
	MemCopy(header + 12, "fmt ", 4);
	write32(header + 16, 16, true);
	write16(header + 20, 1, true);
	write16(header + 22, 1, true);
	write32(header + 24, DDB_PSG_SAMPLE_RATE, true);
	write32(header + 28, DDB_PSG_SAMPLE_RATE, true);
	write16(header + 32, 1, true);
	write16(header + 34, 8, true);
	MemCopy(header + 36, "data", 4);
	write32(header + 40, sampleCount, true);

	bool ok = File_Write(file, header, sizeof(header)) == sizeof(header) &&
		File_Write(file, samples, sampleCount) == sampleCount;
	File_Close(file);
	return ok;
}

static void ToolPSG_BuildDefaultPrefix(const char* inputFileName, char* prefix, size_t prefixSize)
{
	StrCopy(prefix, prefixSize, inputFileName);
	char* dot = (char*)StrRChr(prefix, '.');
	char* slash = (char*)StrRChr(prefix, '/');
	char* backslash = (char*)StrRChr(prefix, '\\');
	char* separator = slash;
	if (backslash != 0 && (separator == 0 || backslash > separator))
		separator = backslash;
	if (dot != 0 && (separator == 0 || dot > separator))
		*dot = 0;
}

static bool ToolPSG_FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static bool ToolPSG_BuildOutputFileName(char* outputFileName, size_t outputFileNameSize, const char* outputPrefix, uint8_t soundIndex)
{
	int written = snprintf(outputFileName, outputFileNameSize, "%s-psg-%02u.wav", outputPrefix, soundIndex + 1);
	return written > 0 && (size_t)written < outputFileNameSize;
}

static bool ToolPSG_ExportStream(const DDB* ddb, uint8_t soundIndex, const char* outputPrefix)
{
	uint32_t streamStart = 0;
	uint32_t streamEnd = 0;
	if (!DDB_GetExternalPSGStreamRange(ddb, soundIndex, &streamStart, &streamEnd))
		return false;

	uint32_t bodyTicks = 0;
	if (!DDB_EstimatePSGStreamTicks(ddb->data + streamStart, ddb->data + streamEnd, TOOL_PSG_MAX_BODY_TICKS, &bodyTicks))
	{
		fprintf(stderr, "Error: PSG stream %u has unsupported Dosound commands\n", (unsigned)(soundIndex + 1));
		return false;
	}

	uint32_t maxTicks = bodyTicks + TOOL_PSG_MAX_TAIL_TICKS;
	uint32_t maxSamples = maxTicks * DDB_PSG_SAMPLES_PER_TICK;
	uint8_t* buffer = Allocate<uint8_t>("PSG WAV buffer", maxSamples, false);
	if (buffer == 0)
		return false;

	DDB_PSGState state;
	if (!DDB_RenderPSGStream(ddb->data + streamStart, ddb->data + streamEnd, buffer, bodyTicks, &state))
	{
		Free(buffer);
		return false;
	}

	uint32_t totalTicks = bodyTicks;
	uint32_t tailTicks = 0;
	bool tailTruncated = false;
	for (uint32_t tailTick = 0; tailTick < TOOL_PSG_MAX_TAIL_TICKS; tailTick++)
	{
		uint8_t* tailBuffer = buffer + totalTicks * DDB_PSG_SAMPLES_PER_TICK;
		DDB_RenderPSGTicks(&state, tailBuffer, 1);
		if (ToolPSG_IsSilent(tailBuffer, DDB_PSG_SAMPLES_PER_TICK))
			break;
		totalTicks++;
		tailTicks++;
		if (tailTick + 1 == TOOL_PSG_MAX_TAIL_TICKS)
			tailTruncated = true;
	}

	if (tailTicks != 0)
	{
		uint32_t fadeTicks = tailTicks;
		if (fadeTicks > TOOL_PSG_FADE_TAIL_TICKS)
			fadeTicks = TOOL_PSG_FADE_TAIL_TICKS;
		uint32_t fadeStartTick = totalTicks - fadeTicks;
		uint8_t* fadeStart = buffer + fadeStartTick * DDB_PSG_SAMPLES_PER_TICK;
		ToolPSG_ApplyFadeOut(fadeStart, fadeTicks * DDB_PSG_SAMPLES_PER_TICK);
	}

	char outputFileName[FILE_MAX_PATH];
	if (!ToolPSG_BuildOutputFileName(outputFileName, sizeof(outputFileName), outputPrefix, soundIndex))
	{
		fprintf(stderr, "Error: Output file name for PSG stream %u is too long\n", (unsigned)(soundIndex + 1));
		Free(buffer);
		return false;
	}
	if (!force && ToolPSG_FileExists(outputFileName))
	{
		fprintf(stderr, "Error: Output file '%s' already exists (use -f to force overwrite)\n", outputFileName);
		Free(buffer);
		return false;
	}

	uint32_t sampleCount = totalTicks * DDB_PSG_SAMPLES_PER_TICK;
	if (sampleCount == 0 || ToolPSG_IsSilent(buffer, sampleCount))
	{
		Free(buffer);
		printf("PSG stream %u skipped (pure silence / stop stream)\n", (unsigned)(soundIndex + 1));
		return true;
	}

	bool ok = ToolPSG_WriteWAV(outputFileName, buffer, sampleCount);
	Free(buffer);
	if (!ok)
	{
		fprintf(stderr, "Error writing '%s'\n", outputFileName);
		return false;
	}

	printf("PSG stream %u written to '%s' (%u samples at %u Hz)\n",
		(unsigned)(soundIndex + 1), outputFileName,
		(unsigned)sampleCount, (unsigned)DDB_PSG_SAMPLE_RATE);
	return true;
}

static bool ToolPSG_ExportWAVFiles(const DDB* ddb, const char* inputFileName, const char* outputPrefixArg)
{
	if (ddb->externPsgTable == 0 || ddb->externPsgCount == 0)
	{
		fprintf(stderr, "Error: DDB does not contain EXTERN PSG streams\n");
		return false;
	}

	char outputPrefix[FILE_MAX_PATH];
	if (outputPrefixArg != 0)
		StrCopy(outputPrefix, sizeof(outputPrefix), outputPrefixArg);
	else
		ToolPSG_BuildDefaultPrefix(inputFileName, outputPrefix, sizeof(outputPrefix));

	for (uint8_t soundIndex = 0; soundIndex < ddb->externPsgCount; soundIndex++)
	{
		if (!ToolPSG_ExportStream(ddb, soundIndex, outputPrefix))
			return false;
	}
	return true;
}

#endif

// Printf, but converts ISO8859-1 to UTF8
static int printf_iso88591(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	static char*  buffer = 0;
	static size_t bufferSize = 0;

	if (buffer == 0)
	{
		bufferSize = 1024;
		buffer = Allocate<char>("printf buffer", bufferSize);
	}

	int result = vsnprintf(buffer, bufferSize, format, args);
	if (result >= bufferSize)
	{
		Free(buffer);
		bufferSize = (result + 1023) & ~1023;
		buffer = Allocate<char>("printf buffer", bufferSize);
		result = vsnprintf(buffer, bufferSize, format, args);
	}
	va_end(args);

	for (const char* ptr = buffer; *ptr; ptr++)
	{
		if (*ptr < 0)
		{
			int c = *ptr & 0xFF;
			if (c < 0x80)
				printf("%c", c);
			else
				printf("%c%c", 0xC0 | (c >> 6), 0x80 | (c & 0x3F));
		}
		else
			printf("%c", *ptr);
	}
	return result;
}

int main (int argc, char *argv[])
{
	char parseError[256];
	CLI_CommandLine commandLine;
	static const CLI_ActionSpec actionSpecs[] =
	{
		{ "list", "list", ACTION_LIST },
		{ "l", "list", ACTION_LIST },
		{ "run", "run", ACTION_RUN },
		{ "r", "run", ACTION_RUN },
		{ "test", "test", ACTION_TEST },
		{ "t", "test", ACTION_TEST },
		{ "extract", "extract", ACTION_EXTRACT },
		{ "x", "extract", ACTION_EXTRACT },
		{ "psg", "psg", ACTION_PSG },
		{ "p", "psg", ACTION_PSG },
		{ "dump", "dump", ACTION_DUMP },
		{ "d", "dump", ACTION_DUMP },
		{ "help", "help", ACTION_HELP },
		{ "h", "help", ACTION_HELP },
		{ 0, 0, 0 }
	};
	static const CLI_OptionSpec optionSpecs[] =
	{
		{ 'v', "trace", CLI_OPTION_TRACE, CLI_OPTION_NONE },
		{ 'f', "force", CLI_OPTION_FORCE, CLI_OPTION_NONE },
		{ 'm', "memory-map", CLI_OPTION_MEMORY_MAP, CLI_OPTION_NONE },
		{ 'o', "transcript", CLI_OPTION_TRANSCRIPT, CLI_OPTION_REQUIRED_VALUE },
		{ 'i', "input", CLI_OPTION_INPUT, CLI_OPTION_REQUIRED_VALUE },
		{ 's', "screen", CLI_OPTION_SCREEN, CLI_OPTION_REQUIRED_VALUE },
		{ 0, "capture", CLI_OPTION_CAPTURE, CLI_OPTION_REQUIRED_VALUE },
		{ 0, "interactive", CLI_OPTION_INTERACTIVE, CLI_OPTION_NONE },
		{ 0, "skip-timed-pauses", CLI_OPTION_SKIP_TIMED_PAUSES, CLI_OPTION_NONE },
		{ 0, "part", CLI_OPTION_PART, CLI_OPTION_REQUIRED_VALUE },
		{ 0, "random-seed", CLI_OPTION_RANDOM_SEED, CLI_OPTION_REQUIRED_VALUE },
		{ 0, "part-capture", CLI_OPTION_PART_CAPTURE, CLI_OPTION_REQUIRED_VALUE },
		{ 'S', "no-message-samples", CLI_OPTION_NO_MESSAGE_SAMPLES, CLI_OPTION_NONE },
		{ 'S', "no-samples", CLI_OPTION_NO_MESSAGE_SAMPLES, CLI_OPTION_NONE },
		{ 0, "strict-paw", CLI_OPTION_STRICT_PAW, CLI_OPTION_NONE },
		{ 0, "strict-cpm", CLI_OPTION_STRICT_PAW, CLI_OPTION_NONE },
		{ 0, "raw-tokens", CLI_OPTION_RAW_TOKENS, CLI_OPTION_NONE },
		{ 'h', "help", CLI_OPTION_HELP, CLI_OPTION_NONE },
		{ 0, 0, 0, CLI_OPTION_NONE }
	};

	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	DDB_SetWarningHandler(PrintWarning);

	if (!CLI_ParseCommandLine(argc, argv, actionSpecs, ACTION_RUN, optionSpecs, &commandLine, parseError, sizeof(parseError)))
	{
		fprintf(stderr, "Error: %s\n", parseError);
		return 1;
	}

	action = (Action)commandLine.action;
	trace = CLI_HasOption(&commandLine, CLI_OPTION_TRACE);
	force = CLI_HasOption(&commandLine, CLI_OPTION_FORCE);
	showMemoryMap = CLI_HasOption(&commandLine, CLI_OPTION_MEMORY_MAP);
	dumpMessageSamples = !CLI_HasOption(&commandLine, CLI_OPTION_NO_MESSAGE_SAMPLES);
	bool strictPAWDump = CLI_HasOption(&commandLine, CLI_OPTION_STRICT_PAW);
	bool rawTokensDump = CLI_HasOption(&commandLine, CLI_OPTION_RAW_TOKENS);

	if (action == ACTION_HELP || CLI_HasOption(&commandLine, CLI_OPTION_HELP))
	{
		ShowHelp();
		return 0;
	}

	if (commandLine.argumentCount < 1)
	{
		fprintf(stderr, "Error: Missing input file\n");
		return 1;
	}

	const char* inputFileName = commandLine.arguments[0];
	char pawsSinglePart[FILE_MAX_PATH] = { 0 };
	bool mountedPAWSMemory = false;
	const char* optionalOutputFileName = commandLine.argumentCount > 1 ? commandLine.arguments[1] : 0;
	const char* transcriptFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_TRANSCRIPT);
	const char* scriptedInputFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_INPUT);
	const char* screenOption = CLI_GetOptionValue(&commandLine, CLI_OPTION_SCREEN);
	const char* captureFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_CAPTURE);
	bool interactiveTestInput = CLI_HasOption(&commandLine, CLI_OPTION_INTERACTIVE);
	bool skipTimedPauses = CLI_HasOption(&commandLine, CLI_OPTION_SKIP_TIMED_PAUSES);
	const char* randomSeedOption = CLI_GetOptionValue(&commandLine, CLI_OPTION_RANDOM_SEED);
	if (randomSeedOption != 0)
		RandSetDefaultSeed((uint32_t)atoi(randomSeedOption));
	const char* partOption = CLI_GetOptionValue(&commandLine, CLI_OPTION_PART);
	const char* partCaptureFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_PART_CAPTURE);
	int partSelection = partOption != 0 ? atoi(partOption) : 0;

	if (showMemoryMap && action != ACTION_LIST)
	{
		fprintf(stderr, "Error: --memory-map is only valid with the list action\n");
		return 1;
	}
	if (!dumpMessageSamples && action != ACTION_DUMP)
	{
		fprintf(stderr, "Error: --no-message-samples is only valid with the dump action\n");
		return 1;
	}
	if (strictPAWDump && action != ACTION_DUMP)
	{
		fprintf(stderr, "Error: --strict-paw is only valid with the dump action\n");
		return 1;
	}
	if ((transcriptFileName != 0 || scriptedInputFileName != 0 || screenOption != 0 || captureFileName != 0 || interactiveTestInput || skipTimedPauses) &&
		action != ACTION_RUN && action != ACTION_TEST)
	{
		fprintf(stderr, "Error: run-only options can only be used with run or test\n");
		return 1;
	}
	if ((partOption != 0 || partCaptureFileName != 0) && action != ACTION_TEST)
	{
		fprintf(stderr, "Error: --part and --part-capture can only be used with test\n");
		return 1;
	}
	if (partOption != 0 && partSelection < 1)
	{
		fprintf(stderr, "Error: --part requires a positive part number\n");
		return 1;
	}
	if (partCaptureFileName != 0 && partSelection < 1)
	{
		fprintf(stderr, "Error: --part-capture requires --part\n");
		return 1;
	}
	if (captureFileName != 0 && partSelection >= 1)
	{
		fprintf(stderr, "Error: --capture cannot be combined with --part\n");
		return 1;
	}

	#if HAS_PAWS
	if (action == ACTION_EXTRACT)
	{
		PAWSExtractResult pawsResult = ExtractPAWSMedia(inputFileName, optionalOutputFileName);
		if (pawsResult != PAWSExtract_NotHandled)
			return pawsResult == PAWSExtract_Success ? 0 : 1;
	}
	#endif
	DDB_ScreenMode requestedScreenMode = ScreenMode_Default;
	if (screenOption != 0 && !ParseScreenModeOption(screenOption, &requestedScreenMode))
	{
		fprintf(stderr, "Error: Unknown screen mode '%s'\n", screenOption);
		return 1;
	}

	if (transcriptFileName != 0)
		DDB_UseTranscriptFile(transcriptFileName);
	if (scriptedInputFileName != 0)
		DDB_TestLoadInput(scriptedInputFileName);
	if (action == ACTION_TEST)
		DDB_TestSetScreenshotCallback(SaveScreenshot);
	if (interactiveTestInput)
		DDB_TestEnableInteractiveInput();
	if (partSelection >= 1)
		DDB_TestSetPartSelection(partSelection, partCaptureFileName);

	#ifdef HAS_VIRTUALFILESYSTEM
	#if HAS_PAWS
	if (action == ACTION_RUN || action == ACTION_TEST)
	{
		int pawsParts = MountPAWSTape(inputFileName, pawsSinglePart, sizeof(pawsSinglePart));
		if (pawsParts < 0)
		{
			fprintf(stderr, "Error: Unable to prepare PAWS databases from '%s'\n", inputFileName);
			return 1;
		}
		if (pawsParts == 1)
		{
			// A single embedded database follows the normal direct-load path. This
			// preserves the exact run/test initialization contract; only genuinely
			// multi-part media needs the player front end and its selector.
			inputFileName = pawsSinglePart;
			mountedPAWSMemory = true;
		}
		else if (pawsParts > 1)
		{
			if (requestedScreenMode != ScreenMode_Default)
				DDB_SetStartupScreenModeOverride(requestedScreenMode);
			VID_SetFastMode(skipTimedPauses);
			bool ran = DDB_RunPlayer();
			File_UnmountMemoryFiles();
			if (!ran)
			{
				fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
				return 1;
			}
			if (DDB_TestHasError())
			{
				fprintf(stderr, "Error: %s\n", DDB_TestGetError());
				return 1;
			}
			return 0;
		}
	}
	#endif
	// A part selection needs the real player so the part selector runs; route
	// multi-part test scenarios through it just like the interactive player.
	if ((action == ACTION_RUN || (action == ACTION_TEST && partSelection >= 1)) &&
		File_MountDisk(inputFileName))
	{
		// Mount any additional disk images (e.g. a multi-disk game supplied as two
		// .st files) so the player sees every part's files at once, no disk swapping.
		for (int i = 1; i < commandLine.argumentCount; i++)
			File_MountDisk(commandLine.arguments[i]);
		// The part selector runs inside the player, so the requested graphics mode
		// (CGA/EGA/VGA) has to be applied as a startup override here rather than on
		// the direct load path below.
		if (requestedScreenMode != ScreenMode_Default)
			DDB_SetStartupScreenModeOverride(requestedScreenMode);
		VID_SetFastMode(skipTimedPauses);
		if (!DDB_RunPlayer())
		{
			fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
			return 1;
		}
		if (DDB_TestHasError())
		{
			fprintf(stderr, "Error: %s\n", DDB_TestGetError());
			return 1;
		}
		return 0;
	}
	#endif

	const char* loadedDDBFileName = inputFileName;
	bool mountedDiskImage = false;
	DDB* ddb = LoadDDBFromInput(inputFileName, &loadedDDBFileName, &mountedDiskImage);
	if (ddb == NULL)
	{
		fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		if (mountedDiskImage)
			File_UnmountDisk();
		#ifdef HAS_VIRTUALFILESYSTEM
		if (mountedPAWSMemory) File_UnmountMemoryFiles();
		#endif
		return 1;
	}

	if (action == ACTION_DUMP)
	{
		if (strictPAWDump && ddb->version != DDB_VERSION_PAWS)
		{
			fprintf(stderr, "Error: --strict-paw requires a PAW database\n");
			DDB_Close(ddb);
			if (mountedDiskImage)
				File_UnmountDisk();
			return 1;
		}
		if (rawTokensDump && ddb->version != DDB_VERSION_PAWS)
		{
			fprintf(stderr, "Error: --raw-tokens requires a PAW database\n");
			DDB_Close(ddb);
			if (mountedDiskImage)
				File_UnmountDisk();
			return 1;
		}
		DDB_DumpOptions dumpOptions;
		dumpOptions.includeMessageSamples = dumpMessageSamples;
		dumpOptions.strictPAWCompatibility = strictPAWDump;
		dumpOptions.rawTokens = rawTokensDump;
		DDB_DumpWithOptions(ddb, printf, &dumpOptions);
		DDB_Close(ddb);
		if (mountedDiskImage)
			File_UnmountDisk();
		return 0;
	}

	#if HAS_PSG
	if (action == ACTION_PSG)
	{
		if (!ToolPSG_ExportWAVFiles(ddb, loadedDDBFileName, optionalOutputFileName))
		{
			if (mountedDiskImage)
				File_UnmountDisk();
			return 1;
		}
		if (mountedDiskImage)
			File_UnmountDisk();
		return 0;
	}
	#endif

	printf("Database file loaded (%s, %s, %s, %d bytes)\n",
		loadedDDBFileName, DDB_DescribeVersion(ddb->version),
		ddb->littleEndian ? "little endian" : "big endian",
		ddb->dataSize);

	if (action == ACTION_LIST)
	{
		printf("Platform: %s\n", DDB_DescribeMachine(ddb->target));
		printf("Language: %s\n", DDB_DescribeLanguage(ddb->language));
		printf("\n%5d objects\n", ddb->numObjects);
		printf("%5d locations\n", ddb->numLocations);
		printf("%5d messages\n", ddb->numMessages);
		printf("%5d system messages\n", ddb->numSystemMessages);
		printf("%5d processes\n", ddb->numProcesses);
		if (ddb->objExAttrTable)
			printf("    - Includes extra object attributes\n");
		if (ddb->hasTokens && ddb->tokenBlockSize > 1)
			printf("    - Text is compressed (%d bytes in tokens)\n", (int)ddb->tokenBlockSize);
		if (ddb->externPsgTable && ddb->externPsgCount != 0)
			printf("    - Includes %d EXTERN PSG streams\n", ddb->externPsgCount);
		if (ddb->sdbMemoryModel != 0)
			printf("    - SDB: %dK Spectrum, %d sparse segments\n",
				ddb->sdbMemoryModel, ddb->sdbSegmentCount);
		DDB_DumpMetrics(ddb, printf);
		if (showMemoryMap)
			PrintDDBMemoryMap(ddb);
		DDB_Close(ddb);
		if (mountedDiskImage)
			File_UnmountDisk();
		return 0;
	}

	if (action == ACTION_EXTRACT)
	{
		char outputBase[FILE_MAX_PATH];
		StrCopy(outputBase, sizeof(outputBase),
			optionalOutputFileName != 0 ? optionalOutputFileName : inputFileName);
		char* baseDot = (char*)StrRChr(outputBase, '.');
		if (baseDot != 0 && baseDot > (char*)StrRChr(outputBase, '/'))
			*baseDot = 0;

		// Graphics companions shipped next to a database on a disk: the DMG
		// variants for the 16-bit targets plus the 8-bit vector databases
		static const char* companionExtensions[] =
		{
			".dat", ".DAT", ".ega", ".EGA", ".cga", ".CGA", ".vga", ".VGA",
			".sga", ".SGA", ".sdg", ".SDG", ".cdg", ".CDG", ".adg", ".ADG",
			".mdg", ".MDG", 0
		};

		int failures = 0;
		int written = 0;

		// Collect every database in a mounted disk image so a multi-part game
		// extracts as GAMENAME1.*, GAMENAME2.*... (a single part keeps the
		// plain GAMENAME.* name)
		char partNames[8][FILE_MAX_PATH];
		int partCount = 0;
		if (mountedDiskImage)
		{
			FindFileResults results;
			if (File_FindFirst("*", &results))
			{
				DDB_SetWarningHandler(IgnoreDDBWarning);
				do
				{
					if (results.attributes & FileAttribute_Directory)
						continue;
					if (partCount < 8 && DDB_Check(results.fileName, 0, 0, 0))
						StrCopy(partNames[partCount++], FILE_MAX_PATH, results.fileName);
				}
				while (File_FindNext(&results));
				DDB_SetWarningHandler(PrintWarning);
			}
		}
		if (partCount == 0)
		{
			// Carved container, snapshot or bare DDB: single database, no
			// on-disk companions to copy
			StrCopy(partNames[0], FILE_MAX_PATH, loadedDDBFileName);
			partCount = 1;
		}

		for (int part = 0; part < partCount; part++)
		{
			DDB* partDDB = ddb;
			if (StrComp(partNames[part], loadedDDBFileName) != 0)
			{
				partDDB = DDB_Load(partNames[part]);
				if (partDDB == 0)
				{
					// A file that passed the cheap DDB_Check but fails a full
					// load is a false candidate (e.g. a .SCR whose first bytes
					// resemble a header), not an extraction failure
					fprintf(stderr, "Skipping %s: %s\n", partNames[part], DDB_GetErrorString());
					continue;
				}
			}

			char outputFileName[FILE_MAX_PATH];
			if (partCount > 1)
				snprintf(outputFileName, sizeof(outputFileName), "%s%d.ddb", outputBase, part + 1);
			else
				snprintf(outputFileName, sizeof(outputFileName), "%s.ddb", outputBase);

			bool skip = false;
			if (!force)
			{
				File* outputFile = File_Open(outputFileName, ReadOnly);
				if (outputFile)
				{
					File_Close(outputFile);
					fprintf(stderr, "Error: Output file '%s' already exists (use -f to force overwrite)\n", outputFileName);
					failures++;
					skip = true;
				}
			}
			if (!skip)
			{
				if (!DDB_Write(partDDB, outputFileName))
				{
					fprintf(stderr, "Writing %s: Error: %s\n", outputFileName, DDB_GetErrorString());
					failures++;
				}
				else
				{
					printf("DDB file written to '%s'\n", outputFileName);
					written++;
				}
			}

			#if HAS_DRAWSTRING
			// A vector database found alongside (or inside) this part: write
			// it with the platform's extension
			if (!skip && partDDB->version != DDB_VERSION_PAWS &&
				partDDB->drawString && DDB_HasVectorDatabase())
			{
				const char* extension = ".bin";
				switch (partDDB->target)
				{
					case DDB_MACHINE_SPECTRUM: extension = ".sdg"; break;
					case DDB_MACHINE_C64:      extension = ".cdg"; break;
					case DDB_MACHINE_CPC:      extension = ".adg"; break;
					case DDB_MACHINE_MSX:      extension = ".mdg"; break;
					default: break;
				}
				char vectorFileName[FILE_MAX_PATH];
				StrCopy(vectorFileName, sizeof(vectorFileName), ChangeExtension(outputFileName, extension));
				if (!DDB_WriteVectorDatabase(vectorFileName))
				{
					fprintf(stderr, "Writing %s: Error: %s\n", vectorFileName, DDB_GetErrorString());
					failures++;
				}
				else
					printf("Vector database written to '%s'\n", vectorFileName);
			}
			#endif

			// Copy graphics companion files shipped next to the database in
			// the disk image (e.g. PART1.DAT for the 16-bit targets)
			if (!skip && mountedDiskImage)
			{
				char writtenExtensions[8][8];
				int writtenCount = 0;
				for (const char** ext = companionExtensions; *ext != 0; ext++)
				{
					char companionName[FILE_MAX_PATH];
					StrCopy(companionName, sizeof(companionName), ChangeExtension(partNames[part], *ext));
					File* companion = File_Open(companionName, ReadOnly);
					if (companion == 0)
						continue;
					uint64_t size = File_GetSize(companion);
					uint8_t* buffer = Allocate<uint8_t>("Companion file", (size_t)size);
					if (buffer == 0 || File_Read(companion, buffer, size) != size)
					{
						fprintf(stderr, "Reading %s: Error\n", companionName);
						File_Close(companion);
						if (buffer) Free(buffer);
						failures++;
						continue;
					}
					File_Close(companion);

					char lowered[8];
					int li = 0;
					for (const char* c = *ext; *c != 0 && li < 7; c++)
						lowered[li++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c;
					lowered[li] = 0;
					// A case-insensitive filesystem matches both ".dat" and
					// ".DAT": copy each companion once
					bool alreadyWritten = false;
					for (int w = 0; w < writtenCount; w++)
						if (StrComp(writtenExtensions[w], lowered) == 0)
							alreadyWritten = true;
					if (alreadyWritten)
					{
						Free(buffer);
						continue;
					}
					if (writtenCount < 8)
						StrCopy(writtenExtensions[writtenCount++], 8, lowered);
					char companionOutput[FILE_MAX_PATH];
					StrCopy(companionOutput, sizeof(companionOutput), ChangeExtension(outputFileName, lowered));
					File* output = File_Create(companionOutput);
					if (output == 0 || File_Write(output, buffer, size) != size)
					{
						fprintf(stderr, "Writing %s: Error\n", companionOutput);
						failures++;
					}
					else
						printf("Graphics data written to '%s'\n", companionOutput);
					if (output) File_Close(output);
					Free(buffer);
					// Only one companion per case family is expected; keep
					// scanning so mixed-mode PC releases copy all DMG variants
				}
			}

			if (partDDB != ddb)
				DDB_Close(partDDB);
		}

		if (mountedDiskImage)
			File_UnmountDisk();
		return failures > 0 || written == 0 ? 1 : 0;
	}

	DDB_ScreenMode screenMode = DDB_GetDefaultScreenMode(ddb->target);
	uint8_t displayPlanes = 4;
	DDB_ScreenMode requestedDataMode = requestedScreenMode;

	DDB_ScreenMode resolvedDataMode = requestedDataMode;
	if (DDB_CheckDataFileConfig(loadedDDBFileName, ddb->target, &resolvedDataMode, &displayPlanes))
	{
		if (resolvedDataMode != ScreenMode_Default)
			screenMode = resolvedDataMode;
		else if (requestedDataMode != ScreenMode_Default)
			screenMode = requestedDataMode;
	}
	else if (requestedDataMode != ScreenMode_Default)
	{
		screenMode = requestedDataMode;
	}
	VID_SetDisplayPlanesHint(displayPlanes);
	VID_Initialize(ddb->target, ddb->version, screenMode);
	if (DDB_SupportsDataFile(ddb->version, ddb->target))
		VID_LoadDataFile(loadedDDBFileName);
	#if HAS_PCX
	if (VID_HasExternalPictures())
	{
		screenMode = ScreenMode_VGA;
		LoadPCXStartupPalette(loadedDDBFileName);
	}
	#endif

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == NULL)
	{
		fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		#ifdef HAS_VIRTUALFILESYSTEM
		if (mountedPAWSMemory) File_UnmountMemoryFiles();
		#endif
		return 1;
	}

	DDB_Reset(interpreter);
	DDB_ResetWindows(interpreter);
	VID_SetFastMode(skipTimedPauses);
	DDB_SetSkipTimedPauses(interpreter, skipTimedPauses);
	if (action != ACTION_TEST)
	{
		uint32_t seed = 0;
		SCR_GetMilliseconds(&seed);
		RandSeed(seed);
	}
	DDB_Run(interpreter);
	if (DDB_TestHasError())
	{
		fprintf(stderr, "Error: %s\n", DDB_TestGetError());
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		if (mountedDiskImage)
			File_UnmountDisk();
		#ifdef HAS_VIRTUALFILESYSTEM
		if (mountedPAWSMemory) File_UnmountMemoryFiles();
		#endif
		VID_Finish();
		return 1;
	}
	if (captureFileName != 0 && !VID_SaveScreenshot(captureFileName))
	{
		fprintf(stderr, "Error: unable to save capture '%s'\n", captureFileName);
		DDB_CloseInterpreter(interpreter);
		DDB_Close(ddb);
		if (mountedDiskImage)
			File_UnmountDisk();
		#ifdef HAS_VIRTUALFILESYSTEM
		if (mountedPAWSMemory) File_UnmountMemoryFiles();
		#endif
		VID_Finish();
		return 1;
	}
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	if (mountedDiskImage)
		File_UnmountDisk();
	#ifdef HAS_VIRTUALFILESYSTEM
	if (mountedPAWSMemory) File_UnmountMemoryFiles();
	#endif
	VID_Finish();

	return 0;
}
