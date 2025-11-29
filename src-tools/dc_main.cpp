#include <dc.h>
#include <dc_char.h>
#include <dc_symb.h>
#include <os_char.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_char.h>

static DC_Error dcError = DCError_None;

void DC_SetError(DC_Error error)
{
	dcError = error;
}

const char* DC_GetErrorString()
{
	static const char* message[] =
	{
		"No error",
		"File not found",
		"I/O error reading file",
		"I/O error writing file",
	};
	static int messageCount = sizeof(message)/sizeof(message[0]);
	if (dcError < messageCount)
		return message[dcError];
	else
		return "Unknown error";
}

void DC_GetNextLine (DC_Context* c)
{
	for (; c->ptr < c->src.end ; c->ptr++)
	{
		if (IsSpace(*c->ptr))
			continue;
		if (*c->ptr == ';')
		{
			while (c->ptr < c->src.end && *c->ptr != '\n')
				c->ptr++;
			continue;
		}
		if (*c->ptr == '#')
		{
			// TODO: Preprocessor
			while (c->ptr < c->src.end && *c->ptr != '\n')
				c->ptr++;
			continue;
		}

		break;
	}
}

DC_Program* DC_Compile (const char* fileName, DC_CompilerOptions* opts)
{
	File* file = File_Open(fileName);
	if (file == 0)
	{
		DC_SetError(DCError_FileNotFound);
		return 0;
	}

	// Blatantly ignore the case of >4GB source file
	uint32_t fileSize = (uint32_t)File_GetSize(file);

	Arena* arena = AllocateArena(fileName);
	DC_Program* program = Allocate<DC_Program>(arena);
	program->arena = arena;
	
	uint8_t* contents = Allocate<uint8_t>(arena, fileSize); 
	DC_Context* context = Allocate<DC_Context>(arena);
	context->file = DC_String(fileName);
	context->src.ptr = contents;
	context->src.end = contents + fileSize;

	if (contents == 0 || File_Read(file, contents, fileSize) != fileSize)
	{
		File_Close(file);
		DC_SetError(DCError_ReadError);
		FreeArena(arena);
		return 0;
	}

	FreeArena(arena);
	return 0;
}
