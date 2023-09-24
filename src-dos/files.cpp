#ifdef _DOS

#include <os_file.h>
#include <os_lib.h>

#include <stdio.h>
#include <direct.h>

struct FindFileInternal
{
	DIR *dirp;
	struct dirent *direntp;
};

void FillFindFileReults (FindFileResults* results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	StrCopy(results->fileName, sizeof(results->fileName), i->direntp->d_name);
	results->attributes = 0;
	results->fileSize = 0;
	results->description[0] = 0;
}

bool File_FindFirst(const char *pattern, FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	i->dirp = opendir(".");
	return File_FindNext(results);
}

bool File_FindNext(FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	if (i->dirp == 0)
		return false;

	for (;;)
	{
		i->direntp = readdir(i->dirp);
		if (i->direntp == 0)
			break;
		FillFindFileReults(results);
		return true;
	}
	closedir(i->dirp);
	i->dirp = 0;
	return false;
}

#endif