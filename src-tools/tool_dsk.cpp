#include <dim.h>
#include <os_file.h>
#include <os_mem.h>

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
	ACTION_INTERACTIVE,
}
Action;

typedef enum
{
	DIR_BRIEF     = 0x01,
	DIR_LOWERCASE = 0x02,
}
DirOptions;

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
	{ "ls",       ACTION_LIST,       MODE_EXISTING_DISK, "BL", DIR_BRIEF },
	{ "dir",      ACTION_LIST,       MODE_EXISTING_DISK, "BL" },
	{ "i",        ACTION_INFO,       MODE_EXISTING_DISK },
	{ "info",     ACTION_INFO,       MODE_EXISTING_DISK },
	{ "c",        ACTION_CREATEDISK, MODE_NEW_DISK      },
	{ "mkdisk",   ACTION_CREATEDISK, MODE_NEW_DISK      },
	{ "a",        ACTION_ADD,        MODE_EXISTING_DISK },
	{ "add",      ACTION_ADD,        MODE_EXISTING_DISK },
	{ "rm",       ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "del",      ACTION_DELETE,     MODE_EXISTING_DISK },
	{ "x",        ACTION_EXTRACT,    MODE_EXISTING_DISK },
	{ "extract",  ACTION_EXTRACT,    MODE_EXISTING_DISK },
	{ "cd",       ACTION_CHDIR,      MODE_EXISTING_DISK },
	{ "chdir",    ACTION_CHDIR,      MODE_EXISTING_DISK },
	{ "md",       ACTION_MKDIR,      MODE_EXISTING_DISK },
	{ "mkdir",    ACTION_MKDIR,      MODE_EXISTING_DISK },
	{ "rd",       ACTION_RMDIR,      MODE_EXISTING_DISK },
	{ "rmdir",    ACTION_RMDIR,      MODE_EXISTING_DISK },
	{ "cat",      ACTION_CAT,        MODE_EXISTING_DISK },
	{ "type",     ACTION_CAT,        MODE_EXISTING_DISK },
	{ "hex",      ACTION_HEXCAT,     MODE_EXISTING_DISK },
	{ "dump",     ACTION_HEXCAT,     MODE_EXISTING_DISK },
	{ "setvol",   ACTION_SETVOL,     MODE_EXISTING_DISK },
	{ "label",    ACTION_SETVOL,     MODE_EXISTING_DISK },
	{ "shell",    ACTION_INTERACTIVE, MODE_EXISTING_DISK },
	{ "i",        ACTION_INTERACTIVE, MODE_EXISTING_DISK },
	{ NULL }
};

static bool RunInteractiveSession();
static bool ExecuteInteractiveLine(char* line, bool* keepRunning);
static int  TokenizeLine(char* line, char* argv[], int maxArgs);
static void TrimLine(char* line);
static bool HandleInteractiveBuiltins(int argc, char* argv[], bool* keepRunning);
static bool RunInteractiveCommand(int argc, char* argv[]);
static void HostPrintCurrentDirectory();
static bool HostListDirectory(const char* pattern);
static bool HostChangeDirectoryCommand(const char* path);
static void ParseOptions (int *argc, char *argv[], const char* config);
static bool RunCommand (Action action, int argc, char *argv[]);

static bool AddFiles (int argc, char *argv[]);
static bool Extract  (int argc, char *argv[]);

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
                        printf("Usage: dsk [disk.img] dir [/B] [/L] [pattern]\n\n");
                        printf("    /B    Brief listing\n");
                        printf("    /L    Lowercase filenames\n");
                        return;
                    case ACTION_CREATEDISK:
                        printf("Usage: dsk mkdisk disk.img\n\n");
                        printf("    Create a new empty disk image\n");
                        printf("    The disk format is chosen based on the file extension:\n");
                        printf("      .adf  - Amiga Disk File (ADF)\n");
                        printf("      .dsk  - FAT Disk Image (FAT12)\n");
                        return;
                    case ACTION_INFO:
                        printf("Usage: dsk info disk.img\n\n");
                        printf("    Show information about the given disk image\n");
                        return;
					case ACTION_ADD:
						printf("Usage: dsk add disk.img hostfile [hostfile2 ...]\n\n");
						printf("    Copy one or more host files into the disk image.\n");
						printf("    Directory paths in hostfile are stripped inside the image.\n");
						return;
					case ACTION_DELETE:
						printf("Usage: dsk del disk.img pattern [pattern2 ...]\n\n");
						printf("    Delete files that match the supplied patterns. Wildcards are allowed.\n");
						return;
					case ACTION_EXTRACT:
						printf("Usage: dsk extract disk.img [pattern]\n\n");
						printf("    Copy files from the disk image to the host directory.\n");
						printf("    If no pattern is provided all files are extracted.\n");
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
						printf("Usage: dsk cat disk.img pattern\n\n");
						printf("    Print the contents of files that match the pattern to stdout.\n");
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
					case ACTION_INTERACTIVE:
						printf("Usage: dsk shell disk.img\n\n");
						printf("    Open the disk image and enter an interactive shell.\n");
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
        {
            printf("Unknown command: %s\n\n", command);
        }
    }

	printf("Disk file utility for DAAD " VERSION_STR "\n\n");
	printf("Usage: dsk [disk.img] command [options]\n\n");
	printf("Available commands:\n\n");
	printf("    dir       List contents of disk image (default)\n");
	printf("    mkdisk    Create new disk image\n");
	printf("    info      Show information about disk image\n");
	printf("    add       Add files to disk image\n");
	printf("    extract   Extract files from disk image\n");
	printf("    del       Delete files from disk image\n");
	printf("    rmdir     Delete an empty directory from disk image\n");
	printf("    mkdir     Make a directory in disk image\n");
	printf("    chdir     Change current directory\n");
	printf("    setvol    Set or clear the volume label\n");
	printf("    shell     Open an interactive shell\n");
	printf("    help      Show extended command help\n");
	printf("\nAdditional interactive-only commands: LDIR, LCD/LCHDIR, PUT, GET, EXIT\n");
	printf("\n");
}

static void TrimLine(char* line)
{
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = 0;
}

static int TokenizeLine(char* line, char* argv[], int maxArgs)
{
	int argc = 0;
	char* ptr = line;
	while (*ptr != 0)
	{
		while (*ptr && isspace((unsigned char)*ptr))
			ptr++;
		if (!*ptr)
			break;
		if (argc >= maxArgs)
			break;
		char quote = 0;
		if (*ptr == '"' || *ptr == '\'')
		{
			quote = *ptr++;
		}
		argv[argc++] = ptr;
		while (*ptr)
		{
			if (quote)
			{
				if (*ptr == quote)
				{
					*ptr++ = 0;
					break;
				}
			}
			else if (isspace((unsigned char)*ptr))
			{
				*ptr++ = 0;
				break;
			}
			ptr++;
		}
	}
	return argc;
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
	{
		printf("No host files match %s\n", search);
		return false;
	}
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
	for (int n = 0; commands[n].command != NULL; n++)
	{
		if (stricmp(commands[n].command, argv[0]) == 0)
		{
			if (commands[n].action == ACTION_INTERACTIVE)
			{
				printf("Already in interactive mode\n");
				return true;
			}
			if (commands[n].mode == MODE_NEW_DISK)
			{
				printf("%s: command unavailable during an active session\n", argv[0]);
				return true;
			}
			int localArgc = argc;
			char* localArgv[INTERACTIVE_MAX_ARGS];
			for (int i = 0; i < argc && i < INTERACTIVE_MAX_ARGS; i++)
				localArgv[i] = argv[i];
			if (commands[n].options)
				ParseOptions(&localArgc, localArgv, commands[n].options);
			localArgc--;
			if (localArgc < 0)
				localArgc = 0;
			return RunCommand(commands[n].action, localArgc, localArgv + 1);
		}
	}
	printf("%s: Unknown command\n", argv[0]);
	return true;
}

static bool HandleInteractiveBuiltins(int argc, char* argv[], bool* keepRunning)
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

static bool ExecuteInteractiveLine(char* line, bool* keepRunning)
{
	char* argv[INTERACTIVE_MAX_ARGS];
	int argc = TokenizeLine(line, argv, INTERACTIVE_MAX_ARGS);
	if (argc == 0)
		return true;
	if (HandleInteractiveBuiltins(argc, argv, keepRunning))
		return true;
	return RunInteractiveCommand(argc, argv);
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

static void ParseOptions (int *argc, char *argv[], const char* config)
{
	for (int n = 0; n < *argc; n++)
	{
		if (argv[n][0] == '/')
		{
			for (int i = 1; argv[n][i]; i++)
			{
				for (int c = 0; ; c++)
				{
					if (!config[c])
					{
						printf("Unknown option \"%c\"\n", config[c]);
						break;
					}
					if (ToUpper(argv[n][i]) == ToUpper(config[c]))
					{
						if (argv[n][i + 1] == '-')
						{
							options &= ~(1 << c);
							i++;
						}
						else
						{
							options |= (1 << c);
						}
						break;
					}
				}
			}
			for (int i = n+1; i < *argc; i++)
				argv[i-1] = argv[i];
			(*argc) -= 1;
		}
	}
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

static bool Cat (int argc, char *argv[], bool hexMode)
{
	const char* pattern = NULL;
	FindFileResults result;
	int fileCount = 0;
	bool singleFile = false;
	uint32_t bufferSize = 0;
	uint8_t* buffer = NULL;

	if (argc > 0)
		pattern = argv[0];
	if (pattern != NULL)
	{
		if (strchr(pattern, '?') == NULL && strchr(pattern, '*') == NULL)
		{
			singleFile = true;
			if (DIM_ChangeDirectory(disk, pattern))
			{
				argc--, argv++;
				pattern = argc > 0 ? argv[0] : "*";
			}
		}
		char* pathSeparator = (char *)strrchr(pattern, '\\');
		if (pathSeparator != NULL)
		{
			*pathSeparator = 0;
			if (pathSeparator > pattern && !DIM_ChangeDirectory(disk, pattern))
			{
				printf("Error: Cannot change directory to %s\n", pattern);
				return false;
			}
			pattern = pathSeparator + 1;
		}
	}

	if (DIM_FindFirstFile(disk, &result, pattern))
	{
		do
		{
			if (!singleFile)
				printf("\n%s (%u bytes)\n\n", result.fileName, result.fileSize);

			if (!(result.attributes & FileAttribute_Directory))
			{
				uint32_t size = result.fileSize;
				if (bufferSize < size)
				{
					buffer = (uint8_t*)realloc(buffer, size);
					bufferSize = size;
					if (buffer == NULL)
					{
						DIM_SetError(DIMError_OutOfMemory);
						Free(buffer);
						return false;
					}
				}
				if (!DIM_ReadFile(disk, result.fileName, buffer, bufferSize))
					printf("%s: %s\n", result.fileName, DIM_GetErrorString());
				else
				{
					fileCount++;
					if (hexMode)
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
					return false;
				}
			}
		}
		while (DIM_FindNextFile(disk, &result));
	}

	if (fileCount == 0)
		printf("File not found\n");
	if (buffer)
		Free(buffer);
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
		if (strchr(pattern, '?') == NULL && strchr(pattern, '*') == NULL)
		{
			if (DIM_ChangeDirectory(disk, pattern))
			{
				argc--, argv++;
				pattern = argc > 0 ? argv[0] : "*";
			}
		}
		char* pathSeparator = (char *)strrchr(pattern, '\\');
		if (pathSeparator != NULL)
		{
			*pathSeparator = 0;
			if (pathSeparator > pattern && !DIM_ChangeDirectory(disk, pattern))
			{
				printf("Error: Cannot change directory to %s\n", pattern);
				return false;
			}
			pattern = pathSeparator + 1;
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
						struct tm* t = localtime(&result.modifyTime);
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
						struct tm* t = localtime(&result.modifyTime);
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

	disk = DIM_CreateDisk(diskFileName, size);
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

static bool AddFile (const char* filename)
{
	static size_t bufferSize = 0;
	static uint8_t* buffer = NULL;

	File* file = File_Open(filename);
	if (file == NULL)
	{
		printf("%s: file not found\n", filename);
		return false;
	}
	uint64_t size = File_GetSize(file);
	uint64_t freeSpace = DIM_GetFreeSpace(disk);
	if (freeSpace < size)
	{
		printf("%s: no enough space left on disk\n", filename);
		File_Close(file);
		return false;
	}
	if (bufferSize < size)
	{
		bufferSize = size;
		buffer = (uint8_t*)realloc(buffer, bufferSize);
		if (buffer == NULL)
		{
			printf("Out of memory\n");
			File_Close(file);
			return false;
		}
	}
	if (File_Read(file, buffer, size) != size)
	{
		printf("%s: error reading file\n", filename);
		File_Close(file);
		return false;
	}
	File_Close(file);

	const char* ptr = filename;
	const char* base = ptr;
	while (*ptr != 0)
	{
		if (*ptr == '/' || *ptr == '\\' || *ptr == ':')
			base = ptr + 1;
		ptr++;
	}
	if (!DIM_WriteFile(disk, base, buffer, (uint32_t)size))
    {
		printf("%s: %s\n", base, DIM_GetErrorString());
        return false;
    }
	else
    {
		printf("%s: %d bytes written\n", base, (uint32_t)size);
        return true;
    }
}

static bool AddFiles (int argc, char *argv[])
{
	bool ok = true;

	for (; argc > 0; argc--, argv++)
	{
        if (strchr(argv[0], '*') != NULL || strchr(argv[0], '?') != NULL)
        {
            FindFileResults results;
            char *dirSep = strrchr(argv[0], '/');
            if (!dirSep)
                dirSep = strrchr(argv[0], '\\');
            if (dirSep)
            {
                char savedChar = *dirSep;
                *dirSep = 0;
                if (!OS_ChangeDirectory(argv[0]))
                {
                    printf("%s: directory not found\n", argv[0]);
                    ok = false;
                    *dirSep = savedChar;
                    continue;
                }
                *dirSep = savedChar;
                argv[0] = dirSep + 1;
            }
            if (OS_FindFirstFile(argv[0], &results))
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
                printf("%s: file not found\n", argv[0]);
                ok = false;
            }
            continue;
        }

        AddFile(argv[0]);
	}
	return ok;
}

static bool Extract (int argc, char *argv[])
{
	uint8_t* buffer = NULL;
	uint32_t bufferSize = 0;
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

				if (bufferSize < result.fileSize)
				{
					bufferSize = result.fileSize;
					buffer = (uint8_t*)realloc(buffer, bufferSize);
					if (buffer == NULL)
					{
						printf("Error: Out of memory\n");
						return 1;
					}
				}
				if (result.attributes & FileAttribute_Directory)
				{
					// TODO: Extract subdirectory
				}
				else
				{
					uint32_t read = DIM_ReadFile(disk, result.fileName, buffer, bufferSize);
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
		case ACTION_INTERACTIVE:
			return RunInteractiveSession();
	}
	printf("Action %d not implemented yet", action);
	return false;
}

static bool RunCommandLine (int argc, char *argv[])
{
	if (argc < 1)
	{
		PrintHelp(argc, argv);
		return true;
	}

	for (int n = 0; n < argc; n++)
	{
		char* ptr = argv[n];
		while (*ptr && *ptr != ';') ptr++;
		if (*ptr == ';')
		{
			*ptr = 0;
			if (ptr > argv[n] || n > 0)
			{
				if (!RunCommandLine(ptr > argv[n] ? n + 1 : n, argv))
					return false;
			}
			argv += n;
			argc -= n;
			ptr++;
			if (*ptr == 0)
				argc--, argv++;
			if (argc > 0)
				return RunCommandLine(argc, argv);
			return true;
		}
	}

	while (true)
	{
		for (int n = 0; commands[n].command; n++)
		{
			if (stricmp(commands[n].command, argv[0]) == 0)
			{
				options = commands[n].defaultOptions;
				if (commands[n].options)
					ParseOptions(&argc, argv, commands[n].options);
				if (commands[n].mode != MODE_NO_DISK)
				{
					if (diskFileName == NULL && argc > 1)
					{
						diskFileName = argv[1];
						argc--, argv++;
					}
					if (diskFileName == NULL)
					{
						printf("Missing disk name\n");
						return false;
					}
					if (commands[n].mode == MODE_EXISTING_DISK)
					{
						disk = DIM_OpenDisk(diskFileName);
						if (!disk)
						{
							printf("%s: %s\n", diskFileName, DIM_GetErrorString());
							return false;
						}
					}
				}
				argc--, argv++;
				return RunCommand(commands[n].action, argc, argv);
			}
		}
		if (diskFileName == NULL)
		{
			diskFileName = argv[0];
			argc--, argv++;
			if (argc == 0)
			{
				disk = DIM_OpenDisk(diskFileName);
				if (!disk)
				{
					printf("%s: %s\n", diskFileName, DIM_GetErrorString());
					return false;
				}
				return RunCommand(ACTION_LIST, 0, argv);
			}
			continue;
		}
		break;
	}

	printf("%s: Unknown command\n", argv[0]);
	return false;
}

int main (int argc, char *argv[])
{
	int value = RunCommandLine(--argc, ++argv) ? 0 : 1;
	if (disk != NULL)
		DIM_CloseDisk(disk);
	return value;
}