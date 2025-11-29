#include <dim.h>
#include <os_file.h>
#include <os_mem.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
	{ NULL }
};

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

static void PrintHelp()
{
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
	printf("    chdir     Change current directory (interactive use)\n");
	printf("    help      Show extended command help\n");
	printf("\n");
}

static bool CheckExtension(const char* filename, const char* ext)
{
	const char* p = strrchr(filename, '.');
	if (!p)
		return false;
	return stricmp(p + 1, ext) == 0;
}

void ParseOptions (int *argc, char *argv[], const char* config)
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

bool MkDir (int argc, char *argv[])
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

bool ChDir (int argc, char *argv[])
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

bool RmDir (int argc, char *argv[])
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

bool Cat (int argc, char *argv[], bool hexMode)
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

bool Dir (int argc, char* argv[])
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
					printf("%-31s <DIR>            %s\n", result.fileName, result.description);
				dirCount++;
			}
			else
			{
				if (options & DIR_BRIEF)
					printf("%s\n", result.fileName);
				else
					printf("%-31s        %8d  %s\n", result.fileName, result.fileSize, result.description);
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

bool CreateDisk(int argc, char *argv[])
{
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

	int size = 0;
	disk = DIM_CreateDisk(diskFileName, size);
	if (!disk)
	{
		printf("%s: %s\n", diskFileName, DIM_GetErrorString());
		return false;
	}
	printf("%s created\n", diskFileName);
	return true;
}

bool DeleteFiles (int argc, char *argv[])
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

bool AddFiles (int argc, char *argv[])
{
	uint8_t* buffer = NULL;
	size_t bufferSize = 0;
	bool ok = true;

	for (; argc > 0; argc--, argv++)
	{
		File* file = File_Open(argv[0]);
		if (file == NULL)
		{
			printf("%s: file not found\n", argv[0]);
			ok = false;
			continue;
		}
		uint64_t size = File_GetSize(file);
		uint64_t freeSpace = DIM_GetFreeSpace(disk);
		if (freeSpace < size)
		{
			printf("%s: no enough space left on disk\n", argv[0]);
			File_Close(file);
			ok = false;
			continue;
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
			printf("%s: error reading file\n", argv[0]);
			File_Close(file);
			continue;
		}
		File_Close(file);

		const char* ptr = argv[0];
		const char* base = ptr;
		while (*ptr != 0)
		{
			if (*ptr == '/' || *ptr == '\\' || *ptr == ':')
				base = ptr + 1;
			ptr++;
		}
		if (!DIM_WriteFile(disk, base, buffer, (uint32_t)size))
			printf("%s: %s\n", base, DIM_GetErrorString());
		else
			printf("%s: %d bytes written\n", base, (uint32_t)size);
	}
	return ok;
}

bool Extract (int argc, char *argv[])
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

bool RunCommand (Action action, int argc, char *argv[])
{
	switch (action)
	{
		case ACTION_HELP:
			PrintHelp();
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
	}
	printf("Action %d not implemented yet", action);
	return false;
}

bool RunCommandLine (int argc, char *argv[])
{
	if (argc < 1)
	{
		PrintHelp();
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