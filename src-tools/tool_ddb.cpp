#include <ddb.h>
#include <ddb_vid.h>
#include <ddb_scr.h>
#include <dmg.h>
#include <os_mem.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_bito.h>

#ifdef _WIN32
#include <SDL.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static bool trace = false;
static bool force = false;
static bool showMemoryMap = false;

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
	const uint32_t extensionPreviewSize = 64;
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
	static size_t bufferSize;

	va_list args;
	va_start(args, format);
	int result = vsnprintf(buffer, bufferSize, format, args);
	if (result >= bufferSize)
	{
		Free(buffer);
		bufferSize = (result + 1023) & ~1023;
		buffer = Allocate<char>("Trace print buffer", bufferSize);
		if (buffer == 0)
		{
			bufferSize = 0;
			return;
		}
		result = vsnprintf(buffer, bufferSize, format, args);
	}
	va_end(args);

	fputs(buffer, stdout);
}

typedef enum
{
	ACTION_LIST,
	ACTION_RUN,
	ACTION_TEST,
	ACTION_EXTRACT,
	ACTION_DUMP
}
Action;

static Action action = ACTION_RUN;

void ShowHelp()
{
	printf("ADP DAAD Database Utility " VERSION_STR " \n\n");
	printf("Dumps, inspects or runs a game from a DDB file or a disk image.\n\n");
	printf("Usage: ddb [action] [options] <input.ddb>\n");
	printf("Usage: ddb [action] [options] <input.adf/.st/.dsk> [output.ddb]\n\n");
	printf("Actions:\n\n");
	printf("    l         Show game information\n");
	printf("    r         Runs the game (default action)\n");
	printf("    x         Extracts game database (useful for images & snapshots)\n");
	printf("    d         Decompiles/dumps the database in .SCE text format\n");
	printf("\nOptions:\n\n");
	printf("   -v         Show a trace of the program's execution\n");
    printf("   -f         Force overwrite of output file in extract commands\n");
	printf("   -m         Show a DDB memory map in list mode\n");
    printf("   -o n.txt   Use n.txt as transcript output file for the game\n");
    printf("   -i n.txt   Use n.txt as input file for the game\n\n");
}

static void PrintWarning(const char* message)
{
	fprintf(stderr, "WARNING: %s\n", message);
}

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
	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	DDB_SetWarningHandler(PrintWarning);

	bool actionFound = false;
	while (argc > 2 && (argv[1][0] == '-' || !actionFound))
	{
		actionFound = true;
		switch (argv[1][0])
		{
			case 'l':	action = ACTION_LIST; break;
			case 'r':	action = ACTION_RUN; break;
			case 't':	action = ACTION_TEST; break;
			case 'x':	action = ACTION_EXTRACT; break;
			case 'd':	action = ACTION_DUMP; break;
			case '-':
				actionFound = false;
                for (int n = 1; argv[1][n]; n++)
                {
    				if (argv[1][n] == 'v')
    					trace = true;
    				if (argv[1][n] == 'f')
    					force = true;
					if (argv[1][n] == 'm')
						showMemoryMap = true;
                    if (argv[1][n] == 'o')
                    {
                        if (argv[1][n+1])
                        {
                            DDB_UseTranscriptFile(argv[1]+n+1);
                            break;
                        }
                        else
                        {
                            if (argc < 3) {
                                fprintf(stderr, "Error: No output file specified\n");
                                return 1;
                            }
                            DDB_UseTranscriptFile(argv[2]);
                            argv++, argc--;
                            break;
                        }
                    }
                    if (argv[1][n] == 'i')
                    {
                        if (argv[1][n+1])
                        {
                            SCR_UseInputFile(argv[1]+n+1);
                            break;
                        }
                        else
                        {
                            if (argc < 3) {
                                fprintf(stderr, "Error: No input file specified\n");
                                return 1;
                            }
                            SCR_UseInputFile(argv[2]);
                            argv++, argc--;
                            break;
                        }
                    }
                }
				break;
			default:
				ShowHelp();
				return 0;
		}
		argv++;
		argc--;
	}

	#ifdef HAS_VIRTUALFILESYSTEM
	if (File_MountDisk(argv[1]))
	{
		if (!DDB_RunPlayer())
			fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		return 0;
	}
	#endif

	DDB* ddb = DDB_Load(argv[1]);
	if (ddb == NULL)
	{
		fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		return 1;
	}

	if (action == ACTION_DUMP)
	{
		printf("%c%c%c", 0xEF, 0xBB, 0xBF);		// UTF-8 BOM
		DDB_Dump(ddb, printf_iso88591);
		return 0;
	}

	printf("DDB file loaded (%s, %s, %s, %d bytes)\n",
		argv[1], DDB_DescribeVersion(ddb->version),
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
		DDB_DumpMetrics(ddb, printf);
		if (showMemoryMap)
			PrintDDBMemoryMap(ddb);
		return 0;
	}

	if (action == ACTION_EXTRACT)
	{
		argv++, argc--;
		if (argc < 2)
		{
			fprintf(stderr, "Error: No output file specified\n");
			return 1;
		}

		const char* outputFileName = ChangeExtension(argv[1], ".ddb");
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
			outputFileName = ChangeExtension(argv[1], extension);
			if (!DDB_WriteVectorDatabase(outputFileName))
			{
				fprintf(stderr, "Writing %s: Error: %s\n", outputFileName, DDB_GetErrorString());
				return 1;
			}
			printf("Vector database written to '%s'\n", outputFileName);
		}
		#endif

		return 0;
	}

	DDB_ScreenMode screenMode = ScreenMode_VGA16;
	if (ddb->target == DDB_MACHINE_IBMPC)
		DDB_CheckVideoMode(argv[1], &screenMode);

	VID_Initialize(ddb->target, ddb->version, screenMode);
	if (DDB_SupportsDataFile(ddb->version, ddb->target))
		VID_LoadDataFile(argv[1]);
	#if HAS_PCX
	if (VID_HasExternalPictures())
	{
		screenMode = ScreenMode_VGA;
		LoadPCXStartupPalette(argv[1]);
	}
	#endif

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, screenMode);
	if (interpreter == NULL)
	{
		fprintf(stderr, "Error: %s\n", DDB_GetErrorString());
		return 1;
	}

	argv++, argc--;
	while (argc > 1)
	{
		switch (argv[1][0])
		{
			case 'e':
			case 'E':
				interpreter->screenMode = ScreenMode_EGA;
				break;
			case 'c':
			case 'C':
				interpreter->screenMode = ScreenMode_CGA;
				break;
			case 'v':
			case 'V':
				interpreter->screenMode = ScreenMode_VGA16;
				break;
			default:
				fprintf(stderr, "Error: Unknown option '%s'\n", argv[1]);
				break;
		}
		argc--, argv++;
	}

	DDB_Reset(interpreter);
	DDB_ResetWindows(interpreter);
	DDB_Run(interpreter);
	DDB_CloseInterpreter(interpreter);
	DDB_Close(ddb);
	VID_Finish();

	return 1;
}
