#include <ddb.h>
#include <ddb_vid.h>
#include <ddb_scr.h>
#include <dmg.h>
#include <cli_parser.h>
#include <os_mem.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_bito.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static bool trace = false;
static bool force = false;
static bool showMemoryMap = false;

static void PrintWarning(const char* message);

typedef enum
{
	CLI_OPTION_TRACE = 1,
	CLI_OPTION_FORCE,
	CLI_OPTION_MEMORY_MAP,
	CLI_OPTION_TRANSCRIPT,
	CLI_OPTION_INPUT,
	CLI_OPTION_SCREEN,
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
	printf("Dumps, inspects or runs a game from a DDB file or a disk image.\n\n");
	printf("Usage: ddb [global options] <action> <input.ddb>\n");
	printf("Usage: ddb [global options] <input.ddb>\n");
	printf("Usage: ddb [global options] extract <input> [output.ddb]\n\n");
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
    printf("   -o, --transcript FILE   Use FILE as transcript output file when running\n");
    printf("   -i, --input FILE        Use FILE as scripted input when running\n");
    printf("   -s, --screen MODE       Select screen mode for run/test: cga, ega, vga\n");
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
	const char* optionalOutputFileName = commandLine.argumentCount > 1 ? commandLine.arguments[1] : 0;
	const char* transcriptFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_TRANSCRIPT);
	const char* scriptedInputFileName = CLI_GetOptionValue(&commandLine, CLI_OPTION_INPUT);
	const char* screenOption = CLI_GetOptionValue(&commandLine, CLI_OPTION_SCREEN);

	if (showMemoryMap && action != ACTION_LIST)
	{
		fprintf(stderr, "Error: --memory-map is only valid with the list action\n");
		return 1;
	}
	if ((transcriptFileName != 0 || scriptedInputFileName != 0 || screenOption != 0) &&
		action != ACTION_RUN && action != ACTION_TEST)
	{
		fprintf(stderr, "Error: run-only options can only be used with run or test\n");
		return 1;
	}

	if (transcriptFileName != 0)
		DDB_UseTranscriptFile(transcriptFileName);
	if (scriptedInputFileName != 0)
		SCR_UseInputFile(scriptedInputFileName);

	#ifdef HAS_VIRTUALFILESYSTEM
	if ((action == ACTION_RUN || action == ACTION_TEST) && File_MountDisk(inputFileName))
	{
		if (!DDB_RunPlayer())
		{
			fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
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
		return 1;
	}

	if (action == ACTION_DUMP)
	{
		printf("%c%c%c", 0xEF, 0xBB, 0xBF);		// UTF-8 BOM
		DDB_Dump(ddb, printf_iso88591);
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

	printf("DDB file loaded (%s, %s, %s, %d bytes)\n",
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
		const char* outputFileName = optionalOutputFileName != 0 ? optionalOutputFileName : ChangeExtension(inputFileName, ".ddb");
		if (!force)
		{
			File* outputFile = File_Open(outputFileName, ReadOnly);
			if (outputFile)
			{
				File_Close(outputFile);
				fprintf(stderr, "Error: Output file '%s' already exists (use -f to force overwrite)\n", outputFileName);
				return 1;
			}
		}
		if (!DDB_Write(ddb, outputFileName))
		{
			fprintf(stderr, "Writing %s: Error: %s\n", outputFileName, DDB_GetErrorString());
			if (mountedDiskImage)
				File_UnmountDisk();
			return 1;
		}
		printf("DDB file written to '%s'\n", outputFileName);

		#if HAS_DRAWSTRING
		if (DDB_HasVectorDatabase())
		{
			const char* extension = ".bin";
			switch (ddb->target)
			{
				case DDB_MACHINE_SPECTRUM: extension = ".sdg"; break;
				case DDB_MACHINE_C64:      extension = ".cdg"; break;
				case DDB_MACHINE_CPC:      extension = ".adg"; break;
				case DDB_MACHINE_MSX:      extension = ".mdg"; break;
				default: break;
			}
			outputFileName = ChangeExtension(inputFileName, extension);
			if (!DDB_WriteVectorDatabase(outputFileName))
			{
				fprintf(stderr, "Writing %s: Error: %s\n", outputFileName, DDB_GetErrorString());
				if (mountedDiskImage)
					File_UnmountDisk();
				return 1;
			}
			printf("Vector database written to '%s'\n", outputFileName);
		}
		#endif
		if (mountedDiskImage)
			File_UnmountDisk();
		return 0;
	}

	DDB_ScreenMode screenMode = DDB_GetDefaultScreenMode(ddb->target);
	if (ddb->target == DDB_MACHINE_IBMPC)
		DDB_CheckVideoMode(inputFileName, &screenMode);

	if (screenOption != 0)
	{
		if (stricmp(screenOption, "ega") == 0)
			screenMode = ScreenMode_EGA;
		else if (stricmp(screenOption, "cga") == 0)
			screenMode = ScreenMode_CGA;
		else if (stricmp(screenOption, "vga") == 0)
			screenMode = ScreenMode_VGA16;
		else
		{
			fprintf(stderr, "Error: Unknown screen mode '%s'\n", screenOption);
			DDB_Close(ddb);
			if (mountedDiskImage)
				File_UnmountDisk();
			return 1;
		}
	}

	VID_Initialize(ddb->target, ddb->version, screenMode);
	if (DDB_SupportsDataFile(ddb->version, ddb->target))
		VID_LoadDataFile(inputFileName);
	#if HAS_PCX
	if (VID_HasExternalPictures())
	{
		screenMode = ScreenMode_VGA;
		LoadPCXStartupPalette(inputFileName);
	}
	#endif

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == NULL)
	{
		fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		return 1;
	}

	DDB_Reset(interpreter);
	DDB_ResetWindows(interpreter);
	DDB_Run(interpreter);
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	if (mountedDiskImage)
		File_UnmountDisk();
	VID_Finish();

	return 0;
}
