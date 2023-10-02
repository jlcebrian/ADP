#ifdef _UNIX

#include <os_file.h>
#include <os_lib.h>

#include <stdio.h>
#include <dirent.h>

struct FindFileInternal
{
	DIR *dirp;
	struct dirent *direntp;
	char path[FILE_MAX_PATH];
};

void FillFindFileResults (FindFileResults* results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	StrCopy(results->fileName, sizeof(results->fileName), i->path);
	StrCat(results->fileName, sizeof(results->fileName), i->direntp->d_name);
	results->attributes = 0;
	results->fileSize = 0;
	results->description[0] = 0;
}

bool OS_FindFirstFile(const char *pattern, FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	const char* folder = StrRChr(pattern, '/');
	if (folder != 0)
	{
		StrCopy(i->path, folder-pattern, pattern);
		i->dirp = opendir(i->path);
		StrCat(i->path, FILE_MAX_PATH, "/");
	}
	else
	{
		i->path[0] = 0;
		i->dirp = opendir(".");
	}
	return OS_FindNextFile(results);
}

bool OS_FindNextFile(FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	if (i->dirp == 0)
		return false;

	for (;;)
	{
		i->direntp = readdir(i->dirp);
		if (i->direntp == 0)
			break;

		FillFindFileResults(results);
		//printf("File found: %s\n", results->fileName);
		return true;
	}
	closedir(i->dirp);
	i->dirp = 0;
	return false;
}

#endif
