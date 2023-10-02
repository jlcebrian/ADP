#include <os_file.h>
#include <os_lib.h>

#ifdef _ATARIST

#include <stdio.h>
#include <sys/dirent.h>

struct FindFileInternal
{
	DIR *dirp;
	struct dirent *direntp;
};

void FillFindFileResults (FindFileResults* results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	StrCopy(results->fileName, sizeof(results->fileName), i->direntp->d_name);
	results->attributes = 0;
	results->fileSize = 0;
	results->description[0] = 0;
}

bool OS_FindFirstFile(const char *pattern, FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	i->dirp = opendir(".");
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
		return true;
	}
	closedir(i->dirp);
	i->dirp = 0;
	return false;
}

#endif