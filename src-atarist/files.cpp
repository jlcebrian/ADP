#include <os_file.h>
#include <os_lib.h>

#ifdef _ATARIST

#include <stdio.h>
#include <sys/dirent.h>
#include <string.h>
#include <osbind.h>

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

bool OS_GetCurrentDirectory(char* buffer, size_t bufferSize)
{
	if (buffer == 0 || bufferSize == 0)
		return false;

	char path[FILE_MAX_PATH];
	if (Dgetpath(path, 0) != 0)
		return false;

	char drive = (char)('A' + Dgetdrv());
	size_t pos = 0;
	if (bufferSize < 4)
		return false;
	buffer[pos++] = drive;
	buffer[pos++] = ':';

	if (path[0] == 0)
	{
		buffer[pos++] = '\\';
		buffer[pos] = 0;
		return true;
	}
	if (path[0] != '\\')
		buffer[pos++] = '\\';

	for (const char* p = path; *p; ++p)
	{
		if (pos + 1 >= bufferSize)
			return false;
		buffer[pos++] = *p;
	}
	buffer[pos] = 0;
	return true;
}

bool OS_ChangeDirectory(const char* path)
{
	if (path == 0 || path[0] == 0)
		return false;

	char temp[FILE_MAX_PATH];
	StrCopy(temp, sizeof(temp), path);
	for (char* p = temp; *p; ++p)
		if (*p == '/')
			*p = '\\';

	char* dirPath = temp;
	if (temp[1] == ':')
	{
		char drive = temp[0];
		if (drive >= 'a' && drive <= 'z')
			drive -= 'a' - 'A';
		if (drive < 'A' || drive > 'Z')
			return false;
		if (Dsetdrv(drive - 'A') < 0)
			return false;
		dirPath = temp + 2;
		if (*dirPath == 0)
			return true;
	}

	return Dsetpath(dirPath) == 0;
}

#endif
