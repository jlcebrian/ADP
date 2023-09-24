#pragma once

#include <os_types.h>
#include <os_mem.h>
#include <ddb.h>

#define MAX_INCLUDE_STACK 16

enum DC_Error
{
	DCError_None,
	DCError_FileNotFound,
	DCError_ReadError,
	DCError_WriteError,
};

struct DC_String
{
	const uint8_t* ptr;
	const uint8_t* end;

	DC_String(const char* ptr)
	{
		this->ptr = (const uint8_t*)ptr;
		this->end = this->ptr + StrLen(ptr);
	}

	DC_String(const uint8_t* ptr, size_t size)
	{
		this->ptr = ptr;
		this->end = ptr + size;
	}

	DC_String(const uint8_t* ptr, const uint8_t* end)
	{
		this->ptr = ptr;
		this->end = end;
	}
};

struct DC_Buffer
{
	uint8_t* ptr;
	uint8_t* end;

	DC_Buffer(uint8_t* ptr, size_t size)
	{
		this->ptr = ptr;
		this->end = ptr + size;
	}

	DC_Buffer(uint8_t* ptr, uint8_t* end)
	{
		this->ptr = ptr;
		this->end = end;
	}
};

struct DC_IncludeStack
{
	DC_String      file;
	uint32_t       line;
	DC_String      src;
	const uint8_t* ptr;
};

struct DC_Program;

struct DC_Context
{
	DC_Program*         prg;

	DC_IncludeStack		includes[MAX_INCLUDE_STACK];
	uint32_t			includeCount;

	DC_String			file;
	uint32_t			line;
	DC_String           src;
	const uint8_t*      ptr;
};

struct DC_CompilerOptions
{
	uint8_t 		version;			// 1: Original/Jabato - 2: Later
	DDB_Machine		target;				// Target machine
	DDB_Language	language;			// Target language
};

struct DC_Object
{
	uint8_t* name;
	uint8_t  noun;
	uint8_t  adjective;
	uint8_t  attributes;
	uint16_t extraAttributes;
	uint8_t  location;
};

struct DC_Program
{
	Arena*          arena;

	bool			hasTokens;
	uint8_t*        tokens;
	uint8_t*		tokensPtr[128];

	uint8_t			numObjects;
	uint8_t			numLocations;
	uint8_t			numMessages;
	uint8_t			numSystemMessages;
	uint8_t			numProcesses;

	uint8_t*		process[256];
	uint8_t*		message[256];
	uint8_t*		sysMess[256];
	uint8_t*		locDesc[256];
	DC_Object 		object[256];
	uint16_t*		connections[256];
	uint8_t*		vocabulary;
};

extern void        DC_SetError       (DC_Error error);
extern const char* DC_GetErrorString ();
extern DC_Program* DC_Compile        (const char* file, DC_CompilerOptions* opts);