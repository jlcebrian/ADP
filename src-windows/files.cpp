#include <os_file.h>
#include <os_lib.h>

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define  _WIN32_WINNT   0x0A00
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>
#include <stdlib.h>

struct FindFileInternal
{
	HANDLE 				handle;
	WIN32_FIND_DATA*	data;
};

void FillFindResults(FindFileResults* results)
{
	WIN32_FIND_DATA* data = ((FindFileInternal*)results->internalData)->data;

	StrCopy(results->fileName, sizeof(results->fileName), data->cFileName);
	results->fileSize = data->nFileSizeLow;
	if (data->nFileSizeHigh)
		results->fileSize = 0xFFFFFFFFu;
	results->attributes = 0;
	if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		results->attributes |= FileAttribute_Directory;
	if (data->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
		results->attributes |= FileAttribute_Hidden;
	if (data->dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		results->attributes |= FileAttribute_ReadOnly;
	if (data->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
		results->attributes |= FileAttribute_System;
	if (data->dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)
		results->attributes |= FileAttribute_Archive;
	results->description[0] = 0;
}

bool OS_FindFirstFile(const char *pattern, FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)results->internalData;
	i->data = (WIN32_FIND_DATA*)malloc(sizeof(WIN32_FIND_DATA));
	memset(i->data, 0, sizeof(WIN32_FIND_DATA));
	i->handle = FindFirstFile(pattern, i->data);
	if (i->handle == INVALID_HANDLE_VALUE)
	{
		free(i->data);
		return false;
	}

	FillFindResults(results);
	return true;
}

bool OS_FindNextFile(FindFileResults *results)
{
	FindFileInternal* i = (FindFileInternal*)results->internalData;
	if (!FindNextFile(i->handle, i->data))
	{
		free(i->data);
		i->data = 0;
		return false;
	}

	FillFindResults(results);
	return true;
}

bool OS_GetCurrentDirectory(char* buffer, size_t bufferSize)
{
	if (buffer == NULL || bufferSize == 0)
		return false;
	DWORD len = GetCurrentDirectory((DWORD)bufferSize, buffer);
	return len > 0 && len < bufferSize;
}

bool OS_ChangeDirectory(const char* path)
{
	if (path == NULL)
		return false;
	return SetCurrentDirectory(path) != 0;
}

#endif