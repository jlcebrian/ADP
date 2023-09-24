#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>

#ifdef _WIN32
#include <SDL.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static bool trace = false;

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
	ACTION_OPTIMIZE,
	ACTION_DUMP
}
Action;

static Action action = ACTION_RUN;

void ShowHelp()
{
	printf("DAAD Database Utility " VERSION_STR " \n\n");
	printf("Dumps, inspects or runs a game from a DDB file.\n\n");
	printf("Usage: ddb [action] [options] <input.ddb>\n\n");
	printf("Actions:\n\n");
	printf("    l     Show game information\n");
	printf("    r     Runs the game (default action)\n");
	// printf("    t     Runs the game in test/debug mode\n");
	// printf("    o     Optimizes the database (text compression)\n");
	printf("    d     Decompiles/dumps the database in .SCE text format\n");
	printf("\nOptions:\n\n");
	printf("   -v     Show a trace of the program's execution\n\n");
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
	static char*  buffer = NULL;
	static size_t bufferSize = 0;

	if (buffer == NULL)
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
			case 'o':	action = ACTION_OPTIMIZE; break;
			case 'd':	action = ACTION_DUMP; break;
			case '-':
				actionFound = false;
				if (argv[1][1] == 'v')
					trace = true;
				break;
			default:
				ShowHelp();
				return 0;
		}
		argv++;
		argc--;
	}

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

	printf("DDB file loaded (%s, version %d, %s, %d bytes)\n", 
		argv[1], ddb->version, 
		ddb->littleEndian ? "little endian" : "big endian", 
		ddb->dataSize);

	if (action == ACTION_LIST)
	{
		printf("\n%5d objects\n", ddb->numObjects);
		printf("%5d locations\n", ddb->numLocations);
		printf("%5d messages\n", ddb->numMessages);
		printf("%5d system messages\n", ddb->numSystemMessages);
		printf("%5d processes\n", ddb->numProcesses);
		if (ddb->objExAttrTable)
			printf("    - Includes extra object attributes\n");
		if (ddb->hasTokens)
			printf("    - Text is compressed (%d bytes in tokens)\n", (int)ddb->tokenBlockSize);
		if (ddb->oldMainLoop)
			printf("    - Uses PAWS style main loop (PRO 0 is a responses table)\n");	
		DDB_DumpMetrics(ddb, printf);
		return 0;
	}

	VID_Initialize();
	VID_LoadDataFile(argv[1]);

	DDB_Interpreter* interpreter = DDB_CreateInterpreter(ddb);
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