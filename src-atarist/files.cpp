#include <os_file.h>
#include <os_lib.h>

#ifdef _ATARIST

#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include <mint/ostruct.h>

struct FindFileInternal
{
	_DTA dta;
	bool active;
};

typedef char FindFileInternalFitsInResults[
	sizeof(FindFileInternal) <= sizeof(((FindFileResults*)0)->internalData) ? 1 : -1
];

static void NormalizeSearchPattern(const char* pattern, char* output, size_t outputSize)
{
	if (output == 0 || outputSize == 0)
		return;

	if (pattern == 0 || pattern[0] == 0)
		pattern = "*";

	StrCopy(output, outputSize, pattern);
	for (char* p = output; *p; ++p)
	{
		if (*p == '/')
			*p = '\\';
	}

	char* base = output;
	for (char* p = output; *p; ++p)
	{
		if (*p == '\\' || *p == ':')
			base = p + 1;
	}

	if (StrComp(base, "*") == 0)
	{
		StrCopy(base, outputSize - (uint32_t)(base - output), "*.*");
		return;
	}

	if (base[0] == '.')
	{
		size_t baseLen = StrLen(base);
		if ((size_t)(base - output) + baseLen + 2 <= outputSize)
		{
			MemMove(base + 1, base, baseLen + 1);
			base[0] = '*';
		}
	}
}

static int MapTOSAttributes(char tosAttributes)
{
	int attributes = 0;
	if (tosAttributes & FA_DIR)
		attributes |= FileAttribute_Directory;
	if (tosAttributes & FA_HIDDEN)
		attributes |= FileAttribute_Hidden;
	if (tosAttributes & FA_SYSTEM)
		attributes |= FileAttribute_System;
	if (tosAttributes & FA_CHANGED)
		attributes |= FileAttribute_Archive;
	if (tosAttributes & FA_RDONLY)
		attributes |= FileAttribute_ReadOnly;
	return attributes;
}

void FillFindFileResults (FindFileResults* results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	StrCopy(results->fileName, sizeof(results->fileName), i->dta.dta_name);
	results->attributes = MapTOSAttributes(i->dta.dta_attribute);
	results->fileSize = (uint32_t)i->dta.dta_size;
	results->modifyTime = ((uint32_t)i->dta.dta_date << 16) | i->dta.dta_time;
	results->description[0] = 0;
}

bool OS_FindFirstFile(const char *pattern, FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;
	char search[FILE_MAX_PATH];
	NormalizeSearchPattern(pattern, search, sizeof(search));

	i->active = false;

	_DTA* oldDTA = Fgetdta();
	Fsetdta(&i->dta);
	long result = Fsfirst(search, FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIR);
	Fsetdta(oldDTA);
	if (result != 0)
		return false;

	i->active = true;
	FillFindFileResults(results);
	return true;
}

bool OS_FindNextFile(FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)&results->internalData;

	if (!i->active)
		return false;

	_DTA* oldDTA = Fgetdta();
	Fsetdta(&i->dta);
	long result = Fsnext();
	Fsetdta(oldDTA);
	if (result != 0)
	{
		i->active = false;
		return false;
	}

	FillFindFileResults(results);
	return true;
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
