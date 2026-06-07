#pragma once

#include <os_types.h>
#include <os_lib.h>
#include <ddb.h>

struct DC_String
{
	const uint8_t* ptr;
	const uint8_t* end;

	DC_String()
		: ptr(0), end(0)
	{
	}

	DC_String(const char* text)
		: ptr((const uint8_t*)text),
		  end(text ? (const uint8_t*)text + StrLen(text) : (const uint8_t*)text)
	{
	}

	DC_String(const uint8_t* start, size_t size)
		: ptr(start), end(start + size)
	{
	}

	DC_String(const uint8_t* start, const uint8_t* finish)
		: ptr(start), end(finish)
	{
	}
};

struct DC_Buffer
{
	uint8_t* ptr;
	uint8_t* end;

	DC_Buffer(uint8_t* start, size_t size)
		: ptr(start), end(start + size)
	{
	}

	DC_Buffer(uint8_t* start, uint8_t* finish)
		: ptr(start), end(finish)
	{
	}
};

enum DC_Error
{
	DCError_None,
	DCError_FileNotFound,
	DCError_ReadError,
	DCError_WriteError,
	DCError_SyntaxError,
	DCError_SemanticError,
	DCError_Unsupported,
	DCError_OutOfMemory
};

enum DC_Charset
{
	DCCharset_Auto,
	DCCharset_UTF8,
	DCCharset_CP437
};

struct DC_CharTranslation
{
	uint32_t unicode;
	uint8_t  code;
};

struct DC_CompilerOptions
{
	DDB_Version  version;
	DDB_Machine  target;
	DDB_Language language;
	DC_Charset   sourceCharset;

	const char** includePaths;
	size_t       includePathCount;
	const char** defines;
	size_t       defineCount;
	const DC_CharTranslation* translations;
	size_t       translationCount;

	bool         dumpPreprocessed;
	bool         strict;
};

struct DC_Compilation
{
	uint8_t* data;
	size_t   size;
};

extern void                 DC_SetError          (DC_Error error, const char* message = 0);
extern DC_Error             DC_GetError          ();
extern int                  DC_GetErrorCount     ();
extern const char*          DC_GetErrorString    ();
extern const char*          DC_GetErrorDetails   ();
extern const DC_Compilation* DC_Compile          (const char* file, const DC_CompilerOptions* opts);
extern void                 DC_FreeCompilation   (const DC_Compilation* compilation);
