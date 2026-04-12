#include <bas_cpc.h>
#include <bas_zx.h>
#include <cli_parser.h>
#include <dim.h>
#include <dim_cpc.h>
#include <os_file.h>
#include <os_mem.h>
#include <session_commands.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define INTERACTIVE_MAX_ARGS 64

#ifndef VERSION
#error Version not defined
#endif
#define __xstr(a) __str(a)
#define __str(a) #a
#define VERSION_STR __xstr(VERSION)

const char* diskFileName = NULL;
DIM_Disk* disk = NULL;
int options;

static const uint8_t DefaultAmigaBootBlockPrefix[] =
{
	0x44,0x4f,0x53,0x00,0xe3,0x3d,0x0e,0x73,0x00,0x00,0x03,0x70,0x43,0xfa,0x00,0x3e,
	0x70,0x25,0x4e,0xae,0xfd,0xd8,0x4a,0x80,0x67,0x0c,0x22,0x40,0x08,0xe9,0x00,0x06,
	0x00,0x22,0x4e,0xae,0xfe,0x62,0x43,0xfa,0x00,0x18,0x4e,0xae,0xff,0xa0,0x4a,0x80,
	0x67,0x0a,0x20,0x40,0x20,0x68,0x00,0x16,0x70,0x00,0x4e,0x75,0x70,0xff,0x4e,0x75,
	0x64,0x6f,0x73,0x2e,0x6c,0x69,0x62,0x72,0x61,0x72,0x79,0x00,0x65,0x78,0x70,0x61,
	0x6e,0x73,0x69,0x6f,0x6e,0x2e,0x6c,0x69,0x62,0x72,0x61,0x72,0x79,0x00
};

typedef enum
{
	ACTION_HELP,
	ACTION_INFO,
	ACTION_LIST,
	ACTION_CREATEDISK,
	ACTION_ADD,
	ACTION_DELETE,
	ACTION_EXTRACT,
	ACTION_CHDIR,
	ACTION_MKDIR,
	ACTION_RMDIR,
	ACTION_CAT,
	ACTION_HEXCAT,
	ACTION_SETVOL,
	ACTION_SETBOOT,
	ACTION_GETBOOT,
	ACTION_ADDTREE,
	ACTION_INTERACTIVE,
}
Action;

typedef enum
{
	DIR_BRIEF     = 0x01,
	DIR_LOWERCASE = 0x02,
}
DirOptions;

enum
{
	TEXTOPT_BASIC = 0x01,
};

typedef enum
{
	DSK_OPTION_BRIEF = 1,
	DSK_OPTION_LOWERCASE,
	DSK_OPTION_ASCII,
	DSK_OPTION_HELP,
}
DSK_CLIOption;

typedef enum
{
	MODE_NO_DISK,
	MODE_EXISTING_DISK,
	MODE_NEW_DISK,
}
ActionMode;

struct
{
	const char*  command;
	Action       action;
	ActionMode   mode;
	const char*  options;
	int          defaultOptions;
}
commands[] =
{
	{ "h",        ACTION_HELP },
	{ "help",     ACTION_HELP },
	{ "l",        ACTION_LIST,       MODE_EXISTING_DISK, "BL" },
	{ "list",     ACTION_LIST,       MODE_EXISTING_DISK, "BL" },
	{ "ls",       ACTION_LIST,       MODE_EXISTING_DISK, "BL", DIR_BRIEF },
	{ "dir",      ACTION_LIST,       MODE_EXISTING_DISK, "BL" },
	{ "i",        ACTION_INFO,       MODE_EXISTING_DISK },
	{ "info",     ACTION_INFO,       MODE_EXISTING_DISK },
	{ "c",        ACTION_CREATEDISK, MODE_NEW_DISK      },
	{ "create",   ACTION_CREATEDISK, MODE_NEW_DISK      },
	{ "mkdisk",   ACTION_CREATEDISK, MODE_NEW_DISK      },
	{ "a",        ACTION_ADD,        MODE_EXISTING_DISK },
	{ "add",      ACTION_ADD,        MODE_EXISTING_DISK },
	{ "d",        ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "delete",   ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "rm",       ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "del",      ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "x",        ACTION_EXTRACT,    MODE_EXISTING_DISK, "A" },
	{ "extract",  ACTION_EXTRACT,    MODE_EXISTING_DISK, "A" },
	{ "cd",       ACTION_CHDIR,      MODE_EXISTING_DISK },
	{ "chdir",    ACTION_CHDIR,      MODE_EXISTING_DISK },
	{ "md",       ACTION_MKDIR,      MODE_EXISTING_DISK },
	{ "mkdir",    ACTION_MKDIR,      MODE_EXISTING_DISK },
	{ "rd",       ACTION_RMDIR,      MODE_EXISTING_DISK },
	{ "rmdir",    ACTION_RMDIR,      MODE_EXISTING_DISK },
	{ "cat",      ACTION_CAT,        MODE_EXISTING_DISK, "A" },
	{ "type",     ACTION_CAT,        MODE_EXISTING_DISK, "A" },
	{ "hex",      ACTION_HEXCAT,     MODE_EXISTING_DISK },
	{ "dump",     ACTION_HEXCAT,     MODE_EXISTING_DISK },
	{ "setvol",   ACTION_SETVOL,     MODE_EXISTING_DISK },
	{ "label",    ACTION_SETVOL,     MODE_EXISTING_DISK },
	{ "boot",     ACTION_SETBOOT,    MODE_EXISTING_DISK },
	{ "setboot",  ACTION_SETBOOT,    MODE_EXISTING_DISK },
	{ "getboot",  ACTION_GETBOOT,    MODE_EXISTING_DISK },
	{ "addtree",  ACTION_ADDTREE,    MODE_EXISTING_DISK },
	{ "puttree",  ACTION_ADDTREE,    MODE_EXISTING_DISK },
	{ "shell",    ACTION_INTERACTIVE, MODE_EXISTING_DISK },
	{ NULL }
};

static const CLI_ActionSpec actionSpecs[] =
{
	{ "help", "help", ACTION_HELP },
	{ "h", "help", ACTION_HELP },
	{ "list", "list", ACTION_LIST },
	{ "l", "list", ACTION_LIST },
	{ "ls", "list", ACTION_LIST },
	{ "dir", "list", ACTION_LIST },
	{ "info", "info", ACTION_INFO },
	{ "i", "info", ACTION_INFO },
	{ "create", "create", ACTION_CREATEDISK },
	{ "c", "create", ACTION_CREATEDISK },
	{ "mkdisk", "create", ACTION_CREATEDISK },
	{ "add", "add", ACTION_ADD },
	{ "a", "add", ACTION_ADD },
	{ "delete", "delete", ACTION_DELETE },
	{ "d", "delete", ACTION_DELETE },
	{ "del", "delete", ACTION_DELETE },
	{ "rm", "delete", ACTION_DELETE },
	{ "extract", "extract", ACTION_EXTRACT },
	{ "x", "extract", ACTION_EXTRACT },
	{ "chdir", "chdir", ACTION_CHDIR },
	{ "cd", "chdir", ACTION_CHDIR },
	{ "mkdir", "mkdir", ACTION_MKDIR },
	{ "md", "mkdir", ACTION_MKDIR },
	{ "rmdir", "rmdir", ACTION_RMDIR },
	{ "rd", "rmdir", ACTION_RMDIR },
	{ "cat", "cat", ACTION_CAT },
	{ "type", "cat", ACTION_CAT },
	{ "hex", "hex", ACTION_HEXCAT },
	{ "dump", "hex", ACTION_HEXCAT },
	{ "setvol", "setvol", ACTION_SETVOL },
	{ "label", "setvol", ACTION_SETVOL },
	{ "setboot", "setboot", ACTION_SETBOOT },
	{ "boot", "setboot", ACTION_SETBOOT },
	{ "getboot", "getboot", ACTION_GETBOOT },
	{ "addtree", "addtree", ACTION_ADDTREE },
	{ "puttree", "addtree", ACTION_ADDTREE },
	{ "shell", "shell", ACTION_INTERACTIVE },
	{ 0, 0, 0 }
};

static const CLI_OptionSpec optionSpecs[] =
{
	{ 'b', "brief", DSK_OPTION_BRIEF, CLI_OPTION_NONE },
	{ 'l', "lowercase", DSK_OPTION_LOWERCASE, CLI_OPTION_NONE },
	{ 'a', "ascii", DSK_OPTION_ASCII, CLI_OPTION_NONE },
	{ 'h', "help", DSK_OPTION_HELP, CLI_OPTION_NONE },
	{ 0, 0, 0, CLI_OPTION_NONE }
};

static bool RunInteractiveSession();
static bool ExecuteInteractiveLine(char* line, bool* keepRunning);
static void TrimLine(char* line);
static bool HandleInteractiveBuiltinCommand(int argc, char* argv[], bool* keepRunning);
static bool RunInteractiveCommand(int argc, char* argv[]);
static void HostPrintCurrentDirectory();
static bool HostListDirectory(const char* pattern);
static bool HostChangeDirectoryCommand(const char* path);
static bool RunCommand (Action action, int argc, char *argv[]);
static bool ExecuteCLICommand(int argc, char* argv[], bool implicitDiskOpen);
static bool RunSessionCommands(int argc, char* argv[], bool implicitDiskOpen);

static bool AddFiles (int argc, char *argv[]);
static bool AddTree  (int argc, char *argv[]);
static bool Extract  (int argc, char *argv[]);
static bool SetBoot  (int argc, char *argv[]);
static bool GetBoot  (int argc, char *argv[]);
static char* FindLastPathSeparator(char* path);
static bool ParseAddSpec(const char* spec, char* hostFileName, size_t hostFileNameSize, char* diskFileName, size_t diskFileNameSize);

static inline char ToUpper(char c)
{
	if (c >= 'a' && c <= 'z')
		return c - ('a' - 'A');
	else
		return c;
}

static inline char ToLower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - ('A' - 'a');
	else
		return c;
}

static char* FindLastPathSeparator(char* path)
{
	char* slash = (char*)strrchr(path, '/');
	char* bslash = (char*)strrchr(path, '\\');
	return slash > bslash ? slash : bslash;
}

void TracePrintf(const char* format, ...)
{
}

static void PrintHelp(int argc, char *argv[])
{
	if (argc > 0)
	{
		const char* command = argv[0];
		int n;

		for (n = 0; commands[n].command != NULL; n++)
		{
			if (stricmp(command, commands[n].command) == 0)
			{
				switch (commands[n].action)
				{
					case ACTION_HELP:
						break;
					case ACTION_LIST:
						printf("Usage: dsk list [options] disk.img [pattern]\n");
						printf("       dsk disk.img [pattern]\n\n");
						printf("    -b, --brief       Brief listing\n");
						printf("    -l, --lowercase   Lowercase filenames\n");
						return;
					case ACTION_CREATEDISK:
						printf("Usage: dsk create diskfile [plus3|cpc|pcw|size|dd|hd]\n\n");
						printf("    Create a new empty disk image\n");
						printf("    The disk format is chosen based on the file extension:\n");
						printf("      .adf  - Amiga Disk File (ADF)\n");
						printf("      .dsk  - CPC/+3/PCW Disk Image\n");
						printf("      .img  - FAT Disk Image\n");
						printf("      .st   - FAT Disk Image\n");
						printf("    DSK presets:\n");
						printf("      plus3 - Spectrum +3 layout\n");
						printf("      cpc   - CPC system layout\n");
						printf("      pcw   - PCW double-sided layout\n");
						printf("    FAT/ADF size presets:\n");
						printf("      dd    - 880K for .adf, 720K for .img/.st\n");
						printf("      hd    - 1760K for .adf, 1440K for .img/.st\n");
						return;
					case ACTION_INFO:
						printf("Usage: dsk info disk.img\n\n");
						printf("    Show information about the given disk image\n");
						return;
					case ACTION_ADD:
						printf("Usage: dsk add disk.img hostfile[:diskpath] [hostfile2[:diskpath2] ...]\n\n");
						printf("    Copy one or more host files into the disk image.\n");
						printf("    Directory paths in hostfile are stripped inside the image unless diskpath is supplied.\n");
						printf("    Examples:\n");
						printf("      dsk add disk.adf foo.txt\n");
						printf("      dsk add disk.adf foo.txt:S/foo.txt\n");
						return;
					case ACTION_DELETE:
						printf("Usage: dsk delete disk.img pattern [pattern2 ...]\n\n");
						printf("    Delete files that match the supplied patterns. Wildcards are allowed.\n");
						return;
					case ACTION_EXTRACT:
						printf("Usage: dsk extract [options] disk.img [pattern]\n\n");
						printf("    Copy files from the disk image to the host directory.\n");
						printf("    If no pattern is provided all files are extracted.\n");
						printf("    -a, --ascii   Decode CPC or Sinclair BASIC files to ASCII and save them as .txt files.\n");
						return;
					case ACTION_CHDIR:
						printf("Usage: dsk chdir disk.img path\n\n");
						printf("    Change the current directory.\n");
						return;
					case ACTION_MKDIR:
						printf("Usage: dsk mkdir disk.img dirname\n\n");
						printf("    Create a directory inside the disk image.\n");
						return;
					case ACTION_RMDIR:
						printf("Usage: dsk rmdir disk.img dirname\n\n");
						printf("    Remove an empty directory from the disk image.\n");
						return;
					case ACTION_CAT:
						printf("Usage: dsk cat [options] disk.img pattern\n\n");
						printf("    Print the contents of files that match the pattern to stdout.\n");
						printf("    -a, --ascii   Force BASIC decoding.\n");
						printf("          AMSDOS and +3DOS BASIC files are decoded automatically.\n");
						return;
					case ACTION_HEXCAT:
						printf("Usage: dsk hex disk.img pattern\n\n");
						printf("    Dump matching files in a hexadecimal/ASCII view.\n");
						return;
					case ACTION_SETVOL:
						printf("Usage: dsk setvol disk.img [label]\n\n");
						printf("    Set or clear the volume label of the disk image.\n");
						printf("    If no label is provided, the volume label will be cleared.\n");
						printf("    Not all disk formats support volume labels.\n");
						return;
					case ACTION_SETBOOT:
						printf("Usage: dsk setboot disk.adf [source.adf|bootblock.bin|default]\n\n");
						printf("    Copy a 1024-byte Amiga bootblock into an ADF image.\n");
						printf("    If no source is supplied, the built-in default Amiga bootblock is used.\n");
						printf("    If source is an .adf, its bootblock is copied.\n");
						return;
					case ACTION_GETBOOT:
						printf("Usage: dsk getboot disk.adf output.bin\n\n");
						printf("    Extract the 1024-byte Amiga bootblock from an ADF image.\n");
						return;
					case ACTION_ADDTREE:
						printf("Usage: dsk addtree disk.img hostdir [destdir]\n\n");
						printf("    Recursively copy a host directory tree into the disk image.\n");
						printf("    Files keep their relative paths inside the image.\n");
						return;
					case ACTION_INTERACTIVE:
						printf("Usage: dsk shell disk.img\n\n");
						printf("    Open the disk image and enter an interactive shell.\n");
						printf("    You can chain commands with :: in batch or shell mode.\n");
						printf("    Additional shell-only commands:\n");
						printf("      LDIR [pattern]    - List host files\n");
						printf("      LCD path          - Change host directory\n");
						printf("      PUT files         - Alias for ADD\n");
						printf("      GET [pattern]     - Alias for EXTRACT\n");
						printf("      EXIT              - Leave the session\n");
						return;
					default:
						printf("%s: no extended help available\n\n", command);
						break;
				}
				break;
			}
		}
		if (commands[n].command == NULL)
			printf("Unknown command: %s\n\n", command);
	}

	printf("Disk file utility for DAAD " VERSION_STR "\n\n");
	printf("Usage: dsk [options] command [command options] disk.img [arguments]\n");
	printf("       dsk [options] disk.img [pattern]\n\n");
	printf("Batch mode: dsk [options] disk.img -- command [args] :: command [args]\n");
	printf("            dsk [options] disk.img @commands.txt\n\n");
	printf("Global options: -h, --help\n\n");
	printf("Available commands:\n\n");
	printf("    list      List contents of disk image (default)\n");
	printf("    create    Create new disk image\n");
	printf("    info      Show information about disk image\n");
	printf("    add       Add files to disk image\n");
	printf("    extract   Extract files from disk image\n");
	printf("    delete    Delete files from disk image\n");
	printf("    rmdir     Delete an empty directory from disk image\n");
	printf("    mkdir     Make a directory in disk image\n");
	printf("    chdir     Change current directory\n");
	printf("    setvol    Set or clear the volume label\n");
	printf("    setboot   Set ADF bootblock from built-in default, ADF, or raw 1024-byte file\n");
	printf("    getboot   Extract ADF bootblock to a host file\n");
	printf("    addtree   Recursively add a host directory tree\n");
	printf("    shell     Open an interactive shell\n");
	printf("    help      Show extended command help\n");
	printf("\nAdditional interactive-only commands: LDIR, LCD/LCHDIR, PUT, GET, EXIT\n");
	printf("Use :: as the shared command separator in batch and shell mode.\n");
	printf("Use @commands.txt to execute one command line per file line.\n");
	printf("\n");
}

static void TrimLine(char* line)
{
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = 0;
}

static bool HostGetCurrentDirectory(char* buffer, size_t bufferSize)
{
	return OS_GetCurrentDirectory(buffer, bufferSize);
}

static void HostPrintCurrentDirectory()
{
	char cwd[FILE_MAX_PATH];
	if (HostGetCurrentDirectory(cwd, sizeof(cwd)))
		printf("%s\n", cwd);
	else
		printf("Unable to determine host current directory\n");
}

static bool HostListDirectory(const char* pattern)
{
	const char* search = (pattern && pattern[0]) ? pattern : "*.*";
	FindFileResults results;
	if (!OS_FindFirstFile(search, &results))
		return false;

	char cwd[FILE_MAX_PATH];
	if (HostGetCurrentDirectory(cwd, sizeof(cwd)))
		printf("\n  Host directory of %s\n\n", cwd);

	int fileCount = 0;
	int dirCount = 0;
	do
	{
		if (results.attributes & FileAttribute_Directory)
		{
			printf("%-31s <DIR>\n", results.fileName);
			dirCount++;
		}
		else
		{
			printf("%-31s %8u\n", results.fileName, results.fileSize);
			fileCount++;
		}
	}
	while (OS_FindNextFile(&results));

	printf("\n%16d File(s)\n%16d Dir(s)\n", fileCount, dirCount);
	return true;
}

static bool HostChangeDirectoryCommand(const char* path)
{
	if (path == NULL || path[0] == 0)
	{
		HostPrintCurrentDirectory();
		return true;
	}
	if (OS_ChangeDirectory(path))
	{
		HostPrintCurrentDirectory();
		return true;
	}
	printf("lcd: unable to change directory to %s\n", path);
	return false;
}

static bool RunInteractiveCommand(int argc, char* argv[])
{
	if (argc == 0)
		return true;
	return ExecuteCLICommand(argc, argv, true);
}

static bool HandleInteractiveBuiltinCommand(int argc, char* argv[], bool* keepRunning)
{
	if (argc == 0)
		return false;
	const char* command = argv[0];
	if (stricmp(command, "exit") == 0 || stricmp(command, "quit") == 0)
	{
		*keepRunning = false;
		return true;
	}
	if (stricmp(command, "ldir") == 0)
	{
		HostListDirectory(argc > 1 ? argv[1] : "*.*");
		return true;
	}
	if (stricmp(command, "lcd") == 0 || stricmp(command, "lchdir") == 0)
	{
		HostChangeDirectoryCommand(argc > 1 ? argv[1] : NULL);
		return true;
	}
	if (stricmp(command, "put") == 0)
	{
		if (argc < 2)
		{
			printf("put: missing host file name\n");
			return true;
		}
		AddFiles(argc - 1, argv + 1);
		return true;
	}
	if (stricmp(command, "get") == 0)
	{
		Extract(argc - 1, argv + 1);
		return true;
	}
	return false;
}

typedef struct
{
	bool* keepRunning;
}
InteractiveSessionContext;

static bool ExecuteInteractiveSessionCommand(int argc, char* argv[], void* context)
{
	InteractiveSessionContext* interactiveContext = (InteractiveSessionContext*)context;
	if (HandleInteractiveBuiltinCommand(argc, argv, interactiveContext->keepRunning))
		return true;
	return RunInteractiveCommand(argc, argv);
}

static bool ExecuteInteractiveLine(char* line, bool* keepRunning)
{
	char* argv[INTERACTIVE_MAX_ARGS];
	int argc = Session_TokenizeLine(line, argv, INTERACTIVE_MAX_ARGS);
	if (argc == 0)
		return true;
	char errorBuffer[256];
	InteractiveSessionContext context = { keepRunning };
	if (!Session_ExecuteTokenStream(argc, argv, "::", ExecuteInteractiveSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
	{
		printf("Error: %s\n", errorBuffer);
		return false;
	}
	return true;
}

static bool RunInteractiveSession()
{
	if (disk == NULL || diskFileName == NULL)
	{
		printf("Interactive mode requires an open disk image\n");
		return false;
	}
	printf("Entering interactive session for %s. Type HELP for commands, EXIT to quit.\n", diskFileName);
	bool keepRunning = true;
	char line[512];
	while (keepRunning)
	{
		char cwd[FILE_MAX_PATH];
		if (DIM_GetCWD(disk, cwd, sizeof(cwd)) == 0)
			strcpy(cwd, "\\");
		printf("%s%s> ", diskFileName, cwd);
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin))
		{
			printf("\n");
			break;
		}
		TrimLine(line);
		if (line[0] == 0)
			continue;
		ExecuteInteractiveLine(line, &keepRunning);
	}
	return true;
}

static bool CheckExtension(const char* filename, const char* ext)
{
	const char* p = strrchr(filename, '.');
	if (!p)
		return false;
	return stricmp(p + 1, ext) == 0;
}

static int ResolveDiskPresetSize(const char* diskName, const char* preset)
{
	bool isADF = CheckExtension(diskName, "adf");
	bool isIMG = CheckExtension(diskName, "img");
	bool isST  = CheckExtension(diskName, "st");
	bool isFAT = isIMG || isST;

	if (stricmp(preset, "dd") == 0)
	{
		if (isADF)
			return DISK_SIZE_880KB;
		if (isFAT)
			return DISK_SIZE_720KB;
	}
	else if (stricmp(preset, "hd") == 0)
	{
		if (isADF)
			return DISK_SIZE_1760KB;
		if (isFAT)
			return DISK_SIZE_1440KB;
	}
	return 0;
}

static bool MkDir (int argc, char *argv[])
{
	if (argc < 1)
	{
		printf("Missing directory name\n");
		return false;
	}
	if (!DIM_MakeDirectory(disk, argv[0]))
	{
		printf("%s: %s\n", argv[0], DIM_GetErrorString());
		return false;
	}
	return true;
}

static bool ChDir (int argc, char *argv[])
{
	if (argc < 1)
	{
		printf("Missing directory name\n");
		return false;
	}
	if (!DIM_ChangeDirectory(disk, argv[0]))
	{
		printf("%s: %s\n", argv[0], DIM_GetErrorString());
		return false;
	}
	return true;
}

static bool RmDir (int argc, char *argv[])
{
	if (argc < 1)
	{
		printf("Missing directory name\n");
		return false;
	}
	do
	{
		if (!DIM_RemoveDirectory(disk, argv[0]))
		{
			printf("%s: %s\n", argv[0], DIM_GetErrorString());
			return false;
		}
		printf("%s: directory removed\n", argv[0]);
		argc--, argv++;
	}
	while(argc > 0);
	return true;
}

static bool SetVol (int argc, char *argv[])
{
	const char* label = (argc > 0) ? argv[0] : "";

	// TODO: DIM_SetVolumeLabel doesn't exist yet - needs to be implemented
	// in the DIM library for FAT and ADF disk formats
	if (!DIM_SetVolumeLabel(disk, label))
	{
		printf("%s\n", DIM_GetErrorString());
		return false;
	}

	if (label[0] == 0)
		printf("Volume label cleared\n");
	else
		printf("Volume label set to: %s\n", label);
	return true;
}

static bool EnsureBufferCapacity(uint8_t** buffer, size_t* bufferSize, size_t requiredSize, const char* reason)
{
	if (*bufferSize >= requiredSize)
		return true;

	uint8_t* newBuffer = Allocate<uint8_t>(reason, requiredSize, false);
	if (newBuffer == NULL)
	{
		DIM_SetError(DIMError_OutOfMemory);
		return false;
	}

	if (*buffer != NULL)
		Free(*buffer);

	*buffer = newBuffer;
	*bufferSize = requiredSize;
	return true;
}

enum BasicDecoderKind
{
	BASICDEC_NONE,
	BASICDEC_CPC,
	BASICDEC_ZX,
};

static bool IsBasicCandidate(const FindFileResults* result)
{
	return strncmp(result->description, "BASIC", 5) == 0;
}

static BasicDecoderKind GetHeaderBasicDecoder(const FindFileResults* result)
{
	if (disk->type != DIM_CPC || !IsBasicCandidate(result))
		return BASICDEC_NONE;

	const CPC_FindResults* cpc = (const CPC_FindResults*)result->internalData;
	if (cpc->osHeaderType == CPC_HEADER_PLUS3DOS)
		return BASICDEC_ZX;
	if (cpc->osHeaderType == CPC_HEADER_AMSDOS)
		return BASICDEC_CPC;
	return BASICDEC_NONE;
}

static bool TryDecodeBasic(FILE* output, const uint8_t* buffer, uint32_t size, BasicDecoderKind decoder)
{
	switch (decoder)
	{
		case BASICDEC_CPC:
			return BASCPC_DecodeToFile(buffer, size, output);
		case BASICDEC_ZX:
			return BASZX_DecodeToFile(buffer, size, output);
		default:
			return false;
	}
}

static bool TryDecodeBasicAuto(FILE* output, const uint8_t* buffer, uint32_t size, BasicDecoderKind preferred)
{
	if (preferred != BASICDEC_NONE && TryDecodeBasic(output, buffer, size, preferred))
		return true;

	if (preferred != BASICDEC_ZX && TryDecodeBasic(output, buffer, size, BASICDEC_ZX))
		return true;
	if (preferred != BASICDEC_CPC && TryDecodeBasic(output, buffer, size, BASICDEC_CPC))
		return true;
	return false;
}

static bool BuildDecodedExtractName(char* outputName, size_t outputNameSize, const char* fileName, BasicDecoderKind decoder)
{
	(void)decoder;
	const char* suffix = ".txt";
	int written = snprintf(outputName, outputNameSize, "%s%s", fileName, suffix);
	return written > 0 && (size_t)written < outputNameSize;
}

static bool Cat (int argc, char *argv[], bool hexMode)
{
	const char* pattern = NULL;
	FindFileResults result;
	int fileCount = 0;
	bool singleFile = false;
	size_t bufferSize = 0;
	uint8_t* buffer = NULL;
	char savedCwd[FILE_MAX_PATH];
	if (DIM_GetCWD(disk, savedCwd, sizeof(savedCwd)) == 0)
		savedCwd[0] = 0;

	if (argc > 0)
		pattern = argv[0];
	if (pattern != NULL)
	{
		char pathBuffer[FILE_MAX_PATH];
		StrCopy(pathBuffer, sizeof(pathBuffer), pattern);
		char* pathSeparator = FindLastPathSeparator(pathBuffer);
		if (pathSeparator != NULL)
		{
			*pathSeparator = 0;
			if (pathSeparator > pathBuffer && !DIM_ChangeDirectory(disk, pathBuffer))
			{
				printf("Error: Cannot change directory to %s\n", pathBuffer);
				return false;
			}
			pattern = pathSeparator + 1;
		}

		if (strchr(pattern, '?') == NULL && strchr(pattern, '*') == NULL)
		{
			singleFile = true;
			if (DIM_FindFile(disk, &result, pattern))
			{
				goto process_results;
			}
			DIM_ChangeDirectory(disk, savedCwd);
			printf("File not found\n");
			return true;
		}
	}

	if (DIM_FindFirstFile(disk, &result, pattern))
	{
process_results:
		do
		{
			if (!singleFile)
				printf("\n%s (%u bytes)\n\n", result.fileName, result.fileSize);

			if (!(result.attributes & FileAttribute_Directory))
			{
				uint32_t size = result.fileSize;
				uint32_t read = 0;
				if (!EnsureBufferCapacity(&buffer, &bufferSize, size, "DSK cat buffer"))
				{
					return false;
				}
				read = DIM_ReadFile(disk, result.fileName, buffer, bufferSize);
				if (read == 0 && size != 0)
					printf("%s: %s\n", result.fileName, DIM_GetErrorString());
				else
				{
					fileCount++;
					size = read;
					bool forceBasicDecode = (options & TEXTOPT_BASIC) != 0;
					BasicDecoderKind headerDecoder = GetHeaderBasicDecoder(&result);
					bool autoBasicDecode = !forceBasicDecode && !hexMode && headerDecoder != BASICDEC_NONE;

					if (!hexMode && disk->type == DIM_CPC && result.description[0] == 0)
					{
						const uint8_t* eof = (const uint8_t*)memchr(buffer, 0x1A, size);
						if (eof != NULL)
							size = (uint32_t)(eof - buffer);
					}
					if (!hexMode && disk->type == DIM_CPC && (forceBasicDecode || autoBasicDecode))
					{
						BasicDecoderKind preferred = forceBasicDecode ? headerDecoder : headerDecoder;
						if (!TryDecodeBasicAuto(stdout, buffer, size, preferred))
						{
							if (forceBasicDecode)
							{
								printf("%s: unable to decode BASIC program\n", result.fileName);
							}
							else
							{
								fwrite(buffer, size, 1, stdout);
							}
						}
					}
					else if (hexMode)
					{
						uint32_t i;
						for (i = 0; i < size; i++)
						{
							if (i % 16 == 0)
								printf("%04X: ", i);
							printf("%02X ", buffer[i]);
							if (i % 16 == 15)
							{
								for (uint32_t j = i - 15; j <= i; j++)
								{
									if (buffer[j] < 32 || buffer[j] > 127)
										printf(".");
									else
										printf("%c", buffer[j]);
								}
								printf("\n");
							}
						}
						if (i % 16 != 0)
						{
							for (uint32_t j = i; (j % 16) != 0; j++)
								printf("   ");
							for (uint32_t j = i & ~0x0F; j < i; j++)
							{
								if (buffer[j] < 32 || buffer[j] > 127)
									printf(".");
								else
									printf("%c", buffer[j]);
							}
							printf("\n");
						}
					}
					else
					{
						fwrite(buffer, size, 1, stdout);
					}
				}
			}
			else
			{
				if (singleFile)
				{
					printf("%s: it's a directory\n", result.fileName);
					DIM_ChangeDirectory(disk, savedCwd);
					return false;
				}
			}
		}
		while (!singleFile && DIM_FindNextFile(disk, &result));
	}

	if (fileCount == 0)
		printf("File not found\n");
	if (buffer)
		Free(buffer);
	DIM_ChangeDirectory(disk, savedCwd);
	return true;
}

static bool Dir (int argc, char* argv[])
{
	const char* pattern = NULL;
	int dirCount = 0;
	int fileCount = 0;
	uint32_t totalSize = 0;
	char cwd[FILE_MAX_PATH];
	char label[64];
	FindFileResults result;

	if (argc > 0)
		pattern = argv[0];
	if (pattern != NULL)
	{
		char pathBuffer[FILE_MAX_PATH];
		StrCopy(pathBuffer, sizeof(pathBuffer), pattern);
		char* pathSeparator = FindLastPathSeparator(pathBuffer);
		if (pathSeparator != NULL)
		{
			*pathSeparator = 0;
			if (pathSeparator > pathBuffer && !DIM_ChangeDirectory(disk, pathBuffer))
			{
				printf("Error: Cannot change directory to %s\n", pathBuffer);
				return false;
			}
			pattern = pathSeparator + 1;
		}
		if (strchr(pattern, '?') == NULL && strchr(pattern, '*') == NULL)
		{
			if (DIM_ChangeDirectory(disk, pattern))
			{
				argc--, argv++;
				pattern = argc > 0 ? argv[0] : "*";
			}
		}
	}

	if (DIM_GetCWD(disk, cwd, sizeof(cwd)) > 0)
	{
		if (!(options & DIR_BRIEF))
		{
			if (DIM_GetVolumeLabel(disk, label, 64) > 0)
				printf("\n  Volume has label %s\n", label);
			printf("\n  Directory of %s\n\n", cwd);
			if (cwd[0] && cwd[1])
			{
				printf("%-31s <DIR>\n", ".");
				printf("%-31s <DIR>\n", "..");
				dirCount += 2;
			}
		}
		else if (cwd[0] && cwd[1])
		{
			printf("./\n../\n");
			dirCount += 2;
		}
	}

	if (DIM_FindFirstFile(disk, &result, pattern))
	{
		do
		{
			if (options & DIR_LOWERCASE)
			{
				for (char* ptr = result.fileName; *ptr; ptr++)
					*ptr = ToLower(*ptr);
			}
			if (result.attributes & FileAttribute_Directory)
			{
				if (options & DIR_BRIEF)
					printf("%s\\\n", result.fileName);
				else
				{
					char timeStr[20] = "";
					if (result.modifyTime != 0)
					{
						time_t modified = (time_t)result.modifyTime;
						struct tm* t = localtime(&modified);
						if (t)
							strftime(timeStr, sizeof(timeStr), "%d-%m-%y %H:%M", t);
					}
					printf("%-31s <DIR>      %14s  %s\n", result.fileName, timeStr, result.description);
				}
				dirCount++;
			}
			else
			{
				if (options & DIR_BRIEF)
					printf("%s\n", result.fileName);
				else
				{
					char timeStr[20] = "";
					if (result.modifyTime != 0)
					{
						time_t modified = (time_t)result.modifyTime;
						struct tm* t = localtime(&modified);
						if (t)
							strftime(timeStr, sizeof(timeStr), "%d-%m-%y %H:%M", t);
					}
					printf("%-31s %8d  %14s  %s\n", result.fileName, result.fileSize, timeStr, result.description);
				}
				fileCount++;
				totalSize += result.fileSize;
			}
		}
		while (DIM_FindNextFile(disk, &result));
	}

	if (fileCount > 0 || dirCount > 0)
	{
		if (!(options & DIR_BRIEF))
		{
			printf("\n%16d File(s) %16u bytes\n", fileCount, totalSize);
			if ((disk->capabilities & DIMCaps_Directories))
				printf("%16d Dir(s)  %16u bytes free\n", dirCount, (unsigned)DIM_GetFreeSpace(disk));
			else
				printf("%41u bytes free\n", (unsigned)DIM_GetFreeSpace(disk));
		}
	}
	else if (!(options & DIR_BRIEF))
	{
		printf("No files found %16u bytes free\n", (unsigned)DIM_GetFreeSpace(disk));
	}
	return true;
}

static bool CreateDisk(int argc, char *argv[])
{
	int size = 0;
	const char* preset = NULL;
    int n;
    for (n = 0; n < argc; n++)
    {
        const char* command = argv[n];
        if (stricmp(command, "/?") == 0 || stricmp(command, "/h") == 0 || stricmp(command, "/help") == 0)
        {
            PrintHelp(1, argv);
            return true;
        }
        if (diskFileName == NULL)
        {
            diskFileName = command;
        }
        else if ((size = ResolveDiskPresetSize(diskFileName, argv[n])) != 0)
        {
        }
		else if (CheckExtension(diskFileName, "dsk") &&
			     (stricmp(argv[n], "plus3") == 0 || stricmp(argv[n], "cpc") == 0 || stricmp(argv[n], "pcw") == 0))
		{
			preset = argv[n];
		}
        else if (isdigit(argv[n][0]))
        {
            int sizeKB = atoi(argv[n]);
            char lastChar = argv[n][strlen(argv[n]) - 1];
            if (sizeKB > 0)
            {
                if (lastChar == 'm' || lastChar == 'M')
                    sizeKB *= 1024;
            }
            size = sizeKB * 1024;
        }
        else
        {
            printf("Unknown disk size preset: %s\n", argv[n]);
            return false;
        }
    }
	if (diskFileName == NULL && argc > 0)
	{
		diskFileName = argv[0];
		argc--, argv++;
	}
	if (diskFileName == NULL)
	{
		printf("Missing disk file name\n");
		return false;
	}

	disk = DIM_CreateDisk(diskFileName, size, preset);
	if (!disk)
	{
		printf("%s: %s\n", diskFileName, DIM_GetErrorString());
		return false;
	}
	printf("%s created\n", diskFileName);
	return true;
}

static bool DeleteFiles (int argc, char *argv[])
{
	int fileCount = 0;
	int errorCount = 0;
	FindFileResults result;
	char fileName[FILE_MAX_PATH];

	if (argc < 1)
	{
		printf("%s: missing delete pattern\n", diskFileName);
		return false;
	}

	do
	{
		const char* pattern = argv[0];
		if (DIM_FindFirstFile(disk, &result, pattern))
		{
			bool more = true;
			do
			{
				fileCount++;
				memcpy(fileName, result.fileName, FILE_MAX_PATH);
				more = DIM_FindNextFile(disk, &result);

				if (!DIM_RemoveFile(disk, fileName))
				{
					printf("%s: %s\n", fileName, DIM_GetErrorString());
					errorCount++;
				}
			}
			while (more);
		}
		else if (strchr(pattern, '*') == 0 && strchr(pattern, '?') == 0)
		{
			printf("%s: File not found\n", pattern);
			errorCount++, fileCount++;
		}
		argc--, argv++;
	}
	while (argc > 0);

	if (fileCount > 0)
		printf("%d File(s) deleted\n", fileCount - errorCount);
	else
		printf("No files found\n");
	return true;
}

static bool EnsureDiskDirectoryPath(const char* path)
{
	if (path == NULL || path[0] == 0)
		return true;

	char savedCwd[FILE_MAX_PATH];
	if (DIM_GetCWD(disk, savedCwd, sizeof(savedCwd)) == 0)
		savedCwd[0] = 0;

	if (!DIM_ChangeDirectory(disk, "\\"))
		return false;

	const char* ptr = path;
	char component[64];
	while (*ptr)
	{
		const char* sep = ptr;
		while (*sep && *sep != '/' && *sep != '\\')
			sep++;
		size_t len = sep - ptr;
		if (len > 0)
		{
			if (len >= sizeof(component))
			{
				DIM_ChangeDirectory(disk, savedCwd);
				DIM_SetError(DIMError_CommandNotSupported);
				return false;
			}
			memcpy(component, ptr, len);
			component[len] = 0;
			if (!DIM_ChangeDirectory(disk, component))
			{
				if (!DIM_MakeDirectory(disk, component) || !DIM_ChangeDirectory(disk, component))
				{
					DIM_ChangeDirectory(disk, savedCwd);
					return false;
				}
			}
		}
		ptr = (*sep) ? sep + 1 : sep;
	}

	DIM_ChangeDirectory(disk, savedCwd);
	return true;
}

static bool AddFileToDisk(const char* hostFileName, const char* diskFileName)
{
	static size_t bufferSize = 0;
	static uint8_t* buffer = NULL;
	char savedCwd[FILE_MAX_PATH];
	if (DIM_GetCWD(disk, savedCwd, sizeof(savedCwd)) == 0)
		savedCwd[0] = 0;

	File* file = File_Open(hostFileName);
	if (file == NULL)
	{
		printf("%s: file not found\n", hostFileName);
		return false;
	}
	uint64_t size = File_GetSize(file);
	uint64_t freeSpace = DIM_GetFreeSpace(disk);
	if (freeSpace < size)
	{
		printf("%s: no enough space left on disk\n", hostFileName);
		File_Close(file);
		return false;
	}
	if (!EnsureBufferCapacity(&buffer, &bufferSize, size, "DSK add buffer"))
	{
		printf("Out of memory\n");
		File_Close(file);
		return false;
	}
	if (File_Read(file, buffer, size) != size)
	{
		printf("%s: error reading file\n", hostFileName);
		File_Close(file);
		return false;
	}
	File_Close(file);

	char diskPath[FILE_MAX_PATH];
	if (diskFileName != NULL && diskFileName[0] != 0)
	{
		strncpy(diskPath, diskFileName, sizeof(diskPath));
		diskPath[sizeof(diskPath) - 1] = 0;
	}
	else
	{
		const char* ptr = hostFileName;
		const char* base = ptr;
		while (*ptr != 0)
		{
			if (*ptr == '/' || *ptr == '\\' || *ptr == ':')
				base = ptr + 1;
			ptr++;
		}
		strncpy(diskPath, base, sizeof(diskPath));
		diskPath[sizeof(diskPath) - 1] = 0;
	}

	char* slash = strrchr(diskPath, '/');
	char* bslash = strrchr(diskPath, '\\');
	char* sep = slash > bslash ? slash : bslash;
	if (sep != NULL)
	{
		*sep = 0;
		if (!EnsureDiskDirectoryPath(diskPath))
		{
			printf("%s: %s\n", diskPath, DIM_GetErrorString());
			DIM_ChangeDirectory(disk, savedCwd);
			return false;
		}
		*sep = '/';
	}

	if (!DIM_WriteFile(disk, diskPath, buffer, (uint32_t)size))
    {
		printf("%s: %s\n", diskPath, DIM_GetErrorString());
		DIM_ChangeDirectory(disk, savedCwd);
        return false;
    }
	else
    {
		printf("%s: %d bytes written\n", diskPath, (uint32_t)size);
		DIM_ChangeDirectory(disk, savedCwd);
        return true;
    }
}

static bool AddFile (const char* filename)
{
	return AddFileToDisk(filename, NULL);
}

static bool ParseAddSpec(const char* spec, char* hostFileName, size_t hostFileNameSize, char* diskFileName, size_t diskFileNameSize)
{
	const char* split = NULL;
	const char* ptr = spec;
	while (*ptr)
	{
		if (*ptr == ':')
		{
			bool isWindowsDrive = (ptr == spec + 1 && isalpha((unsigned char)spec[0]));
			if (!isWindowsDrive)
				split = ptr;
		}
		ptr++;
	}

	if (split == NULL)
	{
		strncpy(hostFileName, spec, hostFileNameSize);
		hostFileName[hostFileNameSize - 1] = 0;
		diskFileName[0] = 0;
		return true;
	}

	size_t hostLen = split - spec;
	size_t diskLen = strlen(split + 1);
	if (hostLen == 0 || hostLen >= hostFileNameSize || diskLen >= diskFileNameSize)
		return false;

	memcpy(hostFileName, spec, hostLen);
	hostFileName[hostLen] = 0;
	memcpy(diskFileName, split + 1, diskLen + 1);
	return true;
}

static bool AddFiles (int argc, char *argv[])
{
	bool ok = true;

	for (; argc > 0; argc--, argv++)
	{
		char hostFileName[FILE_MAX_PATH];
		char diskPath[FILE_MAX_PATH];
		if (!ParseAddSpec(argv[0], hostFileName, sizeof(hostFileName), diskPath, sizeof(diskPath)))
		{
			printf("%s: invalid add specification\n", argv[0]);
			ok = false;
			continue;
		}

        if (strchr(hostFileName, '*') != NULL || strchr(hostFileName, '?') != NULL)
        {
            FindFileResults results;
            char *dirSep = strrchr(hostFileName, '/');
            if (!dirSep)
                dirSep = strrchr(hostFileName, '\\');
            if (dirSep)
            {
                char savedChar = *dirSep;
                *dirSep = 0;
                if (!OS_ChangeDirectory(hostFileName))
                {
                    printf("%s: directory not found\n", hostFileName);
                    ok = false;
                    *dirSep = savedChar;
                    continue;
                }
                *dirSep = savedChar;
                memmove(hostFileName, dirSep + 1, strlen(dirSep + 1) + 1);
            }
            if (diskPath[0] != 0)
            {
                printf("%s: wildcard host patterns cannot be combined with a disk destination path\n", argv[0]);
                ok = false;
                continue;
            }
            if (OS_FindFirstFile(hostFileName, &results))
            {
                do
                {
                    if (results.attributes & FileAttribute_Directory)
                        continue;
                    if (!AddFile(results.fileName))
                        ok = false;
                }
                while (OS_FindNextFile(&results));
            }
            else
            {
                printf("%s: file not found\n", hostFileName);
                ok = false;
            }
            continue;
        }

		if (!AddFileToDisk(hostFileName, diskPath[0] ? diskPath : NULL))
			ok = false;
	}
	return ok;
}

static bool AddTreeRecursive(const char* hostDir, const char* destDir)
{
	char savedCwd[FILE_MAX_PATH];
	if (!OS_GetCurrentDirectory(savedCwd, sizeof(savedCwd)))
		savedCwd[0] = 0;
	if (!OS_ChangeDirectory(hostDir))
	{
		printf("%s: directory not found\n", hostDir);
		return false;
	}

	if (destDir != NULL && destDir[0] != 0 && !EnsureDiskDirectoryPath(destDir))
	{
		printf("%s: %s\n", destDir, DIM_GetErrorString());
		OS_ChangeDirectory(savedCwd);
		return false;
	}

	bool ok = true;
	FindFileResults results;
	if (OS_FindFirstFile("*", &results))
	{
		do
		{
			if (strcmp(results.fileName, ".") == 0 || strcmp(results.fileName, "..") == 0)
				continue;

			char nextDest[FILE_MAX_PATH];
			if (destDir != NULL && destDir[0] != 0)
			{
				size_t destLen = strlen(destDir);
				size_t nameLen = strlen(results.fileName);
				if (destLen + 1 + nameLen >= sizeof(nextDest))
				{
					printf("%s/%s: path too long\n", destDir, results.fileName);
					ok = false;
					continue;
				}
				memcpy(nextDest, destDir, destLen);
				nextDest[destLen] = '/';
				memcpy(nextDest + destLen + 1, results.fileName, nameLen + 1);
			}
			else
			{
				if (strlen(results.fileName) >= sizeof(nextDest))
				{
					printf("%s: path too long\n", results.fileName);
					ok = false;
					continue;
				}
				strcpy(nextDest, results.fileName);
			}

			if (results.attributes & FileAttribute_Directory)
			{
				if (!AddTreeRecursive(results.fileName, nextDest))
					ok = false;
			}
			else if (!AddFileToDisk(results.fileName, nextDest))
			{
				ok = false;
			}
		}
		while (OS_FindNextFile(&results));
	}

	OS_ChangeDirectory(savedCwd);
	return ok;
}

static bool AddTree(int argc, char* argv[])
{
	if (argc < 1)
	{
		printf("%s: missing host directory\n", diskFileName);
		return false;
	}

	const char* hostDir = argv[0];
	const char* destDir = argc > 1 ? argv[1] : "";
	return AddTreeRecursive(hostDir, destDir);
}

static bool Extract (int argc, char *argv[])
{
	uint8_t* buffer = NULL;
	size_t bufferSize = 0;
	int fileCount = 0;
	int errorCount = 0;
	FindFileResults result;

	do
	{
		const char* pattern = argc > 0 ? argv[0] : "*";
		if (DIM_FindFirstFile(disk, &result, pattern))
		{
			do
			{
				fileCount++;

				if (!EnsureBufferCapacity(&buffer, &bufferSize, result.fileSize, "DSK extract buffer"))
				{
					printf("Error: Out of memory\n");
					return 1;
				}
				if (result.attributes & FileAttribute_Directory)
				{
					// TODO: Extract subdirectory
				}
				else
				{
					uint32_t read = DIM_ReadFile(disk, result.fileName, buffer, bufferSize);
					bool decodeBasic = !((result.attributes & FileAttribute_Directory) != 0) &&
						(options & TEXTOPT_BASIC) != 0 &&
						disk->type == DIM_CPC;
					if (decodeBasic)
					{
						BasicDecoderKind preferred = GetHeaderBasicDecoder(&result);
						BasicDecoderKind outputDecoder = preferred != BASICDEC_NONE ? preferred : BASICDEC_CPC;
						char outputName[FILE_MAX_PATH];
						if (!BuildDecodedExtractName(outputName, sizeof(outputName), result.fileName, outputDecoder))
						{
							printf("%s: decoded output path too long\n", result.fileName);
							errorCount++;
						}
						else
						{
							FILE* file = fopen(outputName, "wb");
							if (!file)
							{
								printf("Error: Cannot open %s\n", outputName);
								errorCount++;
							}
							else
							{
								if (!TryDecodeBasicAuto(file, buffer, read, preferred))
								{
									printf("%s: unable to decode BASIC program\n", result.fileName);
									errorCount++;
								}
								else
								{
									printf("Extracted %s as %s\n", result.fileName, outputName);
								}
								fclose(file);
							}
						}
					}
					else
					{
						File* file = File_Create(result.fileName);
						if (!file)
						{
							printf("Error: Cannot open %s\n", result.fileName);
							errorCount++;
						}
						else
						{
							if (File_Write(file, buffer, read) != read)
							{
								printf("Error: Writing to %s\n", result.fileName);
								errorCount++;
							}
							else
							{
								printf("Extracted %s (%u bytes)\n", result.fileName, read);
							}
							File_Close(file);
						}
					}
				}
			}
			while (DIM_FindNextFile(disk, &result));
		}
		else if (strchr(pattern, '*') == 0 && strchr(pattern, '?') == 0)
		{
			printf("%s: File not found\n", pattern);
			errorCount++, fileCount++;
		}
		argc--, argv++;
	}
	while (argc > 0);

	if (fileCount > 0)
		printf("%d File(s) extracted\n", fileCount - errorCount);
	else
	printf("No files found\n");
	return true;
}

static bool SetBoot(int argc, char* argv[])
{
	if (disk->type != DIM_ADF)
	{
		printf("%s: bootblocks are only supported for ADF images\n", diskFileName);
		return false;
	}

	uint8_t boot[1024];
	const char* source = argc > 0 ? argv[0] : NULL;
	if (source == NULL || source[0] == 0 || stricmp(source, "default") == 0)
	{
		memset(boot, 0, sizeof(boot));
		memcpy(boot, DefaultAmigaBootBlockPrefix, sizeof(DefaultAmigaBootBlockPrefix));
	}
	else
	{
		const char* extension = strrchr(source, '.');
		if (extension && stricmp(extension, ".adf") == 0)
		{
			DIM_Disk* sourceDisk = DIM_OpenDisk(source);
			if (!sourceDisk)
			{
				printf("%s: %s\n", source, DIM_GetErrorString());
				return false;
			}
			bool ok = DIM_ReadBootBlock(sourceDisk, boot, sizeof(boot));
			if (!ok)
				printf("%s: %s\n", source, DIM_GetErrorString());
			DIM_CloseDisk(sourceDisk);
			if (!ok)
				return false;
		}
		else
		{
			File* file = File_Open(source);
			if (!file)
			{
				printf("%s: file not found\n", source);
				return false;
			}
			if (File_Read(file, boot, sizeof(boot)) != sizeof(boot))
			{
				printf("%s: bootblock must be exactly 1024 bytes\n", source);
				File_Close(file);
				return false;
			}
			File_Close(file);
		}
	}

	if (!DIM_WriteBootBlock(disk, boot, sizeof(boot)))
	{
		printf("%s: %s\n", diskFileName, DIM_GetErrorString());
		return false;
	}

	if (source == NULL || source[0] == 0 || stricmp(source, "default") == 0)
		printf("%s: default bootblock written\n", diskFileName);
	else
		printf("%s: bootblock written\n", diskFileName);
	return true;
}

static bool GetBoot(int argc, char* argv[])
{
	if (argc < 1)
	{
		printf("%s: missing output file name\n", diskFileName);
		return false;
	}
	if (disk->type != DIM_ADF)
	{
		printf("%s: bootblocks are only supported for ADF images\n", diskFileName);
		return false;
	}

	uint8_t boot[1024];
	if (!DIM_ReadBootBlock(disk, boot, sizeof(boot)))
	{
		printf("%s: %s\n", diskFileName, DIM_GetErrorString());
		return false;
	}

	File* file = File_Create(argv[0]);
	if (!file)
	{
		printf("%s: cannot create file\n", argv[0]);
		return false;
	}
	bool ok = File_Write(file, boot, sizeof(boot)) == sizeof(boot);
	File_Close(file);
	if (!ok)
	{
		printf("%s: write error\n", argv[0]);
		return false;
	}

	printf("%s: bootblock extracted\n", argv[0]);
	return true;
}

static bool RunCommand (Action action, int argc, char *argv[])
{
	switch (action)
	{
		case ACTION_HELP:
			PrintHelp(argc, argv);
			return true;
		case ACTION_INFO:
			printf("%s:\n\n", diskFileName);
			DIM_DumpInfo(disk);
			return true;
		case ACTION_CREATEDISK:
			return CreateDisk(argc, argv);
		case ACTION_ADD:
			return AddFiles(argc, argv);
		case ACTION_ADDTREE:
			return AddTree(argc, argv);
		case ACTION_DELETE:
			return DeleteFiles(argc, argv);
		case ACTION_EXTRACT:
			return Extract(argc, argv);
		case ACTION_LIST:
			return Dir(argc, argv);
		case ACTION_MKDIR:
			return MkDir(argc, argv);
		case ACTION_CHDIR:
			return ChDir(argc, argv);
		case ACTION_RMDIR:
			return RmDir(argc, argv);
		case ACTION_CAT:
			return Cat(argc, argv, false);
		case ACTION_HEXCAT:
			return Cat(argc, argv, true);
		case ACTION_SETVOL:
			return SetVol(argc, argv);
		case ACTION_SETBOOT:
			return SetBoot(argc, argv);
		case ACTION_GETBOOT:
			return GetBoot(argc, argv);
		case ACTION_INTERACTIVE:
			return RunInteractiveSession();
	}
	printf("Action %d not implemented yet", action);
	return false;
}

static ActionMode GetActionMode(Action action)
{
	switch (action)
	{
		case ACTION_HELP:
			return MODE_NO_DISK;
		case ACTION_CREATEDISK:
			return MODE_NEW_DISK;
		default:
			return MODE_EXISTING_DISK;
	}
}

static bool ActionAllowsOption(Action action, int optionId)
{
	switch (optionId)
	{
		case DSK_OPTION_BRIEF:
		case DSK_OPTION_LOWERCASE:
			return action == ACTION_LIST;
		case DSK_OPTION_ASCII:
			return action == ACTION_EXTRACT || action == ACTION_CAT;
	}
	return false;
}

static bool ExecuteCLICommand(int argc, char* argv[], bool implicitDiskOpen)
{
	char parseError[256];
	CLI_CommandLine commandLine;
	char* parseArgv[CLI_MAX_ARGUMENTS + 1];
	parseArgv[0] = (char*)"dsk";
	for (int i = 0; i < argc && i < CLI_MAX_ARGUMENTS; i++)
		parseArgv[i + 1] = argv[i];

	if (!CLI_ParseCommandLine(argc + 1, parseArgv, actionSpecs, ACTION_LIST, optionSpecs, &commandLine, parseError, sizeof(parseError)))
	{
		printf("Error: %s\n", parseError);
		return false;
	}

	Action action = (Action)commandLine.action;
	ActionMode mode = GetActionMode(action);
	const char** arguments = commandLine.arguments;
	int argumentCount = commandLine.argumentCount;
	const char* localDiskFileName = diskFileName;

	options = 0;
	for (int i = 0; i < commandLine.optionCount; i++)
	{
		int optionId = commandLine.options[i].id;
		if (optionId == DSK_OPTION_HELP)
			continue;
		if (!ActionAllowsOption(action, optionId))
		{
			switch (optionId)
			{
				case DSK_OPTION_BRIEF:
					printf("Error: --brief is only valid with the list action\n");
					break;
				case DSK_OPTION_LOWERCASE:
					printf("Error: --lowercase is only valid with the list action\n");
					break;
				case DSK_OPTION_ASCII:
					printf("Error: --ascii is only valid with the extract or cat actions\n");
					break;
				default:
					printf("Error: Invalid option\n");
					break;
			}
			return false;
		}

		switch (optionId)
		{
			case DSK_OPTION_BRIEF:
				options |= DIR_BRIEF;
				break;
			case DSK_OPTION_LOWERCASE:
				options |= DIR_LOWERCASE;
				break;
			case DSK_OPTION_ASCII:
				options |= TEXTOPT_BASIC;
				break;
		}
	}

	if (CLI_HasOption(&commandLine, DSK_OPTION_HELP))
	{
		PrintHelp(argumentCount, (char**)arguments);
		return true;
	}

	if (implicitDiskOpen)
	{
		if (action == ACTION_CREATEDISK)
		{
			printf("%s: command unavailable during an active session\n", CLI_GetActionName(&commandLine) ? CLI_GetActionName(&commandLine) : "create");
			return true;
		}
		if (action == ACTION_INTERACTIVE)
		{
			printf("Already in interactive mode\n");
			return true;
		}
	}

	if (mode == MODE_EXISTING_DISK)
	{
		if (!implicitDiskOpen)
		{
			if (commandLine.actionName == 0)
			{
				if (argumentCount < 1)
				{
					printf("Missing disk name\n");
					return false;
				}
				localDiskFileName = arguments[0];
				arguments++;
				argumentCount--;
			}
			else
			{
				if (argumentCount < 1)
				{
					printf("Missing disk name\n");
					return false;
				}
				localDiskFileName = arguments[0];
				arguments++;
				argumentCount--;
			}
		}

		if (localDiskFileName == NULL)
		{
			printf("Missing disk name\n");
			return false;
		}

		diskFileName = localDiskFileName;
		if (disk == NULL)
		{
			disk = DIM_OpenDisk(diskFileName);
			if (!disk)
			{
				printf("%s: %s\n", diskFileName, DIM_GetErrorString());
				return false;
			}
		}
	}

	return RunCommand(action, argumentCount, (char**)arguments);
}

static bool PrepareSessionTarget(int argc, char* argv[])
{
	char parseError[256];
	CLI_CommandLine commandLine;
	char* parseArgv[CLI_MAX_ARGUMENTS + 1];
	parseArgv[0] = (char*)"dsk";
	for (int i = 0; i < argc && i < CLI_MAX_ARGUMENTS; i++)
		parseArgv[i + 1] = argv[i];

	if (!CLI_ParseCommandLine(argc + 1, parseArgv, actionSpecs, ACTION_LIST, optionSpecs, &commandLine, parseError, sizeof(parseError)))
	{
		printf("Error: %s\n", parseError);
		return false;
	}

	if (CLI_HasOption(&commandLine, DSK_OPTION_HELP))
	{
		PrintHelp(commandLine.argumentCount, (char**)commandLine.arguments);
		return false;
	}

	if (commandLine.actionName != 0)
	{
		printf("Error: Batch mode only accepts a disk image before --\n");
		return false;
	}

	if (commandLine.argumentCount < 1)
	{
		printf("Missing disk name\n");
		return false;
	}

	diskFileName = commandLine.arguments[0];
	if (disk != NULL)
	{
		DIM_CloseDisk(disk);
		disk = NULL;
	}

	disk = DIM_OpenDisk(diskFileName);
	if (!disk)
	{
		printf("%s: %s\n", diskFileName, DIM_GetErrorString());
		return false;
	}
	return true;
}

typedef struct
{
	bool implicitDiskOpen;
}
DSK_SessionExecutionContext;

static bool ExecuteSessionCommand(int argc, char* argv[], void* context)
{
	DSK_SessionExecutionContext* sessionContext = (DSK_SessionExecutionContext*)context;
	return ExecuteCLICommand(argc, argv, sessionContext->implicitDiskOpen);
}

static bool RunSessionCommands(int argc, char* argv[], bool implicitDiskOpen)
{
	char errorBuffer[256];
	DSK_SessionExecutionContext context = { implicitDiskOpen };

	if (argc == 1 && argv[0][0] == '@' && argv[0][1] != 0)
	{
		if (!Session_ExecuteCommandFile(argv[0] + 1, "::", ExecuteSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
		{
			printf("Error: %s\n", errorBuffer);
			return false;
		}
		return true;
	}

	if (!Session_ExecuteTokenStream(argc, argv, "::", ExecuteSessionCommand, &context, errorBuffer, sizeof(errorBuffer)))
	{
		printf("Error: %s\n", errorBuffer);
		return false;
	}
	return true;
}

static bool RunCommandLine (int argc, char *argv[])
{
	if (argc < 1)
	{
		PrintHelp(argc, argv);
		return true;
	}

	int separatorIndex = -1;
	for (int n = 0; n < argc; n++)
	{
		if (strcmp(argv[n], "--") == 0)
		{
			separatorIndex = n;
			break;
		}
	}

	if (separatorIndex >= 0)
	{
		if (separatorIndex == 0)
		{
			printf("Error: Missing disk name before session commands\n");
			return false;
		}

		if (!PrepareSessionTarget(separatorIndex, argv))
			return false;

		if (separatorIndex + 1 >= argc)
		{
			printf("Error: Missing session command after --\n");
			return false;
		}
		return RunSessionCommands(argc - separatorIndex - 1, argv + separatorIndex + 1, true);
	}

	if (argc >= 2 && argv[argc - 1][0] == '@' && argv[argc - 1][1] != 0)
	{
		if (!PrepareSessionTarget(argc - 1, argv))
			return false;
		return RunSessionCommands(1, argv + argc - 1, true);
	}

	return ExecuteCLICommand(argc, argv, false);
}

int main (int argc, char *argv[])
{
	int value = RunCommandLine(--argc, ++argv) ? 0 : 1;
	if (disk != NULL)
		DIM_CloseDisk(disk);
	return value;
}
