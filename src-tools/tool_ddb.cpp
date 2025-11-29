#include <ddb.h>
#include <ddb_vid.h>
#include <ddb_scr.h>
#include <os_mem.h>
#include <os_file.h>

#ifdef _WIN32
#include <SDL.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static bool trace = false;
static bool force = false;

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

	VID_Initialize(ddb->target, ddb->version, ScreenMode_VGA16);
	if (DDB_SupportsDataFile(ddb->version, ddb->target))
		VID_LoadDataFile(argv[1]);

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb, ScreenMode_VGA16);
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
