#ifdef _DOS

#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>

#include <stdio.h>
#include <direct.h>
#include <string.h>

#define MAX_FILES 128
#define MAX_NAME  16

static const char** files;
static int count;
static int selected = -1;

static void init()
{
}

static void error(const char* message)
{
	VID_Finish();

	printf("Error: %s\n", message);
	exit(1);
}

static const char** FindFilesWithExtension(const char* extension)
{
    DIR *dirp;
    struct dirent *direntp;

	char *buffer = Allocate<char>("Find Files", MAX_FILES * MAX_NAME + sizeof(const char*)*(MAX_FILES+1));
	char **fileNames = (char **)buffer;
	char *ptr = (char *)(fileNames + MAX_FILES + 1);
	int numFiles = 0;

    dirp = opendir(".");
	if (dirp != NULL)
	{
		for (;;)
		{
			direntp = readdir(dirp);
			if (direntp == NULL)
				break;
			const char *ext = strrchr(direntp->d_name, '.');
			if (ext && stricmp(ext, extension) == 0)
			{
				if (numFiles >= MAX_FILES)
				{
					fileNames[numFiles-1] = 0;
					return (const char**)fileNames;
				}
				fileNames[numFiles] = ptr;
				strcpy(ptr, direntp->d_name);
				ptr += strlen(direntp->d_name) + 1;
				numFiles++;
			}
		}
		closedir(dirp);
	}

	fileNames[numFiles] = NULL;
	*ptr = 0;
	return (const char**)fileNames;
}

static void ShowLoaderPrompt(int parts, DDB_Language language)
{
	static char* messageSP[2] = {
		"  \x12Qu\x16 parte quieres  ",
		"     cargar (1-2)?    ",
	};
	static char* messageEN[2] = {
		"   Which part do you  ",
		"  want to load (1-2)? ",
	};
	char** message = (language == DDB_SPANISH) ? messageSP : messageEN;
	char* number = strchr(message[1], '2');
	if (number != NULL)
		*number = '0' + parts;

	VID_Clear(88, 80, 136, 32, 0);
	for (int y = 0; y < 2; y++)
	{
		for (int x = 0; message[y][x] != 0; x++)
			VID_DrawCharacter(88 + x * 6, 88 + y * 8, message[y][x], 0x0F, 0);
	}
}

static void WaitForKey(int elapsed)
{
	if (VID_AnyKey())
		VID_Quit();
}

static void LoaderScreenUpdate(int elapsed)
{
	if (VID_AnyKey())
	{
		uint8_t key, ext;
		VID_GetKey(&key, &ext, NULL);
		if (key == 27)
			VID_Quit();
		else if (key >= '1' && key <= '0' + count)
		{
			selected = key - '1';
			VID_Quit();
		}			
	}
}

extern "C" int main (int argc, char**argv)
{
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	return 1;
}

#endif