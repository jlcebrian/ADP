#include <dc.h>

#include <dc_char.h>
#include <dc_condacts.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#include <stdio.h>

struct DC_PreprocessorFrame
{
	bool parentActive;
	bool branchTaken;
	bool active;
};

struct DC_SourceLine
{
	const char* file;
	int         line;
	char*       text;
};

struct DC_Text
{
	char*  data;
	size_t size;
	size_t capacity;
	const char* sourceFile;
	int sourceLine;
};

struct DC_Define
{
	char* name;
	char* value;
};

struct DC_ProcessID
{
	char* name;
	int   value;
};

struct DC_ProcessInstruction
{
	uint8_t opcode;
	bool    indirect;
	int     parameters[2];
	char*   parameterLabels[2];
	int     parameterCount;
};

struct DC_ProcessEntry
{
	int verb;
	int noun;
	DC_ProcessInstruction* instructions;
	size_t instructionCount;
	size_t instructionCapacity;
};

struct DC_ProcessLabel
{
	char* name;
	int entryIndex;
};

struct DC_Process
{
	int index;
	DC_ProcessEntry* entries;
	size_t entryCount;
	size_t entryCapacity;
	DC_ProcessLabel* labels;
	size_t labelCount;
	size_t labelCapacity;
};

struct DC_ObjectDef
{
	int location;
	int weight;
	bool container;
	bool wearable;
	uint16_t attributes;
	int noun;
	int adjective;
};

struct DC_VocabWord
{
	char* text;
	int   index;
	uint8_t type;
	uint8_t encoded[5];
};

struct DC_Connection
{
	int word;
	int destination;
};

struct DC_ConnectionList
{
	DC_Connection* items;
	size_t count;
	size_t capacity;
};

struct DC_ProgramModel
{
	char controlChar;
	DC_Text* tokens;
	size_t tokenCount;
	size_t tokenCapacity;
	DC_VocabWord* vocabulary;
	size_t vocabularyCount;
	size_t vocabularyCapacity;
	DC_Text* systemMessages;
	size_t systemMessageCount;
	size_t systemMessageCapacity;
	DC_Text* messages;
	size_t messageCount;
	size_t messageCapacity;
	DC_Text* objectTexts;
	size_t objectTextCount;
	size_t objectTextCapacity;
	DC_Text* locationTexts;
	size_t locationTextCount;
	size_t locationTextCapacity;
	DC_ConnectionList* connections;
	size_t connectionCount;
	size_t connectionCapacity;
	DC_ObjectDef* objects;
	size_t objectCount;
	size_t objectCapacity;
	DC_Process* processes;
	size_t processCount;
	size_t processCapacity;
};

struct DC_BufferBuilder
{
	uint8_t* data;
	size_t size;
	size_t capacity;
};

struct DC_Context
{
	Arena* arena;
	const char* inputFile;
	DC_CompilerOptions opts;
	DC_Define* defines;
	size_t defineCount;
	size_t defineCapacity;
	const char** includeStack;
	size_t includeStackCount;
	size_t includeStackCapacity;
	DC_ProcessID* processIds;
	size_t processIdCount;
	size_t processIdCapacity;
	DC_VocabWord* parsedVocabulary;
	size_t parsedVocabularyCount;
	bool preprocessRawSection;
	const char* currentSourceFile;
	int currentSourceLine;
	char currentNullWordChar;
	size_t compiledSize;
};

static DC_Error dcError = DCError_None;
static char dcErrorText[32768];
static size_t dcErrorTextSize = 0;
static int dcErrorCount = 0;

static bool IsSpace(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
static bool IsDigit(uint8_t c) { return c >= '0' && c <= '9'; }
static bool IsAlpha(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool IsIdentifierStart(uint8_t c) { return IsAlpha(c) || c == '_'; }
static bool IsIdentifierChar(uint8_t c) { return IsAlpha(c) || IsDigit(c) || c == '_'; }
static uint8_t ToUpper(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c; }

static DC_String MakeString(const char* text)
{
	return DC_String((const uint8_t*)text, text ? StrLen(text) : 0);
}

static DC_String TrimString(DC_String s)
{
	while (s.ptr < s.end && IsSpace(*s.ptr))
		s.ptr++;
	while (s.end > s.ptr && IsSpace(s.end[-1]))
		s.end--;
	return s;
}

static bool StringEmpty(DC_String s)
{
	return s.ptr >= s.end;
}

static bool StringEqualsText(DC_String s, const char* text)
{
	size_t len = StrLen(text);
	if ((size_t)(s.end - s.ptr) != len)
		return false;
	return StrComp(s.ptr, (const uint8_t*)text, len) == 0;
}

static bool StringIEqualsText(DC_String s, const char* text)
{
	while (s.ptr < s.end && *text != 0)
	{
		if (ToUpper(*s.ptr++) != ToUpper((uint8_t)*text++))
			return false;
	}
	return s.ptr == s.end && *text == 0;
}

static bool StringStartsWithTextI(DC_String s, const char* text)
{
	while (*text != 0)
	{
		if (s.ptr >= s.end || ToUpper(*s.ptr++) != ToUpper((uint8_t)*text++))
			return false;
	}
	return true;
}

static bool HeaderStartsWithWordI(DC_String header, const char* text)
{
	header = TrimString(header);
	size_t len = StrLen(text);
	if ((size_t)(header.end - header.ptr) < len)
		return false;
	for (size_t i = 0; i < len; i++)
	{
		if (ToUpper(header.ptr[i]) != ToUpper((uint8_t)text[i]))
			return false;
	}
	return (size_t)(header.end - header.ptr) == len || IsSpace(header.ptr[len]);
}

static char* CopyString(Arena* arena, DC_String s)
{
	size_t size = (size_t)(s.end - s.ptr);
	char* result = Allocate<char>(arena, (unsigned)size + 1, false);
	if (result == 0)
		return 0;
	if (size != 0)
		MemCopy(result, s.ptr, size);
	result[size] = 0;
	return result;
}

static char* CopyUpperString(Arena* arena, DC_String s)
{
	size_t size = (size_t)(s.end - s.ptr);
	char* result = Allocate<char>(arena, (unsigned)size + 1, false);
	if (result == 0)
		return 0;
	for (size_t i = 0; i < size; i++)
		result[i] = (char)ToUpper(s.ptr[i]);
	result[size] = 0;
	return result;
}

static int ParseInt(DC_String s)
{
	s = TrimString(s);
	int sign = 1;
	int value = 0;
	if (s.ptr < s.end && (*s.ptr == '-' || *s.ptr == '+'))
	{
		sign = *s.ptr == '-' ? -1 : 1;
		s.ptr++;
	}
	while (s.ptr < s.end && IsDigit(*s.ptr))
		value = value * 10 + (*s.ptr++ - '0');
	return value * sign;
}

static bool IsNumber(DC_String s)
{
	s = TrimString(s);
	if (s.ptr < s.end && (*s.ptr == '-' || *s.ptr == '+'))
		s.ptr++;
	if (s.ptr >= s.end || !IsDigit(*s.ptr))
		return false;
	while (s.ptr < s.end)
	{
		if (!IsDigit(*s.ptr))
			return false;
		s.ptr++;
	}
	return true;
}

static void AppendRawErrorText(const char* text)
{
	if (text == 0 || text[0] == 0)
		return;
	while (*text != 0 && dcErrorTextSize + 1 < sizeof(dcErrorText))
		dcErrorText[dcErrorTextSize++] = *text++;
	dcErrorText[dcErrorTextSize] = 0;
}

static void AppendErrorText(const char* text)
{
	if (dcErrorTextSize != 0 && dcErrorTextSize + 1 < sizeof(dcErrorText))
		dcErrorText[dcErrorTextSize++] = '\n';
	AppendRawErrorText(text);
}

static void AppendErrorNumber(int value)
{
	char buffer[32];
	LongToChar(value, buffer, 10);
	AppendRawErrorText(buffer);
}

static void SetFailureAt(const char* file, int line, DC_Error error, const char* message)
{
	if (dcError == DCError_None)
		dcError = error;
	if (dcErrorTextSize != 0 && dcErrorTextSize + 1 < sizeof(dcErrorText))
		dcErrorText[dcErrorTextSize++] = '\n';
	if (file != 0 && file[0] != 0)
	{
		AppendRawErrorText(file);
		if (line > 0)
		{
			AppendRawErrorText(":");
			AppendErrorNumber(line);
		}
		AppendRawErrorText(": ");
	}
	AppendRawErrorText(message);
	dcErrorCount++;
}

static void SetFailure(DC_Context* ctx, DC_Error error, const char* message)
{
	SetFailureAt(ctx->currentSourceFile, ctx->currentSourceLine, error, message);
}

template <class T>
static bool ReserveArray(Arena* arena, T** items, size_t* capacity, size_t count)
{
	if (*capacity >= count)
		return true;
	size_t newCapacity = *capacity == 0 ? 8 : *capacity * 2;
	while (newCapacity < count)
		newCapacity *= 2;
	T* newItems = Allocate<T>(arena, (unsigned)newCapacity);
	if (newItems == 0)
		return false;
	if (*items != 0 && *capacity != 0)
		MemCopy(newItems, *items, sizeof(T) * (*capacity));
	*items = newItems;
	*capacity = newCapacity;
	return true;
}

template <class T>
static T* AppendArray(Arena* arena, T** items, size_t* count, size_t* capacity)
{
	if (!ReserveArray(arena, items, capacity, *count + 1))
		return 0;
	return &(*items)[(*count)++];
}

template <class T>
static bool EnsureArray(Arena* arena, T** items, size_t* count, size_t* capacity, size_t size)
{
	if (!ReserveArray(arena, items, capacity, size))
		return false;
	if (*count < size)
		*count = size;
	return true;
}

static bool ReserveBytes(DC_Context* ctx, DC_BufferBuilder* out, size_t size)
{
	if (out->capacity >= size)
		return true;
	size_t newCapacity = out->capacity == 0 ? 1024 : out->capacity * 2;
	while (newCapacity < size)
		newCapacity *= 2;
	uint8_t* newData = Allocate<uint8_t>(ctx->arena, (unsigned)newCapacity, false);
	if (newData == 0)
		return false;
	if (out->data != 0 && out->size != 0)
		MemCopy(newData, out->data, out->size);
	out->data = newData;
	out->capacity = newCapacity;
	return true;
}

static bool AppendByte(DC_Context* ctx, DC_BufferBuilder* out, uint8_t value)
{
	if (!ReserveBytes(ctx, out, out->size + 1))
		return false;
	out->data[out->size++] = value;
	return true;
}

static bool AppendBytes(DC_Context* ctx, DC_BufferBuilder* out, const void* data, size_t size)
{
	if (!ReserveBytes(ctx, out, out->size + size))
		return false;
	if (size != 0)
		MemCopy(out->data + out->size, data, size);
	out->size += size;
	return true;
}

static bool ResizeBytes(DC_Context* ctx, DC_BufferBuilder* out, size_t size)
{
	size_t oldSize = out->size;
	if (!ReserveBytes(ctx, out, size))
		return false;
	if (size > oldSize)
		MemClear(out->data + oldSize, size - oldSize);
	out->size = size;
	return true;
}

static void Write16(uint8_t* data, size_t offset, uint16_t value, bool littleEndian)
{
	if (littleEndian)
	{
		data[offset] = (uint8_t)(value & 0xFF);
		data[offset + 1] = (uint8_t)(value >> 8);
	}
	else
	{
		data[offset] = (uint8_t)(value >> 8);
		data[offset + 1] = (uint8_t)(value & 0xFF);
	}
}

static bool AppendTextBytes(DC_Context* ctx, DC_Text* text, const uint8_t* data, size_t size)
{
	if (text->capacity < text->size + size + 1)
	{
		size_t newCapacity = text->capacity == 0 ? 64 : text->capacity * 2;
		while (newCapacity < text->size + size + 1)
			newCapacity *= 2;
		char* newData = Allocate<char>(ctx->arena, (unsigned)newCapacity, false);
		if (newData == 0)
			return false;
		if (text->data != 0 && text->size != 0)
			MemCopy(newData, text->data, text->size);
		text->data = newData;
		text->capacity = newCapacity;
	}
	if (size != 0)
		MemCopy(text->data + text->size, data, size);
	text->size += size;
	text->data[text->size] = 0;
	return true;
}

static bool AppendTextString(DC_Context* ctx, DC_Text* text, DC_String s)
{
	return AppendTextBytes(ctx, text, s.ptr, (size_t)(s.end - s.ptr));
}

static bool TextEndsWithBreak(DC_Text* text)
{
	return text->size >= 2 && text->data[text->size - 2] == '\\' && text->data[text->size - 1] == 'n';
}

static void CaptureTextOrigin(DC_Context* ctx, DC_Text* text)
{
	if (text->sourceLine == 0)
	{
		text->sourceFile = ctx->currentSourceFile;
		text->sourceLine = ctx->currentSourceLine;
	}
}

static bool AppendTextLine(DC_Context* ctx, DC_Text* text, DC_String line)
{
	if (StringEmpty(line))
		return true;
	CaptureTextOrigin(ctx, text);
	if (text->size != 0 && !TextEndsWithBreak(text))
	{
		uint8_t space = ' ';
		if (!AppendTextBytes(ctx, text, &space, 1))
			return false;
	}
	return AppendTextString(ctx, text, line);
}

static bool AppendTextBreak(DC_Context* ctx, DC_Text* text)
{
	static const uint8_t lineBreak[] = { '\\', 'n' };
	CaptureTextOrigin(ctx, text);
	return AppendTextBytes(ctx, text, lineBreak, 2);
}

static bool IsPAWSMode(DC_Context* ctx)
{
	return ctx->opts.version == DDB_VERSION_PAWS;
}

static bool PAWSTextEndsWithBreak(DC_Text* text)
{
	return text->size >= 3 &&
		text->data[text->size - 3] == '{' &&
		text->data[text->size - 2] == '7' &&
		text->data[text->size - 1] == '}';
}

// PAWCOMP joins the source lines of a message with a space (the interpreter
// word-wraps on output) and encodes a blank source line as control code 7,
// PAW's newline.
static bool PAWSAppendTextLine(DC_Context* ctx, DC_Text* text, DC_String line)
{
	if (StringEmpty(line))
		return true;
	CaptureTextOrigin(ctx, text);
	if (text->size != 0 && !PAWSTextEndsWithBreak(text))
	{
		uint8_t space = ' ';
		if (!AppendTextBytes(ctx, text, &space, 1))
			return false;
	}
	return AppendTextString(ctx, text, line);
}

static bool PAWSAppendTextBreak(DC_Context* ctx, DC_Text* text)
{
	CaptureTextOrigin(ctx, text);
	return AppendTextBytes(ctx, text, (const uint8_t*)"{7}", 3);
}

static bool SplitFields(DC_String line, DC_String* fields, int maxFields, int* count)
{
	int n = 0;
	line = TrimString(line);
	while (line.ptr < line.end)
	{
		while (line.ptr < line.end && IsSpace(*line.ptr))
			line.ptr++;
		if (line.ptr >= line.end)
			break;
		if (n >= maxFields)
			return false;
		fields[n].ptr = line.ptr;
		while (line.ptr < line.end && !IsSpace(*line.ptr))
			line.ptr++;
		fields[n].end = line.ptr;
		n++;
	}
	*count = n;
	return true;
}

static DC_Define* FindDefine(DC_Context* ctx, DC_String name)
{
	for (size_t i = 0; i < ctx->defineCount; i++)
	{
		if (StringEqualsText(name, ctx->defines[i].name))
			return &ctx->defines[i];
	}
	return 0;
}

static DC_String StripComment(DC_String line);

static bool SetDefine(DC_Context* ctx, DC_String name, DC_String value)
{
	name = TrimString(name);
	value = TrimString(StripComment(value));
	if (StringEmpty(name))
		return true;
	DC_Define* define = FindDefine(ctx, name);
	if (define == 0)
		define = AppendArray(ctx->arena, &ctx->defines, &ctx->defineCount, &ctx->defineCapacity);
	if (define == 0)
		return false;
	define->name = CopyString(ctx->arena, name);
	define->value = StringEmpty(value) ? CopyString(ctx->arena, MakeString("1")) : CopyString(ctx->arena, value);
	return define->name != 0 && define->value != 0;
}

static bool IsValidUTF8(DC_String text)
{
	while (text.ptr < text.end)
	{
		uint8_t c = *text.ptr;
		if (c < 0x80)
		{
			text.ptr++;
			continue;
		}
		if ((c & 0xE0) == 0xC0 && text.ptr + 1 < text.end && (text.ptr[1] & 0xC0) == 0x80)
		{
			text.ptr += 2;
			continue;
		}
		if ((c & 0xF0) == 0xE0 && text.ptr + 2 < text.end && (text.ptr[1] & 0xC0) == 0x80 && (text.ptr[2] & 0xC0) == 0x80)
		{
			text.ptr += 3;
			continue;
		}
		if ((c & 0xF8) == 0xF0 && text.ptr + 3 < text.end && (text.ptr[1] & 0xC0) == 0x80 && (text.ptr[2] & 0xC0) == 0x80 && (text.ptr[3] & 0xC0) == 0x80)
		{
			text.ptr += 4;
			continue;
		}
		return false;
	}
	return true;
}

static bool ReadSourceFile(DC_Context* ctx, const char* path, char** output, size_t* outputSize)
{
	File* file = File_Open(path, ReadOnly);
	if (file == 0)
		return false;
	uint64_t size64 = File_GetSize(file);
	if (size64 > 0x7FFFFFFF)
	{
		File_Close(file);
		return false;
	}
	size_t size = (size_t)size64;
	uint8_t* bytes = Allocate<uint8_t>(ctx->arena, (unsigned)size + 1, false);
	if (bytes == 0)
	{
		File_Close(file);
		return false;
	}
	if (File_Read(file, bytes, size) != size)
	{
		File_Close(file);
		return false;
	}
	File_Close(file);

	if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
	{
		bytes += 3;
		size -= 3;
	}
	while (size != 0 && bytes[size - 1] == 0x1A)
		size--;

	bool decodeCP437 = ctx->opts.sourceCharset == DCCharset_CP437 ||
		(ctx->opts.sourceCharset == DCCharset_Auto && !IsValidUTF8(DC_String(bytes, size)));
	if (!decodeCP437)
	{
		char* data = Allocate<char>(ctx->arena, (unsigned)size + 1, false);
		if (data == 0)
			return false;
		if (size != 0)
			MemCopy(data, bytes, size);
		data[size] = 0;
		*output = data;
		*outputSize = size;
		return true;
	}

	char* data = Allocate<char>(ctx->arena, (unsigned)(size * 4 + 1), false);
	if (data == 0)
		return false;
	DC_Buffer buffer((uint8_t*)data, size * 4);
	DC_String converted = ConvertToUTF8(&CP437, buffer, DC_String(bytes, size));
	*outputSize = (size_t)(converted.end - converted.ptr);
	data[*outputSize] = 0;
	*output = data;
	return true;
}

static DC_String StripComment(DC_String line)
{
	bool inString = false;
	const uint8_t* p = line.ptr;
	while (p < line.end)
	{
		if (*p == '"')
			inString = !inString;
		else if (!inString && *p == ';')
		{
			line.end = p;
			break;
		}
		p++;
	}
	return line;
}

static bool BuilderAppendText(DC_Context* ctx, DC_Text* out, const char* text)
{
	return AppendTextBytes(ctx, out, (const uint8_t*)text, StrLen(text));
}

static bool ExpandMacros(DC_Context* ctx, DC_String line, DC_Text* out)
{
	while (line.ptr < line.end)
	{
		if (!IsIdentifierStart(*line.ptr))
		{
			if (!AppendTextBytes(ctx, out, line.ptr, 1))
				return false;
			line.ptr++;
			continue;
		}
		DC_String token;
		token.ptr = line.ptr++;
		while (line.ptr < line.end && IsIdentifierChar(*line.ptr))
			line.ptr++;
		token.end = line.ptr;
		DC_Define* define = FindDefine(ctx, token);
		if (define != 0)
		{
			if (!BuilderAppendText(ctx, out, define->value))
				return false;
		}
		else if (!AppendTextString(ctx, out, token))
			return false;
	}
	return true;
}

static const char* BaseName(const char* path)
{
	const char* result = path;
	for (const char* p = path; *p != 0; p++)
	{
		if (*p == '/' || *p == '\\')
			result = p + 1;
	}
	return result;
}

static size_t DirNameSize(const char* path)
{
	size_t result = 0;
	for (size_t i = 0; path[i] != 0; i++)
	{
		if (path[i] == '/' || path[i] == '\\')
			result = i;
	}
	return result;
}

static bool JoinPath(char* buffer, size_t bufferSize, const char* folder, size_t folderSize, const char* name)
{
	if (folderSize == 0 || (folderSize == 1 && folder[0] == '.'))
		return StrCopy(buffer, (uint32_t)bufferSize, name) < bufferSize;
	if (folderSize + 1 + StrLen(name) + 1 > bufferSize)
		return false;
	MemCopy(buffer, folder, folderSize);
	buffer[folderSize] = '/';
	StrCopy(buffer + folderSize + 1, (uint32_t)(bufferSize - folderSize - 1), name);
	return true;
}

static bool FileExists(const char* path);

static bool HasFileExtension(const char* path)
{
	const char* base = BaseName(path);
	return StrRChr(base, '.') != 0;
}

static bool TryResolveIncludePath(char* resolved, size_t resolvedSize, const char* path)
{
	if (FileExists(path))
	{
		StrCopy(resolved, (uint32_t)resolvedSize, path);
		return true;
	}
	if (!HasFileExtension(path))
	{
		static const char* extensions[] = { ".sce", ".SCE" };
		for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
		{
			char candidate[FILE_MAX_PATH];
			StrCopy(candidate, sizeof(candidate), path);
			StrCat(candidate, sizeof(candidate), extensions[i]);
			if (FileExists(candidate))
			{
				StrCopy(resolved, (uint32_t)resolvedSize, candidate);
				return true;
			}
		}
	}
	return false;
}

static bool FileExists(const char* path)
{
	File* file = File_Open(path, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static bool ResolveInclude(DC_Context* ctx, const char* includingFile, DC_String includeName, char* resolved, size_t resolvedSize)
{
	char includeBuffer[FILE_MAX_PATH];
	char candidate[FILE_MAX_PATH];
	DC_String name = includeName;
	name = TrimString(name);
	if (name.ptr < name.end && *name.ptr == '"')
	{
		name.ptr++;
		const uint8_t* end = name.ptr;
		while (end < name.end && *end != '"')
			end++;
		name.end = end;
	}
	char* include = CopyString(ctx->arena, name);
	if (include == 0)
		return false;
	StrCopy(includeBuffer, sizeof(includeBuffer), include);
	const char* base = BaseName(includeBuffer);

	if (TryResolveIncludePath(resolved, resolvedSize, includeBuffer))
		return true;
	if (JoinPath(candidate, sizeof(candidate), includingFile, DirNameSize(includingFile), includeBuffer) &&
		TryResolveIncludePath(resolved, resolvedSize, candidate))
		return true;
	if (JoinPath(candidate, sizeof(candidate), includingFile, DirNameSize(includingFile), base) &&
		TryResolveIncludePath(resolved, resolvedSize, candidate))
		return true;
	for (size_t i = 0; i < ctx->opts.includePathCount; i++)
	{
		const char* path = ctx->opts.includePaths[i];
		if (path != 0 && JoinPath(candidate, sizeof(candidate), path, StrLen(path), base) &&
			TryResolveIncludePath(resolved, resolvedSize, candidate))
			return true;
	}
	static const char* testPaths[] = { "tests/sce", "tests/devdisk", "tests/pcw" };
	for (size_t i = 0; i < sizeof(testPaths) / sizeof(testPaths[0]); i++)
	{
		if (JoinPath(candidate, sizeof(candidate), testPaths[i], StrLen(testPaths[i]), base) &&
			TryResolveIncludePath(resolved, resolvedSize, candidate))
			return true;
	}
	return false;
}

struct DC_ExprParser
{
	DC_Context* ctx;
	DC_String text;
};

static void ExprSkipSpaces(DC_ExprParser* parser)
{
	while (parser->text.ptr < parser->text.end && IsSpace(*parser->text.ptr))
		parser->text.ptr++;
}

static bool ExprConsume(DC_ExprParser* parser, const char* token)
{
	ExprSkipSpaces(parser);
	size_t len = StrLen(token);
	if ((size_t)(parser->text.end - parser->text.ptr) >= len && StrComp(parser->text.ptr, (const uint8_t*)token, len) == 0)
	{
		parser->text.ptr += len;
		return true;
	}
	return false;
}

static int ParseExprOr(DC_ExprParser* parser);
static DC_ProcessID* FindProcessID(DC_Context* ctx, DC_String name);

static int ParseExprUnary(DC_ExprParser* parser)
{
	ExprSkipSpaces(parser);
	if (ExprConsume(parser, "!")) return !ParseExprUnary(parser);
	if (ExprConsume(parser, "+")) return ParseExprUnary(parser);
	if (ExprConsume(parser, "-")) return -ParseExprUnary(parser);
	if (ExprConsume(parser, "("))
	{
		int value = ParseExprOr(parser);
		ExprConsume(parser, ")");
		return value;
	}
	if (parser->text.ptr < parser->text.end && IsDigit(*parser->text.ptr))
	{
		int value = 0;
		while (parser->text.ptr < parser->text.end && IsDigit(*parser->text.ptr))
			value = value * 10 + (*parser->text.ptr++ - '0');
		return value;
	}
	if (parser->text.ptr < parser->text.end && IsIdentifierStart(*parser->text.ptr))
	{
		DC_String name;
		name.ptr = parser->text.ptr++;
		while (parser->text.ptr < parser->text.end && IsIdentifierChar(*parser->text.ptr))
			parser->text.ptr++;
		name.end = parser->text.ptr;
		DC_Define* define = FindDefine(parser->ctx, name);
		if (define == 0)
		{
			DC_ProcessID* proc = FindProcessID(parser->ctx, name);
			return proc != 0 ? proc->value : 0;
		}
		if (define->value[0] == 0)
			return 1;
		DC_ExprParser nested;
		nested.ctx = parser->ctx;
		nested.text = MakeString(define->value);
		return ParseExprOr(&nested);
	}
	return 0;
}

static int ParseExprAdd(DC_ExprParser* parser)
{
	int value = ParseExprUnary(parser);
	for (;;)
	{
		if (ExprConsume(parser, "+")) value += ParseExprUnary(parser);
		else if (ExprConsume(parser, "-")) value -= ParseExprUnary(parser);
		else break;
	}
	return value;
}

static int ParseExprCompare(DC_ExprParser* parser)
{
	int value = ParseExprAdd(parser);
	bool compared = false;
	for (;;)
	{
		if (ExprConsume(parser, "==")) { value = value == ParseExprAdd(parser); compared = true; }
		else if (ExprConsume(parser, "!=")) { value = value != ParseExprAdd(parser); compared = true; }
		else if (ExprConsume(parser, "<=")) { value = value <= ParseExprAdd(parser); compared = true; }
		else if (ExprConsume(parser, ">=")) { value = value >= ParseExprAdd(parser); compared = true; }
		else if (ExprConsume(parser, "<")) { value = value < ParseExprAdd(parser); compared = true; }
		else if (ExprConsume(parser, ">")) { value = value > ParseExprAdd(parser); compared = true; }
		else break;
	}
	return compared ? (value ? 1 : 0) : value;
}

static int ParseExprAnd(DC_ExprParser* parser)
{
	int value = ParseExprCompare(parser);
	while (ExprConsume(parser, "&&"))
		value = (value && ParseExprCompare(parser)) ? 1 : 0;
	return value;
}

static int ParseExprOr(DC_ExprParser* parser)
{
	int value = ParseExprAnd(parser);
	while (ExprConsume(parser, "||"))
		value = (value || ParseExprAnd(parser)) ? 1 : 0;
	return value;
}

static bool PreprocessFile(DC_Context* ctx, const char* path, DC_SourceLine** lines, size_t* lineCount, size_t* lineCapacity);

static bool IsCompilerSectionHeader(DC_String header)
{
	return HeaderStartsWithWordI(header, "CTL") ||
		HeaderStartsWithWordI(header, "TOK") ||
		HeaderStartsWithWordI(header, "VOC") ||
		HeaderStartsWithWordI(header, "STX") ||
		HeaderStartsWithWordI(header, "MTX") ||
		HeaderStartsWithWordI(header, "OTX") ||
		HeaderStartsWithWordI(header, "LTX") ||
		HeaderStartsWithWordI(header, "CON") ||
		HeaderStartsWithWordI(header, "OBJ") ||
		HeaderStartsWithWordI(header, "PRO");
}

static bool HandleDirective(
	DC_Context* ctx,
	const char* path,
	int lineNumber,
	DC_String trimmed,
	bool active,
	DC_PreprocessorFrame* frames,
	int* frameCount,
	DC_SourceLine** lines,
	size_t* lineCount,
	size_t* lineCapacity)
{
	DC_String rest = trimmed;
	rest.ptr++;
	rest = TrimString(rest);
	DC_String directive = rest;
	while (rest.ptr < rest.end && !IsSpace(*rest.ptr))
		rest.ptr++;
	directive.end = rest.ptr;
	DC_String args = TrimString(rest);

	if (StringIEqualsText(directive, "INCLUDE"))
	{
		if (!active)
			return true;
		char resolved[FILE_MAX_PATH];
		if (!ResolveInclude(ctx, path, args, resolved, sizeof(resolved)))
		{
			SetFailureAt(path, lineNumber, DCError_FileNotFound, "Include not found");
			return true;
		}
		return PreprocessFile(ctx, CopyString(ctx->arena, MakeString(resolved)), lines, lineCount, lineCapacity);
	}

	if (StringIEqualsText(directive, "DEFINE"))
	{
		if (!active)
			return true;
		DC_String name = args;
		while (args.ptr < args.end && !IsSpace(*args.ptr))
			args.ptr++;
		name.end = args.ptr;
		return SetDefine(ctx, name, args);
	}

	if (StringIEqualsText(directive, "IF"))
	{
		bool parentActive = *frameCount == 0 ? true : frames[*frameCount - 1].active;
		bool condition = false;
		if (parentActive)
		{
			DC_ExprParser parser;
			parser.ctx = ctx;
			parser.text = args;
			condition = ParseExprOr(&parser) != 0;
		}
		if (*frameCount >= 32)
		{
			SetFailureAt(path, lineNumber, DCError_SyntaxError, "Too many nested conditionals");
			return true;
		}
		frames[*frameCount].parentActive = parentActive;
		frames[*frameCount].branchTaken = condition;
		frames[*frameCount].active = parentActive && condition;
		(*frameCount)++;
		return true;
	}

	if (StringIEqualsText(directive, "IFDEF") || StringIEqualsText(directive, "IFNDEF"))
	{
		bool parentActive = *frameCount == 0 ? true : frames[*frameCount - 1].active;
		bool defined = FindDefine(ctx, args) != 0;
		bool condition = StringIEqualsText(directive, "IFDEF") ? defined : !defined;
		if (*frameCount >= 32)
		{
			SetFailureAt(path, lineNumber, DCError_SyntaxError, "Too many nested conditionals");
			return true;
		}
		frames[*frameCount].parentActive = parentActive;
		frames[*frameCount].branchTaken = condition;
		frames[*frameCount].active = parentActive && condition;
		(*frameCount)++;
		return true;
	}

	if (StringIEqualsText(directive, "ELSE"))
	{
		if (*frameCount == 0)
		{
			SetFailureAt(path, lineNumber, DCError_SyntaxError, "Unexpected #else");
			return true;
		}
		DC_PreprocessorFrame* frame = &frames[*frameCount - 1];
		frame->active = frame->parentActive && !frame->branchTaken;
		frame->branchTaken = true;
		return true;
	}

	if (StringIEqualsText(directive, "ENDIF"))
	{
		if (*frameCount == 0)
		{
			SetFailureAt(path, lineNumber, DCError_SyntaxError, "Unexpected #endif");
			return true;
		}
		(*frameCount)--;
		return true;
	}

	if (StringIEqualsText(directive, "DEFB") || StringIEqualsText(directive, "DEFW") ||
		StringIEqualsText(directive, "DBADDR") || StringIEqualsText(directive, "EXTERN"))
		return true;

	if (ctx->opts.strict)
		SetFailureAt(path, lineNumber, DCError_Unsupported, "Unsupported directive");
	return true;
}

static bool PreprocessFile(DC_Context* ctx, const char* path, DC_SourceLine** lines, size_t* lineCount, size_t* lineCapacity)
{
	for (size_t i = 0; i < ctx->includeStackCount; i++)
	{
		if (StrComp(ctx->includeStack[i], path) == 0)
		{
			SetFailureAt(path, 0, DCError_SyntaxError, "Recursive include detected");
			return true;
		}
	}
	const char** stackEntry = AppendArray(ctx->arena, &ctx->includeStack, &ctx->includeStackCount, &ctx->includeStackCapacity);
	if (stackEntry == 0)
		return false;
	*stackEntry = path;

	char* contents = 0;
	size_t contentsSize = 0;
	if (!ReadSourceFile(ctx, path, &contents, &contentsSize))
	{
		ctx->includeStackCount--;
		SetFailureAt(path, 0, DCError_FileNotFound, "Unable to read file");
		return false;
	}

	DC_PreprocessorFrame frames[32];
	int frameCount = 0;
	const uint8_t* ptr = (const uint8_t*)contents;
	const uint8_t* end = ptr + contentsSize;
	int lineNumber = 0;
	while (ptr <= end)
	{
		const uint8_t* lineStart = ptr;
		while (ptr < end && *ptr != '\n')
			ptr++;
		const uint8_t* lineEnd = ptr;
		if (lineEnd > lineStart && lineEnd[-1] == '\r')
			lineEnd--;
		if (ptr < end && *ptr == '\n')
			ptr++;
		lineNumber++;

		DC_String line(lineStart, lineEnd);
		DC_String trimmed = TrimString(line);
		bool active = frameCount == 0 ? true : frames[frameCount - 1].active;
		if (!StringEmpty(trimmed) && *trimmed.ptr == '#')
		{
			if (!HandleDirective(ctx, path, lineNumber, trimmed, active, frames, &frameCount, lines, lineCount, lineCapacity))
			{
				ctx->includeStackCount--;
				return false;
			}
		}
		else if (active)
		{
			bool isSectionHeader = false;
			bool enteringRawSection = false;
			if (!StringEmpty(trimmed) && *trimmed.ptr == '/')
			{
				DC_String header = TrimString(StripComment(DC_String(trimmed.ptr + 1, trimmed.end)));
				if (IsPAWSMode(ctx) && HeaderStartsWithWordI(header, "LNK"))
				{
					// PAWCOMP file chaining: /LNK ends the current file and
					// continues compilation in the named one.
					DC_String linked = TrimString(DC_String(header.ptr + 3, header.end));
					char resolved[FILE_MAX_PATH];
					if (StringEmpty(linked) || !ResolveInclude(ctx, path, linked, resolved, sizeof(resolved)))
					{
						ctx->includeStackCount--;
						SetFailureAt(path, lineNumber, DCError_FileNotFound, "Unable to resolve /LNK file");
						return false;
					}
					if (frameCount != 0)
					{
						ctx->includeStackCount--;
						SetFailureAt(path, lineNumber, DCError_SyntaxError, "Unterminated conditional");
						return false;
					}
					char* resolvedCopy = CopyString(ctx->arena, MakeString(resolved));
					bool ok = resolvedCopy != 0 && PreprocessFile(ctx, resolvedCopy, lines, lineCount, lineCapacity);
					ctx->includeStackCount--;
					return ok;
				}
				if (IsCompilerSectionHeader(header))
				{
					isSectionHeader = true;
					enteringRawSection = true;
				}
			}
			int depth = frameCount;
			while (depth > 0 && line.ptr < line.end && *line.ptr == ' ')
			{
				line.ptr++;
				depth--;
			}
			DC_SourceLine* sourceLine = AppendArray(ctx->arena, lines, lineCount, lineCapacity);
			if (sourceLine == 0)
			{
				ctx->includeStackCount--;
				return false;
			}
			sourceLine->file = path;
			sourceLine->line = lineNumber;
			sourceLine->text = CopyString(ctx->arena, line);
			if (isSectionHeader)
				ctx->preprocessRawSection = enteringRawSection;
		}

		if (ptr >= end)
			break;
	}

	ctx->includeStackCount--;
	if (frameCount != 0)
		SetFailureAt(path, lineNumber, DCError_SyntaxError, "Unterminated conditional");
	return true;
}

static bool TryEncodeCodepointToDAADText(DC_Context* ctx, uint32_t codepoint, uint8_t* value)
{
	for (size_t i = ctx->opts.translationCount; i > 0; i--)
	{
		const DC_CharTranslation* tr = &ctx->opts.translations[i - 1];
		if (tr->unicode == codepoint)
		{
			*value = tr->code;
			return true;
		}
	}

	if (codepoint < 0x80)
	{
		*value = (uint8_t)codepoint;
		return true;
	}

	// PAW text bytes 0x00-0x17 are display control codes and everything else
	// comes from the game font, so only ASCII, {n} codes and --tr mappings
	// have a defined meaning there.
	if (ctx->opts.version == DDB_VERSION_PAWS)
		return false;

	// DDB text charset supports ASCII plus these Spanish characters in 0x10..0x1F.
	switch (codepoint)
	{
		case 0x00AA: *value = 0x10; return true; // ª
		case 0x00A1: *value = 0x11; return true; // ¡
		case 0x00BF: *value = 0x12; return true; // ¿
		case 0x00AB: *value = 0x13; return true; // «
		case 0x00BB: *value = 0x14; return true; // »
		case 0x00E1: *value = 0x15; return true; // á
		case 0x00E9: *value = 0x16; return true; // é
		case 0x00ED: *value = 0x17; return true; // í
		case 0x00F3: *value = 0x18; return true; // ó
		case 0x00FA: *value = 0x19; return true; // ú
		case 0x00F1: *value = 0x1A; return true; // ñ
		case 0x00D1: *value = 0x1B; return true; // Ñ
		case 0x00E7: *value = 0x1C; return true; // ç
		case 0x00C7: *value = 0x1D; return true; // Ç
		case 0x00FC: *value = 0x1E; return true; // ü
		case 0x00DC: *value = 0x1F; return true; // Ü
		case 0x00A0: *value = 0x7F; return true; // non-breakable space
		default: break;
	}

	return false;
}

static void SetUnsupportedCodepointFailure(DC_Context* ctx, uint32_t codepoint)
{
	char message[96];
	if (codepoint <= 0xFFFF)
		snprintf(message, sizeof(message), "Unsupported UTF-8 character U+%04X for DDB text encoding", (unsigned)codepoint);
	else
		snprintf(message, sizeof(message), "Unsupported UTF-8 character U+%06X for DDB text encoding", (unsigned)codepoint);
	SetFailure(ctx, DCError_Unsupported, message);
}

static bool MapLegacySourceControl(uint32_t codepoint, uint8_t* value)
{
	switch (codepoint)
	{
		case 0x0010: case 0x2410: *value = 0x0B; return true;
		case 0x0011: case 0x2411: *value = 0x0C; return true;
		case 0x0018: case 0x2418: *value = 0x0E; return true;
		case 0x0019: case 0x2419: *value = 0x0F; return true;
		case 0x001E: case 0x241E: *value = 0x7F; return true;
		case 0x007F: case 0x2421: *value = ' '; return true;
		default: return false;
	}
}

static bool TryParseEscape(DC_String text, size_t* index, bool allowMessageControls, uint8_t* value)
{
	size_t size = (size_t)(text.end - text.ptr);
	if (*index + 1 >= size || text.ptr[*index] != '\\')
		return false;
	uint8_t escape = text.ptr[*index + 1];
	if (escape >= 'A' && escape <= 'P')
	{
		*value = (uint8_t)(16 + (escape - 'A'));
		*index += 2;
		return true;
	}
	switch (escape)
	{
		case 's': *value = ' '; *index += 2; return true;
		case '\\': *value = '\\'; *index += 2; return true;
		case '{': *value = '{'; *index += 2; return true;
		case '}': *value = '}'; *index += 2; return true;
		case 'f': *value = 0x7F; *index += 2; return true;
		case 'b': if (allowMessageControls) { *value = 0x0B; *index += 2; return true; } break;
		case 'k': if (allowMessageControls) { *value = 0x0C; *index += 2; return true; } break;
		case 'n': if (allowMessageControls) { *value = 0x0D; *index += 2; return true; } break;
		case 'g': if (allowMessageControls) { *value = 0x0E; *index += 2; return true; } break;
		case 't': if (allowMessageControls) { *value = 0x0F; *index += 2; return true; } break;
		default: break;
	}
	return false;
}

static bool DecodeUTF8Codepoint(DC_String text, size_t* index, uint32_t* codepoint)
{
	size_t size = (size_t)(text.end - text.ptr);
	if (*index >= size)
		return false;
	uint8_t c = text.ptr[*index];
	if (c < 0x80)
	{
		*codepoint = c;
		(*index)++;
		return true;
	}
	if ((c & 0xE0) == 0xC0 && *index + 1 < size && (text.ptr[*index + 1] & 0xC0) == 0x80)
	{
		*codepoint = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(text.ptr[*index + 1] & 0x3F);
		*index += 2;
		return true;
	}
	if ((c & 0xF0) == 0xE0 && *index + 2 < size && (text.ptr[*index + 1] & 0xC0) == 0x80 && (text.ptr[*index + 2] & 0xC0) == 0x80)
	{
		*codepoint = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(text.ptr[*index + 1] & 0x3F) << 6) | (uint32_t)(text.ptr[*index + 2] & 0x3F);
		*index += 3;
		return true;
	}
	(*index)++;
	return false;
}

static bool AppendEncodedPlain(DC_Context* ctx, DC_BufferBuilder* out, DC_String text, size_t maxBytes)
{
	for (size_t i = 0; i < (size_t)(text.end - text.ptr) && out->size < maxBytes;)
	{
		uint8_t escaped = 0;
		if (TryParseEscape(text, &i, false, &escaped))
		{
			if (!AppendByte(ctx, out, escaped))
				return false;
			continue;
		}
		if (text.ptr[i] == '{')
		{
			size_t start = i + 1;
			size_t end = start;
			while (end < (size_t)(text.end - text.ptr) && text.ptr[end] != '}')
				end++;
			if (end < (size_t)(text.end - text.ptr) && end > start)
			{
				bool digits = true;
				for (size_t n = start; n < end; n++)
					digits = digits && IsDigit(text.ptr[n]);
				if (digits)
				{
					if (!AppendByte(ctx, out, (uint8_t)ParseInt(DC_String(text.ptr + start, text.ptr + end))))
						return false;
					i = end + 1;
					continue;
				}
			}
		}
		uint32_t codepoint = 0;
		if (!DecodeUTF8Codepoint(text, &i, &codepoint))
		{
			SetFailure(ctx, DCError_SyntaxError, "Malformed UTF-8 sequence");
			return false;
		}
		uint8_t encoded = 0;
		if (!TryEncodeCodepointToDAADText(ctx, codepoint, &encoded))
		{
			SetUnsupportedCodepointFailure(ctx, codepoint);
			return false;
		}
		if (!AppendByte(ctx, out, encoded))
			return false;
	}
	return true;
}

static bool EncodeWord(DC_Context* ctx, DC_String word, uint8_t encoded[5])
{
	DC_BufferBuilder out = {};
	if (!AppendEncodedPlain(ctx, &out, word, 5))
		return false;
	for (size_t i = 0; i < 5; i++)
		encoded[i] = i < out.size ? out.data[i] : ' ';
	return true;
}

static bool AppendToken(DC_Context* ctx, DC_BufferBuilder* data, DC_String token)
{
	DC_BufferBuilder out = {};
	if (!AppendEncodedPlain(ctx, &out, token, 65535))
		return false;
	if (out.size == 0)
		return AppendByte(ctx, data, 0x80);
	for (size_t i = 0; i < out.size; i++)
	{
		uint8_t c = out.data[i] & 0x7F;
		if (i + 1 == out.size)
			c |= 0x80;
		if (!AppendByte(ctx, data, c))
			return false;
	}
	return true;
}

static bool AppendMessagePlainBytes(DC_Context* ctx, DC_BufferBuilder* out, DC_String text)
{
	size_t size = (size_t)(text.end - text.ptr);
	for (size_t i = 0; i < size;)
	{
		uint8_t escaped = 0;
		if (TryParseEscape(text, &i, true, &escaped))
		{
			if (!AppendByte(ctx, out, escaped))
				return false;
			continue;
		}
		if (text.ptr[i] == '{')
		{
			size_t start = i + 1;
			size_t end = start;
			while (end < size && text.ptr[end] != '}')
				end++;
			if (end < size && end > start)
			{
				bool digits = true;
				for (size_t n = start; n < end; n++)
					digits = digits && IsDigit(text.ptr[n]);
				if (digits)
				{
					if (!AppendByte(ctx, out, (uint8_t)ParseInt(DC_String(text.ptr + start, text.ptr + end))))
						return false;
					i = end + 1;
					continue;
				}
			}
		}
		uint32_t codepoint = 0;
		if (!DecodeUTF8Codepoint(text, &i, &codepoint))
		{
			SetFailure(ctx, DCError_SyntaxError, "Malformed UTF-8 sequence");
			return false;
		}
		uint8_t legacyControl = 0;
		if (MapLegacySourceControl(codepoint, &legacyControl))
		{
			if (!AppendByte(ctx, out, legacyControl))
				return false;
			continue;
		}
		uint8_t encoded = 0;
		if (!TryEncodeCodepointToDAADText(ctx, codepoint, &encoded))
		{
			SetUnsupportedCodepointFailure(ctx, codepoint);
			return false;
		}
		if (!AppendByte(ctx, out, encoded))
			return false;
	}
	return true;
}

static bool ValidateTextLineEncoding(DC_Context* ctx, DC_String text, bool allowMessageControls)
{
	size_t size = (size_t)(text.end - text.ptr);
	for (size_t i = 0; i < size;)
	{
		uint8_t escaped = 0;
		if (TryParseEscape(text, &i, allowMessageControls, &escaped))
			continue;

		if (text.ptr[i] == '{')
		{
			size_t start = i + 1;
			size_t end = start;
			while (end < size && text.ptr[end] != '}')
				end++;
			if (end < size && end > start)
			{
				bool digits = true;
				for (size_t n = start; n < end; n++)
					digits = digits && IsDigit(text.ptr[n]);
				if (digits)
				{
					i = end + 1;
					continue;
				}
			}
		}

		uint32_t codepoint = 0;
		if (!DecodeUTF8Codepoint(text, &i, &codepoint))
		{
			SetFailure(ctx, DCError_SyntaxError, "Malformed UTF-8 sequence");
			return false;
		}

		uint8_t encoded = 0;
		if (allowMessageControls)
		{
			uint8_t legacyControl = 0;
			if (MapLegacySourceControl(codepoint, &legacyControl))
				continue;
		}

		if (!TryEncodeCodepointToDAADText(ctx, codepoint, &encoded))
		{
			SetUnsupportedCodepointFailure(ctx, codepoint);
			return false;
		}
	}
	return true;
}

static bool EncodeTokenBytes(DC_Context* ctx, DC_Text* token, DC_BufferBuilder* out)
{
	return AppendEncodedPlain(ctx, out, MakeString(token->data != 0 ? token->data : ""), 65535);
}

static bool TokenMatchesAt(const DC_BufferBuilder* plain, size_t offset, const DC_BufferBuilder* token)
{
	if (token->size == 0 || offset + token->size > plain->size)
		return false;
	return MemComp(plain->data + offset, token->data, token->size) == 0;
}

static bool AppendMessage(DC_Context* ctx, DC_BufferBuilder* data, DC_String text, DC_Text* tokens, size_t tokenCount, bool compress)
{
	DC_BufferBuilder plain = {};
	if (!AppendMessagePlainBytes(ctx, &plain, text))
		return false;
	DC_BufferBuilder tokenBytes[128] = {};
	if (compress && tokenCount > 128)
		tokenCount = 128;
	if (compress)
	{
		for (size_t i = 0; i < tokenCount; i++)
		{
			if (!EncodeTokenBytes(ctx, &tokens[i], &tokenBytes[i]))
				return false;
		}
	}
	int* tokenAt = 0;
	bool* covered = 0;
	if (compress && plain.size != 0)
	{
		tokenAt = Allocate<int>(ctx->arena, (unsigned)plain.size, false);
		covered = Allocate<bool>(ctx->arena, (unsigned)plain.size, false);
		if (tokenAt == 0 || covered == 0)
			return false;
		for (size_t i = 0; i < plain.size; i++)
		{
			tokenAt[i] = -1;
			covered[i] = false;
		}
		for (size_t n = 0; n < tokenCount; n++)
		{
			if (tokenBytes[n].size == 0)
				continue;
			for (size_t i = 0; i + tokenBytes[n].size <= plain.size;)
			{
				bool match = true;
				for (size_t k = 0; k < tokenBytes[n].size; k++)
				{
					if (covered[i + k] || tokenAt[i + k] >= 0 || plain.data[i + k] != tokenBytes[n].data[k])
					{
						match = false;
						break;
					}
				}
				if (match)
				{
					tokenAt[i] = (int)n;
					for (size_t k = 1; k < tokenBytes[n].size; k++)
						covered[i + k] = true;
					i += tokenBytes[n].size;
				}
				else
				{
					i++;
				}
			}
		}
	}
	for (size_t i = 0; i < plain.size; i++)
	{
		if (covered != 0 && covered[i])
			continue;
		if (tokenAt != 0 && tokenAt[i] >= 0)
		{
			if (!AppendByte(ctx, data, (uint8_t)((0x80 + tokenAt[i]) ^ 0xFF)))
				return false;
		}
		else
		{
			if (!AppendByte(ctx, data, plain.data[i] ^ 0xFF))
				return false;
		}
	}
	return AppendByte(ctx, data, 0x0A ^ 0xFF);
}

static uint8_t ParseWordType(DC_String text)
{
	if (StringIEqualsText(text, "VERB")) return WordType_Verb;
	if (StringIEqualsText(text, "NOUN")) return WordType_Noun;
	if (StringIEqualsText(text, "ADJECTIVE")) return WordType_Adjective;
	if (StringIEqualsText(text, "ADVERB")) return WordType_Adverb;
	if (StringIEqualsText(text, "PREPOSITION")) return WordType_Preposition;
	if (StringIEqualsText(text, "CONJUNCTION") || StringIEqualsText(text, "CONJUGATION")) return WordType_Conjunction;
	if (StringIEqualsText(text, "PRONOUN")) return WordType_Pronoun;
	return WordType_Unknown;
}

static bool IsNullWord(DC_Context* ctx, DC_String text)
{
	return (text.end - text.ptr) == 1 && *text.ptr == (uint8_t)ctx->currentNullWordChar;
}

static bool VocabularyMatches(DC_Context* ctx, DC_VocabWord* word, DC_String text)
{
	char buffer[256];
	size_t size = (size_t)(text.end - text.ptr);
	if (size < sizeof(buffer))
	{
		for (size_t i = 0; i < size; i++)
			buffer[i] = (char)ToUpper(text.ptr[i]);
		text.ptr = (const uint8_t*)buffer;
		text.end = text.ptr + size;
	}
	uint8_t encoded[5];
	if (!EncodeWord(ctx, text, encoded))
		return false;
	return MemComp(word->encoded, encoded, 5) == 0;
}

static int ResolveWordReference(DC_Context* ctx, DC_ProgramModel* model, DC_String text, bool preferNoun, uint8_t exactType)
{
	if (IsNullWord(ctx, text))
		return -1;
	if (IsNumber(text))
		return ParseInt(text);
	for (size_t i = 0; i < model->vocabularyCount; i++)
	{
		DC_VocabWord* word = &model->vocabulary[i];
		if (!VocabularyMatches(ctx, word, text))
			continue;
		if (exactType != WordType_Unknown && word->type != exactType)
			continue;
		if (preferNoun && word->type == WordType_Noun)
			return word->index;
		if (!preferNoun || exactType != WordType_Unknown)
			return word->index;
	}
	if (preferNoun)
	{
		for (size_t i = 0; i < model->vocabularyCount; i++)
		{
			if (VocabularyMatches(ctx, &model->vocabulary[i], text))
				return model->vocabulary[i].index;
		}
	}
	SetFailure(ctx, DCError_SemanticError, "Unknown vocabulary word");
	return -1;
}

static int LookupWordNumber(DC_Context* ctx, DC_String text, uint8_t type)
{
	if (IsNullWord(ctx, text))
		return 255;
	for (size_t i = 0; i < ctx->parsedVocabularyCount; i++)
	{
		if (ctx->parsedVocabulary[i].type == type && VocabularyMatches(ctx, &ctx->parsedVocabulary[i], text))
			return ctx->parsedVocabulary[i].index;
	}
	for (size_t i = 0; i < ctx->parsedVocabularyCount; i++)
	{
		if (VocabularyMatches(ctx, &ctx->parsedVocabulary[i], text))
			return ctx->parsedVocabulary[i].index;
	}
	SetFailure(ctx, DCError_SemanticError, "Unknown vocabulary reference");
	return -1;
}

static DC_ProcessID* FindProcessID(DC_Context* ctx, DC_String name)
{
	for (size_t i = 0; i < ctx->processIdCount; i++)
	{
		if (StringEqualsText(name, ctx->processIds[i].name))
			return &ctx->processIds[i];
	}
	return 0;
}

static int LookupNumeric(DC_Context* ctx, DC_String text);

static int ParseSymbolicIndex(DC_Context* ctx, DC_String text, int fallbackIndex)
{
	text = TrimString(text);
	if (StringEmpty(text))
		return fallbackIndex;
	if (IsDigit(*text.ptr))
		return ParseInt(text);
	const uint8_t* plus = text.ptr;
	while (plus < text.end && *plus != '+')
		plus++;
	if (plus < text.end)
	{
		DC_String base = TrimString(DC_String(text.ptr, plus));
		DC_ProcessID* id = FindProcessID(ctx, base);
		if (id == 0 && FindDefine(ctx, base) != 0)
			return LookupNumeric(ctx, text);
		if (id == 0)
		{
			SetFailure(ctx, DCError_SemanticError, "Unknown symbolic index");
			return -1;
		}
		return id->value + ParseInt(DC_String(plus + 1, text.end));
	}
	DC_ProcessID* id = FindProcessID(ctx, text);
	if (id != 0)
		return id->value;
	if (FindDefine(ctx, text) != 0)
		return LookupNumeric(ctx, text);
	id = AppendArray(ctx->arena, &ctx->processIds, &ctx->processIdCount, &ctx->processIdCapacity);
	if (id == 0)
		return -1;
	id->name = CopyString(ctx->arena, text);
	id->value = fallbackIndex;
	return fallbackIndex;
}

static int LookupNumeric(DC_Context* ctx, DC_String text)
{
	text = TrimString(text);
	if (StringEmpty(text))
		return 0;
	if (IsNullWord(ctx, text))
		return 255;
	if (IsNumber(text))
		return ParseInt(text);
	DC_Define* define = FindDefine(ctx, text);
	if (define != 0)
	{
		DC_ExprParser nested;
		nested.ctx = ctx;
		nested.text = MakeString(define->value);
		return ParseExprOr(&nested);
	}
	DC_ProcessID* proc = FindProcessID(ctx, text);
	if (proc != 0)
		return proc->value;
	DC_ExprParser parser;
	parser.ctx = ctx;
	parser.text = text;
	int value = ParseExprOr(&parser);
	ExprSkipSpaces(&parser);
	if (parser.text.ptr == parser.text.end)
		return value;
	return 0;
}

static int ParseObjectLocation(DC_Context* ctx, DC_String text)
{
	if (IsNullWord(ctx, text) || StringIEqualsText(text, "DESTROYED") || StringIEqualsText(text, "NOTCREATED"))
		return Loc_Destroyed;
	if (StringIEqualsText(text, "WORN"))
		return Loc_Worn;
	if (StringIEqualsText(text, "CARRIED"))
		return Loc_Carried;
	if (StringIEqualsText(text, "HERE"))
		return Loc_Here;
	return LookupNumeric(ctx, text);
}

static bool ParseIndexedSectionHeader(DC_String header, int* index, DC_String* remainder)
{
	header = TrimString(header);
	if (StringEmpty(header) || !IsDigit(*header.ptr))
		return false;
	const uint8_t* pos = header.ptr;
	while (pos < header.end && IsDigit(*pos))
		pos++;
	*index = ParseInt(DC_String(header.ptr, pos));
	if (remainder != 0)
		*remainder = TrimString(DC_String(pos, header.end));
	return true;
}

static bool ParseSymbolicSectionHeader(DC_Context* ctx, DC_String header, int fallbackIndex, int* index, DC_String* remainder)
{
	header = TrimString(header);
	if (StringEmpty(header) || (!IsDigit(*header.ptr) && !IsIdentifierStart(*header.ptr)))
		return false;
	const uint8_t* pos = header.ptr;
	while (pos < header.end && !IsSpace(*pos))
		pos++;
	DC_String symbol = DC_String(header.ptr, pos);
	bool plainIdentifier = !StringEmpty(symbol) && IsIdentifierStart(*symbol.ptr);
	for (const uint8_t* p = symbol.ptr; p < symbol.end; p++)
	{
		if (!IsIdentifierChar(*p))
		{
			plainIdentifier = false;
			break;
		}
	}
	if (plainIdentifier && FindDefine(ctx, symbol) == 0)
	{
		DC_ProcessID* id = FindProcessID(ctx, symbol);
		if (id == 0)
			id = AppendArray(ctx->arena, &ctx->processIds, &ctx->processIdCount, &ctx->processIdCapacity);
		if (id == 0)
			return false;
		id->name = CopyString(ctx->arena, symbol);
		id->value = fallbackIndex;
		*index = fallbackIndex;
	}
	else
	{
		*index = ParseSymbolicIndex(ctx, symbol, fallbackIndex);
	}
	if (remainder != 0)
		*remainder = TrimString(DC_String(pos, header.end));
	return true;
}

static bool ParseVocabularyLine(DC_Context* ctx, DC_ProgramModel* model, DC_String line)
{
	DC_String fields[8];
	int fieldCount = 0;
	if (!SplitFields(line, fields, 8, &fieldCount) || fieldCount < 3)
	{
		SetFailure(ctx, DCError_SyntaxError, "Invalid vocabulary line");
		return false;
	}
	DC_VocabWord* word = AppendArray(ctx->arena, &model->vocabulary, &model->vocabularyCount, &model->vocabularyCapacity);
	if (word == 0)
		return false;
	word->text = CopyString(ctx->arena, fields[0]);
	word->index = ParseInt(fields[1]);
	word->type = ParseWordType(fields[2]);
	if (word->type == WordType_Unknown)
	{
		SetFailure(ctx, DCError_SyntaxError, "Unknown vocabulary type");
		return false;
	}
	return EncodeWord(ctx, fields[0], word->encoded);
}

static bool ParseConnectionLine(DC_Context* ctx, DC_ProgramModel* model, DC_String line, int location)
{
	if (location < 0)
		return true;
	DC_String fields[4];
	int fieldCount = 0;
	if (!SplitFields(line, fields, 4, &fieldCount) || fieldCount < 2)
	{
		SetFailure(ctx, DCError_SyntaxError, "Invalid connection line");
		return false;
	}
	if (!EnsureArray(ctx->arena, &model->connections, &model->connectionCount, &model->connectionCapacity, (size_t)location + 1))
		return false;
	int word = ResolveWordReference(ctx, model, fields[0], true, WordType_Unknown);
	if (word < 0)
		return false;
	DC_ConnectionList* list = &model->connections[location];
	DC_Connection* connection = AppendArray(ctx->arena, &list->items, &list->count, &list->capacity);
	if (connection == 0)
		return false;
	connection->word = word;
	connection->destination = LookupNumeric(ctx, fields[1]);
	return true;
}

static bool ParseObjectLine(DC_Context* ctx, DC_ProgramModel* model, DC_String line, int objectIndex)
{
	if (objectIndex < 0)
		return true;
	DC_String fields[8];
	int fieldCount = 0;
	if (!SplitFields(line, fields, 8, &fieldCount) || fieldCount < 6)
	{
		DC_String fieldsV2[24];
		if (ctx->opts.version < DDB_VERSION_2 || !SplitFields(line, fieldsV2, 24, &fieldCount) || fieldCount < 22)
		{
			SetFailure(ctx, DCError_SyntaxError, "Invalid object line");
			return false;
		}
		if (!EnsureArray(ctx->arena, &model->objects, &model->objectCount, &model->objectCapacity, (size_t)objectIndex + 1))
			return false;
		DC_ObjectDef* object = &model->objects[objectIndex];
		object->location = ParseObjectLocation(ctx, fieldsV2[0]);
		object->weight = LookupNumeric(ctx, fieldsV2[1]) & Obj_Weight;
		object->container = !IsNullWord(ctx, fieldsV2[2]);
		object->wearable = !IsNullWord(ctx, fieldsV2[3]);
		object->attributes = 0;
		for (int i = 0; i < 16; i++)
		{
			if (!IsNullWord(ctx, fieldsV2[4 + i]))
				object->attributes |= (uint16_t)(1u << (15 - i));
		}
		object->noun = ResolveWordReference(ctx, model, fieldsV2[20], false, WordType_Unknown);
		object->adjective = ResolveWordReference(ctx, model, fieldsV2[21], false, WordType_Adjective);
		return object->noun >= -1 && object->adjective >= -1;
	}
	if (!EnsureArray(ctx->arena, &model->objects, &model->objectCount, &model->objectCapacity, (size_t)objectIndex + 1))
		return false;
	DC_ObjectDef* object = &model->objects[objectIndex];
	object->location = ParseObjectLocation(ctx, fields[0]);
	object->weight = LookupNumeric(ctx, fields[1]) & Obj_Weight;
	object->container = !IsNullWord(ctx, fields[2]);
	object->wearable = !IsNullWord(ctx, fields[3]);
	object->attributes = 0;
	object->noun = ResolveWordReference(ctx, model, fields[4], false, WordType_Unknown);
	object->adjective = ResolveWordReference(ctx, model, fields[5], false, WordType_Adjective);
	return object->noun >= -1 && object->adjective >= -1;
}

static int LookupInstructionWord(DC_Context* ctx, DC_String text, uint8_t type)
{
	text = TrimString(text);
	if (IsNumber(text))
		return ParseInt(text);
	return LookupWordNumber(ctx, text, type);
}

static bool ParseInstruction(DC_Context* ctx, DC_String* fields, int fieldCount, DC_ProcessInstruction* instruction)
{
	if (fieldCount == 0)
		return false;
	char condactName[32];
	size_t nameSize = (size_t)(fields[0].end - fields[0].ptr);
	if (nameSize >= sizeof(condactName))
		nameSize = sizeof(condactName) - 1;
	for (size_t i = 0; i < nameSize; i++)
		condactName[i] = (char)ToUpper(fields[0].ptr[i]);
	condactName[nameSize] = 0;

	uint8_t opcode = 0;
	uint8_t parameterCount = 0;
	if (!DC_FindCondact(ctx->opts.version, condactName, &opcode, &parameterCount))
	{
		SetFailure(ctx, DCError_SyntaxError, "Unknown condact");
		return false;
	}
	instruction->opcode = opcode;
	instruction->indirect = false;
	instruction->parameterCount = parameterCount;
	instruction->parameters[0] = 0;
	instruction->parameters[1] = 0;
	instruction->parameterLabels[0] = 0;
	instruction->parameterLabels[1] = 0;
	for (int i = 0; i < parameterCount; i++)
	{
		if (i + 1 >= fieldCount)
		{
			if (ctx->opts.strict)
			{
				SetFailure(ctx, DCError_SyntaxError, "Missing parameter for condact");
				return false;
			}
			instruction->parameters[i] = 0;
			continue;
		}
		DC_String value = fields[i + 1];
		if (i == 0 && value.end - value.ptr >= 3 && value.ptr[0] == '[' && value.end[-1] == ']')
		{
			if (IsPAWSMode(ctx))
			{
				SetFailure(ctx, DCError_Unsupported, "PAWS does not support indirection");
				return false;
			}
			instruction->indirect = true;
			value.ptr++;
			value.end--;
		}
		if (value.ptr < value.end && value.ptr[0] == '$')
			instruction->parameterLabels[i] = CopyUpperString(ctx->arena, value);
		else if (StringIEqualsText(fields[0], "SYNONYM"))
		{
			instruction->parameters[i] = LookupInstructionWord(ctx, value, i == 0 ? WordType_Verb : WordType_Noun);
			if (instruction->parameters[i] < 0)
				return false;
		}
		else if (StringIEqualsText(fields[0], "NOUN2"))
		{
			instruction->parameters[i] = LookupInstructionWord(ctx, value, WordType_Noun);
			if (instruction->parameters[i] < 0)
				return false;
		}
		else if (StringIEqualsText(fields[0], "ADJECT1") || StringIEqualsText(fields[0], "ADJECT2"))
		{
			instruction->parameters[i] = LookupInstructionWord(ctx, value, WordType_Adjective);
			if (instruction->parameters[i] < 0)
				return false;
		}
		else if (StringIEqualsText(fields[0], "ADVERB"))
		{
			instruction->parameters[i] = LookupInstructionWord(ctx, value, WordType_Adverb);
			if (instruction->parameters[i] < 0)
				return false;
		}
		else if (StringIEqualsText(fields[0], "PREP"))
		{
			instruction->parameters[i] = LookupInstructionWord(ctx, value, WordType_Preposition);
			if (instruction->parameters[i] < 0)
				return false;
		}
		else
			instruction->parameters[i] = LookupNumeric(ctx, value);
	}
	return true;
}

static bool IsStarWord(DC_Context* ctx, DC_String text)
{
	return IsPAWSMode(ctx) && (text.end - text.ptr) == 1 && *text.ptr == '*';
}

static bool AddProcessLabel(DC_Context* ctx, DC_Process* process, DC_String name, int entryIndex)
{
	DC_ProcessLabel* label = AppendArray(ctx->arena, &process->labels, &process->labelCount, &process->labelCapacity);
	if (label == 0)
		return false;
	label->name = CopyUpperString(ctx->arena, name);
	label->entryIndex = entryIndex;
	return true;
}

static bool ParseProcessLine(DC_Context* ctx, DC_Process* process, DC_ProcessEntry** currentEntry, DC_String rawLine, DC_String trimmed)
{
	if (process == 0)
		return true;
	size_t indent = 0;
	while (rawLine.ptr + indent < rawLine.end && rawLine.ptr[indent] == ' ')
		indent++;
	bool continuation = indent >= 3 || (rawLine.ptr < rawLine.end && rawLine.ptr[0] == '\t');
	DC_String fields[16];
	int fieldCount = 0;
	if (!SplitFields(trimmed, fields, 16, &fieldCount) || fieldCount == 0)
		return true;
	if (fields[0].ptr < fields[0].end && fields[0].ptr[0] == '$')
		return AddProcessLabel(ctx, process, fields[0], (int)process->entryCount);

	DC_ProcessEntry localEntry = {};
	DC_ProcessEntry* targetEntry = *currentEntry;
	bool startsNewEntry = false;
	if (!continuation)
	{
		if (fieldCount < 3)
		{
			SetFailure(ctx, DCError_SyntaxError, "Invalid process entry");
			return false;
		}
		startsNewEntry = true;
		localEntry.verb = IsNullWord(ctx, fields[0]) ? 255 : IsStarWord(ctx, fields[0]) ? 1 : LookupWordNumber(ctx, fields[0], WordType_Verb);
		localEntry.noun = IsNullWord(ctx, fields[1]) ? 255 : IsStarWord(ctx, fields[1]) ? 1 : LookupWordNumber(ctx, fields[1], WordType_Noun);
		if (localEntry.verb < 0 || localEntry.noun < 0)
			return false;
		fields[0] = fields[2];
		for (int i = 3; i < fieldCount; i++)
			fields[i - 2] = fields[i];
		fieldCount -= 2;
		targetEntry = &localEntry;
	}
	else if (*currentEntry == 0)
	{
		SetFailure(ctx, DCError_SyntaxError, "Process continuation without entry");
		return false;
	}

	DC_ProcessInstruction* instruction = AppendArray(ctx->arena, &targetEntry->instructions, &targetEntry->instructionCount, &targetEntry->instructionCapacity);
	if (instruction == 0 || !ParseInstruction(ctx, fields, fieldCount, instruction))
		return false;
	if (startsNewEntry)
	{
		DC_ProcessEntry* stored = AppendArray(ctx->arena, &process->entries, &process->entryCount, &process->entryCapacity);
		if (stored == 0)
			return false;
		*stored = localEntry;
		*currentEntry = stored;
	}
	return true;
}

static bool ParseSource(DC_Context* ctx, DC_SourceLine* source, size_t sourceCount, DC_ProgramModel* model)
{
	enum Section
	{
		Section_None,
		Section_CTL,
		Section_TOK,
		Section_VOC,
		Section_STX,
		Section_MTX,
		Section_OTX,
		Section_LTX,
		Section_CON,
		Section_OBJ,
		Section_PRO
	};
	model->controlChar = '_';
	ctx->currentNullWordChar = '_';
	Section section = Section_None;
	int currentIndex = -1;
	DC_Text* currentTextTable = 0;
	size_t* currentTextCount = 0;
	size_t* currentTextCapacity = 0;
	DC_Process* currentProcess = 0;
	DC_ProcessEntry* currentEntry = 0;

	for (size_t i = 0; i < sourceCount; i++)
	{
		ctx->currentSourceFile = source[i].file;
		ctx->currentSourceLine = source[i].line;
		DC_String rawLine = MakeString(source[i].text);
		DC_String line = StripComment(rawLine);
		DC_String trimmed = TrimString(line);
		DC_String rawTrimmed = TrimString(rawLine);
		if (StringEmpty(trimmed))
		{
			if (StringEmpty(rawTrimmed) && currentTextTable != 0 && currentIndex >= 0 && (size_t)currentIndex < *currentTextCount)
			{
				if (IsPAWSMode(ctx))
					PAWSAppendTextBreak(ctx, &currentTextTable[currentIndex]);
				else
					AppendTextBreak(ctx, &currentTextTable[currentIndex]);
			}
			continue;
		}
		if (*trimmed.ptr == '/')
		{
			DC_String header = TrimString(StripComment(DC_String(trimmed.ptr + 1, trimmed.end)));
			DC_String inlineRemainder = {};
			int inlineIndex = -1;
			if (HeaderStartsWithWordI(header, "CTL")) { section = Section_CTL; currentTextTable = 0; currentProcess = 0; continue; }
			if (HeaderStartsWithWordI(header, "TOK")) { section = Section_TOK; currentTextTable = 0; currentProcess = 0; continue; }
			if (HeaderStartsWithWordI(header, "VOC")) { section = Section_VOC; currentTextTable = 0; currentProcess = 0; continue; }
			if (HeaderStartsWithWordI(header, "STX")) { section = Section_STX; currentTextTable = model->systemMessages; currentTextCount = &model->systemMessageCount; currentTextCapacity = &model->systemMessageCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "MTX")) { section = Section_MTX; currentTextTable = model->messages; currentTextCount = &model->messageCount; currentTextCapacity = &model->messageCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "OTX")) { section = Section_OTX; currentTextTable = model->objectTexts; currentTextCount = &model->objectTextCount; currentTextCapacity = &model->objectTextCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "LTX")) { section = Section_LTX; currentTextTable = model->locationTexts; currentTextCount = &model->locationTextCount; currentTextCapacity = &model->locationTextCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "CON")) { section = Section_CON; currentTextTable = 0; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "OBJ")) { section = Section_OBJ; currentTextTable = 0; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (HeaderStartsWithWordI(header, "PRO"))
			{
				section = Section_PRO;
				currentTextTable = 0;
				currentEntry = 0;
				DC_String processText = TrimString(DC_String(header.ptr + 3, header.end));
				int processIndex = ParseSymbolicIndex(ctx, processText, (int)model->processCount);
				if (processIndex < 0)
					currentProcess = 0;
				else
				{
					if (!EnsureArray(ctx->arena, &model->processes, &model->processCount, &model->processCapacity, (size_t)processIndex + 1))
						return false;
					currentProcess = &model->processes[processIndex];
					currentProcess->index = processIndex;
				}
				continue;
			}
			if ((section == Section_STX || section == Section_MTX || section == Section_OTX || section == Section_LTX || section == Section_CON) &&
				ParseSymbolicSectionHeader(ctx, header, currentIndex + 1, &inlineIndex, &inlineRemainder))
			{
				DC_String textSymbol = TrimString(header);
				const uint8_t* textSymbolEnd = textSymbol.ptr;
				while (textSymbolEnd < textSymbol.end && !IsSpace(*textSymbolEnd))
					textSymbolEnd++;
				textSymbol.end = textSymbolEnd;
				bool plainTextSymbol = section != Section_CON && !StringEmpty(textSymbol) && IsIdentifierStart(*textSymbol.ptr);
				for (const uint8_t* p = textSymbol.ptr; p < textSymbol.end; p++)
				{
					if (!IsIdentifierChar(*p))
					{
						plainTextSymbol = false;
						break;
					}
				}
				if (plainTextSymbol)
				{
					inlineIndex = currentIndex + 1;
					DC_ProcessID* id = FindProcessID(ctx, textSymbol);
					if (id == 0)
						id = AppendArray(ctx->arena, &ctx->processIds, &ctx->processIdCount, &ctx->processIdCapacity);
					if (id == 0)
						return false;
					id->name = CopyString(ctx->arena, textSymbol);
					id->value = inlineIndex;
				}
				else if (section != Section_CON && currentIndex >= 0 && inlineIndex <= currentIndex && !IsDigit(*textSymbol.ptr))
				{
					inlineIndex = currentIndex + 1;
				}
				currentIndex = inlineIndex;
				if (section == Section_CON)
				{
					if (!EnsureArray(ctx->arena, &model->connections, &model->connectionCount, &model->connectionCapacity, (size_t)currentIndex + 1))
						return false;
					continue;
				}
				if (!EnsureArray(ctx->arena, &currentTextTable, currentTextCount, currentTextCapacity, (size_t)currentIndex + 1))
					return false;
				if (section == Section_STX) model->systemMessages = currentTextTable;
				if (section == Section_MTX) model->messages = currentTextTable;
				if (section == Section_OTX) model->objectTexts = currentTextTable;
				if (section == Section_LTX) model->locationTexts = currentTextTable;
				continue;
			}
			if (section == Section_OBJ && ParseSymbolicSectionHeader(ctx, header, currentIndex + 1, &inlineIndex, &inlineRemainder))
			{
				DC_String objSymbol = TrimString(header);
				const uint8_t* objSymbolEnd = objSymbol.ptr;
				while (objSymbolEnd < objSymbol.end && !IsSpace(*objSymbolEnd))
					objSymbolEnd++;
				objSymbol.end = objSymbolEnd;
				bool plainObjectSymbol = !StringEmpty(objSymbol) && IsIdentifierStart(*objSymbol.ptr);
				for (const uint8_t* p = objSymbol.ptr; p < objSymbol.end; p++)
				{
					if (!IsIdentifierChar(*p))
					{
						plainObjectSymbol = false;
						break;
					}
				}
				if (plainObjectSymbol)
				{
					inlineIndex = currentIndex + 1;
					DC_ProcessID* id = FindProcessID(ctx, objSymbol);
					if (id == 0)
						id = AppendArray(ctx->arena, &ctx->processIds, &ctx->processIdCount, &ctx->processIdCapacity);
					if (id == 0)
						return false;
					id->name = CopyString(ctx->arena, objSymbol);
					id->value = inlineIndex;
					char objectIndexText[32];
					LongToChar(inlineIndex, objectIndexText, 10);
					SetDefine(ctx, objSymbol, MakeString(objectIndexText));
				}
				currentIndex = inlineIndex;
				if (!EnsureArray(ctx->arena, &model->objects, &model->objectCount, &model->objectCapacity, (size_t)currentIndex + 1))
					return false;
				if (!StringEmpty(inlineRemainder) && !ParseObjectLine(ctx, model, inlineRemainder, currentIndex))
					return false;
				continue;
			}
		}

		switch (section)
		{
			case Section_CTL:
				model->controlChar = (char)*trimmed.ptr;
				ctx->currentNullWordChar = (char)*trimmed.ptr;
				break;
			case Section_TOK:
			{
				DC_Text* token = AppendArray(ctx->arena, &model->tokens, &model->tokenCount, &model->tokenCapacity);
				if (token == 0)
					return false;
				CaptureTextOrigin(ctx, token);
				for (const uint8_t* p = trimmed.ptr; p < trimmed.end; p++)
				{
					uint8_t c = *p == (uint8_t)ctx->currentNullWordChar ? ' ' : *p;
					AppendTextBytes(ctx, token, &c, 1);
				}
				break;
			}
			case Section_VOC:
				if (!ParseVocabularyLine(ctx, model, trimmed))
					return false;
				ctx->parsedVocabulary = model->vocabulary;
				ctx->parsedVocabularyCount = model->vocabularyCount;
				break;
			case Section_STX:
			case Section_MTX:
			case Section_OTX:
			case Section_LTX:
				if (currentTextTable == 0 || currentIndex < 0)
				{
					SetFailure(ctx, DCError_SyntaxError, "Text outside item");
					break;
				}
				if (IsPAWSMode(ctx))
				{
					if (!ValidateTextLineEncoding(ctx, rawLine, true))
						return false;
					PAWSAppendTextLine(ctx, &currentTextTable[currentIndex], rawLine);
					break;
				}
				if (!ValidateTextLineEncoding(ctx, line, true))
					return false;
				AppendTextLine(ctx, &currentTextTable[currentIndex], line);
				break;
			case Section_CON:
				if (!ParseConnectionLine(ctx, model, trimmed, currentIndex))
					return false;
				break;
			case Section_OBJ:
				if (!ParseObjectLine(ctx, model, trimmed, currentIndex))
					return false;
				break;
			case Section_PRO:
				if (!ParseProcessLine(ctx, currentProcess, &currentEntry, rawLine, trimmed))
				{
					currentEntry = 0;
					return false;
				}
				break;
			default:
				break;
		}
	}
	return true;
}

static bool IsLittleEndianTarget(DDB_Machine machine)
{
	return machine != DDB_MACHINE_ATARIST && machine != DDB_MACHINE_AMIGA;
}

static bool UsesV2HeaderExtension(DDB_Machine machine)
{
	return machine == DDB_MACHINE_IBMPC || machine == DDB_MACHINE_ATARIST || machine == DDB_MACHINE_AMIGA;
}

static uint16_t GetBaseOffset(DDB_Machine machine)
{
	switch (machine)
	{
		case DDB_MACHINE_SPECTRUM: return 0x8400;
		case DDB_MACHINE_C64: return 0x3880;
		case DDB_MACHINE_CPC: return 0x2880;
		case DDB_MACHINE_MSX: return 0x0100;
		case DDB_MACHINE_PLUS4: return 0x7080;
		default: return 0;
	}
}

static int GetColumnCount(DDB_Machine machine)
{
	switch (machine)
	{
		case DDB_MACHINE_SPECTRUM: return 42;
		case DDB_MACHINE_C64:
		case DDB_MACHINE_CPC:
		case DDB_MACHINE_MSX:
		case DDB_MACHINE_MSX2:
		case DDB_MACHINE_PLUS4: return 40;
		case DDB_MACHINE_PCW: return 96;
		case DDB_MACHINE_IBMPC:
		case DDB_MACHINE_ATARIST:
		case DDB_MACHINE_AMIGA: return 53;
		default: return 40;
	}
}

static int CompareVocabulary(const DC_VocabWord* a, const DC_VocabWord* b)
{
	int cmp = MemComp((void*)a->encoded, (void*)b->encoded, 5);
	if (cmp != 0) return cmp;
	if (a->index != b->index) return a->index - b->index;
	if (a->type != b->type) return (int)a->type - (int)b->type;
	return StrIComp(a->text, b->text);
}

static void SortVocabulary(DC_VocabWord* words, size_t count)
{
	for (size_t i = 1; i < count; i++)
	{
		DC_VocabWord value = words[i];
		size_t j = i;
		while (j > 0 && CompareVocabulary(&words[j - 1], &value) > 0)
		{
			words[j] = words[j - 1];
			j--;
		}
		words[j] = value;
	}
}

static bool AppendVocabularyWord(DC_Context* ctx, DC_BufferBuilder* data, const DC_VocabWord* word)
{
	for (int i = 0; i < 5; i++)
	{
		if (!AppendByte(ctx, data, word->encoded[i] ^ 0xFF))
			return false;
	}
	return AppendByte(ctx, data, (uint8_t)word->index) && AppendByte(ctx, data, word->type);
}

static uint16_t AppendMessageTable(DC_Context* ctx, DC_BufferBuilder* data, DC_Text* table, size_t count, bool littleEndian,
	DC_Text* tokens, size_t tokenCount, bool compress)
{
	uint16_t* messageOffsets = Allocate<uint16_t>(ctx->arena, (unsigned)count, false);
	if (count != 0 && messageOffsets == 0)
		return 0;
	for (size_t i = 0; i < count; i++)
	{
		ctx->currentSourceFile = table[i].sourceFile;
		ctx->currentSourceLine = table[i].sourceLine;
		messageOffsets[i] = (uint16_t)data->size;
		if (table[i].data != 0)
		{
			if (!AppendMessage(ctx, data, MakeString(table[i].data), tokens, tokenCount, compress))
				return 0;
		}
		else
		{
			if (!AppendByte(ctx, data, 0x0A ^ 0xFF))
				return 0;
		}
	}
	if ((data->size & 1) != 0)
	{
		if (!AppendByte(ctx, data, 0))
			return 0;
	}
	uint16_t tableOffset = (uint16_t)data->size;
	if (!ResizeBytes(ctx, data, data->size + count * 2))
		return 0;
	for (size_t i = 0; i < count; i++)
		Write16(data->data, tableOffset + i * 2, messageOffsets[i], littleEndian);
	return tableOffset;
}

static uint16_t AppendConnections(DC_Context* ctx, DC_BufferBuilder* data, DC_ConnectionList* connections, size_t count, bool littleEndian)
{
	uint16_t* entryOffsets = Allocate<uint16_t>(ctx->arena, (unsigned)count, false);
	if (count != 0 && entryOffsets == 0)
		return 0;
	for (size_t i = 0; i < count; i++)
	{
		entryOffsets[i] = (uint16_t)data->size;
		for (size_t n = 0; n < connections[i].count; n++)
		{
			AppendByte(ctx, data, (uint8_t)connections[i].items[n].word);
			AppendByte(ctx, data, (uint8_t)connections[i].items[n].destination);
		}
		AppendByte(ctx, data, 0xFF);
	}
	if ((data->size & 1) != 0)
		AppendByte(ctx, data, 0);
	uint16_t tableOffset = (uint16_t)data->size;
	ResizeBytes(ctx, data, data->size + count * 2);
	for (size_t i = 0; i < count; i++)
		Write16(data->data, tableOffset + i * 2, entryOffsets[i], littleEndian);
	return tableOffset;
}

static uint16_t AppendObjectLocations(DC_Context* ctx, DC_BufferBuilder* data, DC_ObjectDef* objects, size_t count)
{
	uint16_t offset = (uint16_t)data->size;
	for (size_t i = 0; i < count; i++)
		AppendByte(ctx, data, (uint8_t)objects[i].location);
	return offset;
}

static uint16_t AppendObjectWords(DC_Context* ctx, DC_BufferBuilder* data, DC_ObjectDef* objects, size_t count)
{
	uint16_t offset = (uint16_t)data->size;
	for (size_t i = 0; i < count; i++)
	{
		AppendByte(ctx, data, (uint8_t)objects[i].noun);
		AppendByte(ctx, data, objects[i].adjective < 0 ? 255 : (uint8_t)objects[i].adjective);
	}
	return offset;
}

static uint16_t AppendObjectAttributes(DC_Context* ctx, DC_BufferBuilder* data, DC_ObjectDef* objects, size_t count)
{
	uint16_t offset = (uint16_t)data->size;
	for (size_t i = 0; i < count; i++)
	{
		uint8_t flags = (uint8_t)objects[i].weight;
		if (objects[i].container) flags |= Obj_Container;
		if (objects[i].wearable) flags |= Obj_Wearable;
		AppendByte(ctx, data, flags);
	}
	return offset;
}

static uint16_t AppendObjectExtendedAttributes(DC_Context* ctx, DC_BufferBuilder* data, DC_ObjectDef* objects, size_t count, bool littleEndian)
{
	uint16_t offset = (uint16_t)data->size;
	for (size_t i = 0; i < count; i++)
	{
		size_t attrOffset = data->size;
		AppendByte(ctx, data, 0);
		AppendByte(ctx, data, 0);
		Write16(data->data, attrOffset, objects[i].attributes, littleEndian);
	}
	return offset;
}

static int FindProcessLabelEntry(const DC_Process* process, const char* name)
{
	for (size_t i = 0; i < process->labelCount; i++)
	{
		if (StrComp(process->labels[i].name, name) == 0)
			return process->labels[i].entryIndex;
	}
	return -1;
}

static uint16_t AppendProcesses(DC_Context* ctx, DC_BufferBuilder* data, DC_Process* processes, size_t count, bool littleEndian)
{
	uint16_t* processOffsets = Allocate<uint16_t>(ctx->arena, (unsigned)count, false);
	if (count != 0 && processOffsets == 0)
		return 0;
	AppendByte(ctx, data, 0xFF);
	for (size_t i = 0; i < count; i++)
	{
		uint16_t* codeOffsets = Allocate<uint16_t>(ctx->arena, (unsigned)processes[i].entryCount, false);
		if (processes[i].entryCount != 0 && codeOffsets == 0)
			return 0;
		for (size_t e = 0; e < processes[i].entryCount; e++)
		{
			uint16_t codeOffset = (uint16_t)data->size;
			codeOffsets[e] = codeOffset;
			DC_ProcessEntry* entry = &processes[i].entries[e];
			for (size_t n = 0; n < entry->instructionCount; n++)
			{
				DC_ProcessInstruction* inst = &entry->instructions[n];
				AppendByte(ctx, data, (uint8_t)(inst->opcode | (inst->indirect ? 0x80 : 0)));
				for (int p = 0; p < inst->parameterCount; p++)
				{
					int value = inst->parameters[p];
					if (inst->parameterLabels[p] != 0)
					{
						int targetEntry = FindProcessLabelEntry(&processes[i], inst->parameterLabels[p]);
						value = targetEntry < 0 ? 0 : targetEntry - (int)e - 1;
					}
					AppendByte(ctx, data, (uint8_t)value);
				}
			}
			AppendByte(ctx, data, 0xFF);
		}
		if ((data->size & 1) != 0)
			AppendByte(ctx, data, 0);
		uint16_t entriesOffset = (uint16_t)data->size;
		processOffsets[i] = entriesOffset;
		for (size_t e = 0; e < processes[i].entryCount; e++)
		{
			AppendByte(ctx, data, (uint8_t)processes[i].entries[e].verb);
			AppendByte(ctx, data, (uint8_t)processes[i].entries[e].noun);
			size_t offsetPatch = data->size;
			AppendByte(ctx, data, 0);
			AppendByte(ctx, data, 0);
			Write16(data->data, offsetPatch, codeOffsets[e], littleEndian);
		}
		AppendByte(ctx, data, 0);
		AppendByte(ctx, data, 0);
	}
	if ((data->size & 1) != 0)
		AppendByte(ctx, data, 0);
	uint16_t processTableOffset = (uint16_t)data->size;
	ResizeBytes(ctx, data, data->size + count * 2);
	for (size_t i = 0; i < count; i++)
		Write16(data->data, processTableOffset + i * 2, processOffsets[i], littleEndian);
	return processTableOffset;
}

static void WriteHeaderPointer(uint8_t* header, size_t offset, uint16_t value, bool littleEndian, uint16_t baseOffset, bool present)
{
	uint16_t stored = present ? (uint16_t)(value + baseOffset) : 0;
	if (littleEndian)
	{
		header[offset] = (uint8_t)(stored & 0xFF);
		header[offset + 1] = (uint8_t)(stored >> 8);
	}
	else
	{
		header[offset] = (uint8_t)(stored >> 8);
		header[offset + 1] = (uint8_t)(stored & 0xFF);
	}
}

static void WriteHeader(DC_Context* ctx, uint8_t* header, DDB_Machine outputTarget, bool littleEndian, uint16_t baseOffset, DC_ProgramModel* model,
	uint16_t processTableOffset, uint16_t objNamTableOffset, uint16_t locDescTableOffset, uint16_t msgTableOffset, uint16_t sysMsgTableOffset,
	uint16_t conTableOffset, uint16_t vocabularyOffset, uint16_t objLocOffset, uint16_t objWordsOffset, uint16_t objAttrOffset,
	uint16_t objExAttrOffset, uint16_t tokensOffset)
{
	header[0] = (uint8_t)ctx->opts.version;
	header[1] = (uint8_t)(((uint8_t)outputTarget << 4) | ((uint8_t)ctx->opts.language & 0x0F));
	header[2] = (uint8_t)model->controlChar;
	header[3] = (uint8_t)model->objectCount;
	header[4] = (uint8_t)model->locationTextCount;
	header[5] = (uint8_t)model->messageCount;
	header[6] = (uint8_t)model->systemMessageCount;
	header[7] = (uint8_t)model->processCount;
	WriteHeaderPointer(header, 0x08, tokensOffset, littleEndian, baseOffset, tokensOffset != 0);
	WriteHeaderPointer(header, 0x0A, processTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x0C, objNamTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x0E, locDescTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x10, msgTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x12, sysMsgTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x14, conTableOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x16, vocabularyOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x18, objLocOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x1A, objWordsOffset, littleEndian, baseOffset, true);
	WriteHeaderPointer(header, 0x1C, objAttrOffset, littleEndian, baseOffset, true);
	uint16_t declaredSize = (uint16_t)(baseOffset + (uint16_t)ctx->compiledSize);
	if (ctx->opts.version >= DDB_VERSION_2)
	{
		WriteHeaderPointer(header, 0x1E, objExAttrOffset, littleEndian, baseOffset, true);
		if (littleEndian)
		{
			header[0x20] = (uint8_t)(declaredSize & 0xFF);
			header[0x21] = (uint8_t)(declaredSize >> 8);
		}
		else
		{
			header[0x20] = (uint8_t)(declaredSize >> 8);
			header[0x21] = (uint8_t)(declaredSize & 0xFF);
		}
		WriteHeaderPointer(header, 0x22, 0, littleEndian, 0, false);
		return;
	}
	if (littleEndian)
	{
		header[0x1E] = (uint8_t)(declaredSize & 0xFF);
		header[0x1F] = (uint8_t)(declaredSize >> 8);
	}
	else
	{
		header[0x1E] = (uint8_t)(declaredSize >> 8);
		header[0x1F] = (uint8_t)(declaredSize & 0xFF);
	}
	WriteHeaderPointer(header, 0x20, 0, littleEndian, 0, false);
}

static bool BuildDDB(DC_Context* ctx, DC_ProgramModel* model, DC_BufferBuilder* output)
{
	DC_BufferBuilder data = {};
	ResizeBytes(ctx, &data, ctx->opts.version >= DDB_VERSION_2 ? 36 : 34);
	DDB_Machine outputTarget = ctx->opts.target;
	bool littleEndian = IsLittleEndianTarget(outputTarget);
	if (ctx->opts.version >= DDB_VERSION_2 && UsesV2HeaderExtension(outputTarget))
		ResizeBytes(ctx, &data, data.size + 24);

	uint16_t vocabularyOffset = (uint16_t)data.size;
	DC_VocabWord* sorted = Allocate<DC_VocabWord>(ctx->arena, (unsigned)model->vocabularyCount, false);
	if (model->vocabularyCount != 0 && sorted == 0)
		return false;
	if (model->vocabularyCount != 0)
		MemCopy(sorted, model->vocabulary, sizeof(DC_VocabWord) * model->vocabularyCount);
	SortVocabulary(sorted, model->vocabularyCount);
	for (size_t i = 0; i < model->vocabularyCount; i++)
		AppendVocabularyWord(ctx, &data, &sorted[i]);
	AppendByte(ctx, &data, 0);

	uint16_t tokensOffset = 0;
	if (model->tokenCount != 0)
	{
		tokensOffset = (uint16_t)data.size;
		if (!AppendByte(ctx, &data, 0xFF))
			return false;
		for (size_t i = 0; i < model->tokenCount; i++)
		{
			ctx->currentSourceFile = model->tokens[i].sourceFile;
			ctx->currentSourceLine = model->tokens[i].sourceLine;
			if (!AppendToken(ctx, &data, MakeString(model->tokens[i].data != 0 ? model->tokens[i].data : "")))
				return false;
		}
		if (model->tokenCount < 128)
		{
			if (!AppendByte(ctx, &data, 0))
				return false;
		}
	}

	if (model->connectionCount < model->locationTextCount &&
		!EnsureArray(ctx->arena, &model->connections, &model->connectionCount, &model->connectionCapacity, model->locationTextCount))
		return false;
	if (model->objectTextCount < model->objectCount &&
		!EnsureArray(ctx->arena, &model->objectTexts, &model->objectTextCount, &model->objectTextCapacity, model->objectCount))
		return false;
	uint16_t sysTableOffset = AppendMessageTable(ctx, &data, model->systemMessages, model->systemMessageCount, littleEndian, model->tokens, model->tokenCount, false);
	uint16_t msgTableOffset = AppendMessageTable(ctx, &data, model->messages, model->messageCount, littleEndian, model->tokens, model->tokenCount, false);
	uint16_t objTableOffset = AppendMessageTable(ctx, &data, model->objectTexts, model->objectTextCount, littleEndian, model->tokens, model->tokenCount, false);
	uint16_t locTableOffset = AppendMessageTable(ctx, &data, model->locationTexts, model->locationTextCount, littleEndian, model->tokens, model->tokenCount, true);
	if ((model->systemMessageCount != 0 && sysTableOffset == 0) ||
		(model->messageCount != 0 && msgTableOffset == 0) ||
		(model->objectTextCount != 0 && objTableOffset == 0) ||
		(model->locationTextCount != 0 && locTableOffset == 0))
	{
		if (DC_GetError() == DCError_None)
			SetFailure(ctx, DCError_OutOfMemory, "Failed to build message tables");
		return false;
	}
	uint16_t conTableOffset = AppendConnections(ctx, &data, model->connections, model->locationTextCount, littleEndian);
	uint16_t objWordsOffset = AppendObjectWords(ctx, &data, model->objects, model->objectCount);
	uint16_t objAttrOffset = AppendObjectAttributes(ctx, &data, model->objects, model->objectCount);
	if ((data.size & 1) != 0)
		AppendByte(ctx, &data, 0);
	uint16_t objExAttrOffset = 0;
	if (ctx->opts.version >= DDB_VERSION_2)
	{
		objExAttrOffset = AppendObjectExtendedAttributes(ctx, &data, model->objects, model->objectCount, littleEndian);
	}
	uint16_t objLocOffset = AppendObjectLocations(ctx, &data, model->objects, model->objectCount);
	uint16_t processTableOffset = AppendProcesses(ctx, &data, model->processes, model->processCount, littleEndian);
	if (data.size > MAX_DDB_SIZE)
	{
		SetFailure(ctx, DCError_WriteError, "Compiled DDB exceeds 64K");
		return false;
	}
	ctx->compiledSize = data.size;
	WriteHeader(ctx, data.data, outputTarget, littleEndian, GetBaseOffset(outputTarget), model, processTableOffset, objTableOffset, locTableOffset,
		msgTableOffset, sysTableOffset, conTableOffset, vocabularyOffset, objLocOffset, objWordsOffset, objAttrOffset, objExAttrOffset, tokensOffset);
	*output = data;
	return true;
}


/* ─────────────────────────── PAWS .SDB output ───────────────────────────── */

// The layout produced here follows the PAW B-series editor byte for byte;
// the structures are described in PAWS/docs/database-format.md and in
// docs/SDB Specs.txt.

struct DC_PAWSToken
{
	const uint8_t* text;
	size_t size;
};

struct DC_PAWSDonor
{
	uint8_t* memory;				// 64K Spectrum image, or 0 when absent
	DC_PAWSToken tokens[96];
	size_t tokenCount;
	const uint8_t* dict;			// raw dictionary bytes (1 or 222)
	size_t dictSize;
};

enum
{
	PAWS_BASE          = 0x9300,
	PAWS_UDGS          = 0x9300,	// 19 UDGs + 16 shade patterns
	PAWS_MISC          = 0x9418,	// miscellaneous configuration area
	PAWS_CHARSET       = 0x9419,	// default character set (0: ROM font)
	PAWS_PAGE_MAP1     = 0x941A,
	PAWS_PAGE_MAP2     = 0x9428,
	PAWS_DISPLAY_PAIRS = 0x9437,	// INK/PAPER/FLASH/BRIGHT/INVERSE/OVER
	PAWS_BORDER        = 0x9443,
	PAWS_COUNTS        = 0x9444,	// objects/locations/messages/sysmsgs/processes/fonts
	PAWS_FONTS_PTR     = 0x944A,
	PAWS_DICT_PTR      = 0x944C,
	PAWS_FONTS         = 0x944E,
	PAWS_DICT_SIZE     = 222,
	PAWS_TRAILER       = 0xFFD9,	// table pointers through 0xFFEB
	PAWS_PART_A_END    = 0xFFED,
	PAWS_PART_B_START  = 0xFFEF,
	PAWS_GFX_DIR       = 0xFFF1,
	PAWS_GFX_FLAGS     = 0xFFF3,
	PAWS_GFX_CONTROL   = 0xFFF5,
	PAWS_CONTROL_BYTE  = 0xFFF7,
	PAWS_TRAILER_COUNTS= 0xFFF8,	// messages/sysmsgs/objects/locations/processes
	PAWS_BASE_WORD     = 0xFFFD,
	PAWS_PAGE_BYTE     = 0xFFFF,
	PAWS_MIN_PROCESSES = 3,
	PAWS_MIN_SYSMSGS   = 55,
};

static uint16_t PAWSRead16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void PAWSWrite16(uint8_t* p, uint16_t value)
{
	p[0] = (uint8_t)(value & 0xFF);
	p[1] = (uint8_t)(value >> 8);
}

static uint32_t PAWSCRC32(const uint8_t* data, size_t size)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t n = 0; n < size; n++)
	{
		crc ^= data[n];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1));
	}
	return crc ^ 0xFFFFFFFFU;
}

static bool LoadPAWSDonor(DC_Context* ctx, DC_PAWSDonor* donor)
{
	donor->memory = 0;
	donor->tokenCount = 0;
	donor->dict = 0;
	donor->dictSize = 0;
	if (ctx->opts.pawsDonor == 0)
		return true;

	File* file = File_Open(ctx->opts.pawsDonor, ReadOnly);
	if (file == 0)
	{
		SetFailure(ctx, DCError_FileNotFound, "Unable to open donor database");
		return false;
	}
	uint64_t size64 = File_GetSize(file);
	if (size64 < 24 + 2 * 16 || size64 > 0x7FFFFFFF)
	{
		File_Close(file);
		SetFailure(ctx, DCError_ReadError, "Donor database is not a valid SDB file");
		return false;
	}
	size_t size = (size_t)size64;
	uint8_t* data = Allocate<uint8_t>(ctx->arena, (unsigned)size, false);
	if (data == 0 || File_Read(file, data, size) != size)
	{
		File_Close(file);
		SetFailure(ctx, DCError_ReadError, "Unable to read donor database");
		return false;
	}
	File_Close(file);

	if (data[0] != 'S' || data[1] != 'D' || data[2] != 'B' || data[3] != 0x1A ||
		data[4] != 1)
	{
		SetFailure(ctx, DCError_ReadError, "Donor database is not a valid SDB file");
		return false;
	}
	if (data[6] != 48)
	{
		SetFailure(ctx, DCError_Unsupported, "128K donor databases are not supported yet");
		return false;
	}

	uint8_t* memory = Allocate<uint8_t>(ctx->arena, 65536, true);
	if (memory == 0)
		return false;
	uint16_t segmentCount = PAWSRead16(data + 12);
	uint32_t directoryOffset = 24;
	if (segmentCount == 0 || segmentCount > 16)
	{
		SetFailure(ctx, DCError_ReadError, "Donor database is not a valid SDB file");
		return false;
	}
	for (uint16_t n = 0; n < segmentCount; n++)
	{
		const uint8_t* entry = data + directoryOffset + n * 16;
		uint16_t load = PAWSRead16(entry + 2);
		uint16_t length = PAWSRead16(entry + 4);
		uint32_t payload = (uint32_t)PAWSRead16(entry + 8) | ((uint32_t)PAWSRead16(entry + 10) << 16);
		if (length == 0 || (uint32_t)load + length > 0x10000 || (uint64_t)payload + length > size ||
			PAWSCRC32(data + payload, length) != ((uint32_t)PAWSRead16(entry + 12) | ((uint32_t)PAWSRead16(entry + 14) << 16)))
		{
			SetFailure(ctx, DCError_ReadError, "Donor database is corrupt");
			return false;
		}
		MemCopy(memory + load, data + payload, length);
	}
	if (PAWSRead16(memory + PAWS_BASE_WORD) != PAWS_BASE ||
		memory[PAWS_DISPLAY_PAIRS] != 16 || memory[PAWS_DISPLAY_PAIRS + 2] != 17)
	{
		SetFailure(ctx, DCError_ReadError, "Donor file does not contain a PAW database");
		return false;
	}
	donor->memory = memory;

	uint16_t dictPtr = PAWSRead16(memory + PAWS_DICT_PTR);
	if (memory[dictPtr] == 0xFF)
	{
		donor->dict = memory + dictPtr;
		donor->dictSize = 1;
		return true;
	}
	donor->dict = memory + dictPtr;
	donor->dictSize = PAWS_DICT_SIZE;

	// The dictionary parses as bit-7 terminated entries; the first one is the
	// scanner sentinel, and the rest expand text bytes 0xA5 and up.
	const uint8_t* p = donor->dict;
	const uint8_t* end = donor->dict + PAWS_DICT_SIZE;
	const uint8_t* start = p;
	bool sentinel = true;
	while (p < end)
	{
		if (*p++ & 0x80)
		{
			if (sentinel)
				sentinel = false;
			else if (donor->tokenCount < 96)
			{
				donor->tokens[donor->tokenCount].text = start;
				donor->tokens[donor->tokenCount].size = (size_t)(p - start);
				donor->tokenCount++;
			}
			start = p;
		}
	}
	return true;
}

static bool PAWSEmit(DC_Context* ctx, uint8_t* memory, uint32_t* address, uint32_t limit, const void* data, size_t size)
{
	if (*address + size > limit)
	{
		SetFailure(ctx, DCError_WriteError, "Database exceeds the available Spectrum memory");
		return false;
	}
	if (size != 0)
		MemCopy(memory + *address, data, size);
	*address += (uint32_t)size;
	return true;
}

static bool PAWSEmitByte(DC_Context* ctx, uint8_t* memory, uint32_t* address, uint32_t limit, uint8_t value)
{
	return PAWSEmit(ctx, memory, address, limit, &value, 1);
}

static bool PAWSEmitWord(DC_Context* ctx, uint8_t* memory, uint32_t* address, uint32_t limit, uint16_t value)
{
	uint8_t bytes[2];
	PAWSWrite16(bytes, value);
	return PAWSEmit(ctx, memory, address, limit, bytes, 2);
}

// PAW compresses text by scanning the dictionary in order and claiming every
// remaining match of each token, exactly like the DAAD /TOK compressor.
static bool AppendPAWSMessage(DC_Context* ctx, uint8_t* memory, uint32_t* address, uint32_t limit,
	DC_Text* text, DC_PAWSDonor* donor, bool compress)
{
	DC_BufferBuilder plain = {};
	if (text->data != 0)
	{
		ctx->currentSourceFile = text->sourceFile;
		ctx->currentSourceLine = text->sourceLine;
		if (!AppendMessagePlainBytes(ctx, &plain, MakeString(text->data)))
			return false;
	}

	int* tokenAt = 0;
	bool* covered = 0;
	compress = compress && !ctx->opts.pawsNoCompression && donor->tokenCount != 0 && plain.size != 0;
	if (compress)
	{
		tokenAt = Allocate<int>(ctx->arena, (unsigned)plain.size, false);
		covered = Allocate<bool>(ctx->arena, (unsigned)plain.size, false);
		if (tokenAt == 0 || covered == 0)
			return false;
		for (size_t i = 0; i < plain.size; i++)
		{
			tokenAt[i] = -1;
			covered[i] = false;
		}
		for (size_t n = 0; n < donor->tokenCount; n++)
		{
			size_t tokenSize = donor->tokens[n].size;
			for (size_t i = 0; i + tokenSize <= plain.size;)
			{
				bool match = true;
				for (size_t k = 0; k < tokenSize; k++)
				{
					if (covered[i + k] || tokenAt[i + k] >= 0 ||
						plain.data[i + k] != (donor->tokens[n].text[k] & 0x7F))
					{
						match = false;
						break;
					}
				}
				if (match)
				{
					tokenAt[i] = (int)n;
					for (size_t k = 1; k < tokenSize; k++)
						covered[i + k] = true;
					i += tokenSize;
				}
				else
					i++;
			}
		}
	}

	for (size_t i = 0; i < plain.size; i++)
	{
		if (covered != 0 && covered[i])
			continue;
		uint8_t value = tokenAt != 0 && tokenAt[i] >= 0 ? (uint8_t)(0xA5 + tokenAt[i]) : (uint8_t)plain.data[i];
		if (!PAWSEmitByte(ctx, memory, address, limit, value ^ 0xFF))
			return false;
	}
	return PAWSEmitByte(ctx, memory, address, limit, 0x1F ^ 0xFF);
}

static int ComparePAWSVocabulary(const DC_VocabWord* a, const DC_VocabWord* b)
{
	return a->index - b->index;
}

static void SortPAWSVocabulary(DC_VocabWord* words, size_t count)
{
	for (size_t i = 1; i < count; i++)
	{
		DC_VocabWord value = words[i];
		size_t j = i;
		while (j > 0 && ComparePAWSVocabulary(&words[j - 1], &value) > 0)
		{
			words[j] = words[j - 1];
			j--;
		}
		words[j] = value;
	}
}

static bool AppendPAWSVocabularyWord(DC_Context* ctx, uint8_t* memory, uint32_t* address, uint32_t limit,
	const uint8_t* encoded, uint8_t value, uint8_t type)
{
	for (int i = 0; i < 5; i++)
	{
		if (!PAWSEmitByte(ctx, memory, address, limit, encoded[i] ^ 0xFF))
			return false;
	}
	return PAWSEmitByte(ctx, memory, address, limit, value) &&
		PAWSEmitByte(ctx, memory, address, limit, type);
}

static bool BuildPAWS(DC_Context* ctx, DC_ProgramModel* model, DC_BufferBuilder* output)
{
	DC_PAWSDonor donor;
	if (!LoadPAWSDonor(ctx, &donor))
		return false;

	// A PAW database always holds the response table plus processes 1 and 2,
	// and the full default system message set.
	if (model->processCount < PAWS_MIN_PROCESSES &&
		!EnsureArray(ctx->arena, &model->processes, &model->processCount, &model->processCapacity, PAWS_MIN_PROCESSES))
		return false;
	if (model->systemMessageCount < PAWS_MIN_SYSMSGS &&
		!EnsureArray(ctx->arena, &model->systemMessages, &model->systemMessageCount, &model->systemMessageCapacity, PAWS_MIN_SYSMSGS))
		return false;
	if (model->connectionCount < model->locationTextCount &&
		!EnsureArray(ctx->arena, &model->connections, &model->connectionCount, &model->connectionCapacity, model->locationTextCount))
		return false;
	if (model->objectCount < model->objectTextCount &&
		!EnsureArray(ctx->arena, &model->objects, &model->objectCount, &model->objectCapacity, model->objectTextCount))
		return false;
	if (model->objectTextCount < model->objectCount &&
		!EnsureArray(ctx->arena, &model->objectTexts, &model->objectTextCount, &model->objectTextCapacity, model->objectCount))
		return false;
	if (model->locationTextCount > 255 || model->objectCount > 255 ||
		model->messageCount > 255 || model->systemMessageCount > 255 || model->processCount > 255)
	{
		SetFailure(ctx, DCError_SemanticError, "Too many items for a PAW database");
		return false;
	}

	uint8_t* memory = Allocate<uint8_t>(ctx->arena, 65536, true);
	if (memory == 0)
		return false;

	size_t locationCount = model->locationTextCount;

	// Part B is laid out first so its start can bound the part A allocation.
	// With a donor of matching size the graphics area is reused verbatim;
	// otherwise every location gets an empty drawstring, mirroring the
	// editor's initial database.
	uint32_t partBStart;
	bool donorGraphics = donor.memory != 0 && donor.memory[0xFFFB] == (uint8_t)locationCount;
	if (donor.memory != 0 && !donorGraphics)
	{
		SetFailure(ctx, DCError_SemanticError,
			"Donor database location count differs from the source, graphics cannot be reused");
		return false;
	}
	if (donorGraphics)
	{
		partBStart = PAWSRead16(donor.memory + PAWS_PART_B_START);
		MemCopy(memory + partBStart, donor.memory + partBStart, 0x10000 - partBStart);
	}
	else
	{
		uint32_t flagsStart = 0xFFD4 - (uint32_t)locationCount;	// flags end at 0xFFD3, control data at 0xFFD4
		uint32_t directoryStart = flagsStart - 4 - 2 * (uint32_t)locationCount;
		uint32_t emptyDrawString = directoryStart - 1;
		partBStart = emptyDrawString;
		memory[emptyDrawString] = 0x07;
		for (size_t n = 0; n < locationCount; n++)
			PAWSWrite16(memory + directoryStart + 2 * n, (uint16_t)emptyDrawString);
		PAWSWrite16(memory + directoryStart + 2 * locationCount, (uint16_t)directoryStart);
		memory[flagsStart - 2] = 0xFF;
		memory[flagsStart - 1] = 0xFF;
		MemSet(memory + flagsStart, 0x07, locationCount);
		memory[0xFFD4] = 0xFF;
		PAWSWrite16(memory + PAWS_GFX_DIR, (uint16_t)directoryStart);
		PAWSWrite16(memory + PAWS_GFX_FLAGS, (uint16_t)flagsStart);
		PAWSWrite16(memory + PAWS_GFX_CONTROL, 0xFFD4);
		memory[PAWS_CONTROL_BYTE] = 0x02;
	}

	// Fixed part A prefix: UDGs, shade patterns and display configuration.
	if (donor.memory != 0)
		MemCopy(memory + PAWS_BASE, donor.memory + PAWS_BASE, PAWS_FONTS - PAWS_BASE);
	else
	{
		static const uint8_t pageMap[] = { 0x00, 0x00, 0xFF, 0x01, 0xFF, 0x03, 0xFF, 0x04, 0xFF, 0x06, 0xFF, 0x07, 0xFF, 0xFF };
		static const uint8_t displayPairs[] = { 16, 9, 17, 0, 18, 0, 19, 0, 20, 0, 21, 0 };
		memory[PAWS_MISC] = 20;
		memory[PAWS_CHARSET] = 0;
		MemCopy(memory + PAWS_PAGE_MAP1, pageMap, sizeof(pageMap));
		MemCopy(memory + PAWS_PAGE_MAP2, pageMap, sizeof(pageMap));
		MemCopy(memory + PAWS_DISPLAY_PAIRS, displayPairs, sizeof(displayPairs));
		memory[PAWS_BORDER] = 0;
	}

	uint32_t address = PAWS_FONTS;
	uint32_t limit = partBStart;

	uint8_t fontCount = 0;
	if (donor.memory != 0)
	{
		uint16_t donorFonts = PAWSRead16(donor.memory + PAWS_FONTS_PTR);
		uint16_t donorDict = PAWSRead16(donor.memory + PAWS_DICT_PTR);
		fontCount = (uint8_t)((donorDict - donorFonts) / 768);
		if (!PAWSEmit(ctx, memory, &address, limit, donor.memory + donorFonts, fontCount * 768))
			return false;
	}

	uint16_t dictAddress = (uint16_t)address;
	if (donor.dict != 0)
	{
		if (!PAWSEmit(ctx, memory, &address, limit, donor.dict, donor.dictSize))
			return false;
	}
	else if (!PAWSEmitByte(ctx, memory, &address, limit, 0xFF))
		return false;

	memory[PAWS_COUNTS + 0] = (uint8_t)model->objectCount;
	memory[PAWS_COUNTS + 1] = (uint8_t)locationCount;
	memory[PAWS_COUNTS + 2] = (uint8_t)model->messageCount;
	memory[PAWS_COUNTS + 3] = (uint8_t)model->systemMessageCount;
	memory[PAWS_COUNTS + 4] = (uint8_t)model->processCount;
	memory[PAWS_COUNTS + 5] = fontCount;
	PAWSWrite16(memory + PAWS_FONTS_PTR, PAWS_FONTS);
	PAWSWrite16(memory + PAWS_DICT_PTR, dictAddress);

	// Processes: each one stores its entries' bytecode lists, a shared empty
	// list (a lone 0xFF), the entry table, and a trailing zero byte plus a
	// pointer back to the empty list.
	uint16_t* processTables = Allocate<uint16_t>(ctx->arena, (unsigned)model->processCount, false);
	if (model->processCount != 0 && processTables == 0)
		return false;
	for (size_t p = 0; p < model->processCount; p++)
	{
		DC_Process* process = &model->processes[p];
		uint16_t* codeAddress = Allocate<uint16_t>(ctx->arena, (unsigned)process->entryCount, false);
		if (process->entryCount != 0 && codeAddress == 0)
			return false;
		for (size_t e = 0; e < process->entryCount; e++)
		{
			DC_ProcessEntry* entry = &process->entries[e];
			codeAddress[e] = (uint16_t)address;
			for (size_t n = 0; n < entry->instructionCount; n++)
			{
				DC_ProcessInstruction* inst = &entry->instructions[n];
				if (!PAWSEmitByte(ctx, memory, &address, limit, inst->opcode))
					return false;
				for (int k = 0; k < inst->parameterCount; k++)
				{
					int value = inst->parameters[k];
					if (inst->parameterLabels[k] != 0)
					{
						int targetEntry = FindProcessLabelEntry(process, inst->parameterLabels[k]);
						value = targetEntry < 0 ? 0 : targetEntry - (int)e - 1;
					}
					if (!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)value))
						return false;
				}
			}
			if (!PAWSEmitByte(ctx, memory, &address, limit, 0xFF))
				return false;
		}
		uint16_t emptyList = (uint16_t)address;
		if (!PAWSEmitByte(ctx, memory, &address, limit, 0xFF))
			return false;
		processTables[p] = (uint16_t)address;
		for (size_t e = 0; e < process->entryCount; e++)
		{
			if (!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)process->entries[e].verb) ||
				!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)process->entries[e].noun) ||
				!PAWSEmitWord(ctx, memory, &address, limit, codeAddress[e]))
				return false;
		}
		if (!PAWSEmitByte(ctx, memory, &address, limit, 0x00) ||
			!PAWSEmitByte(ctx, memory, &address, limit, 0x00) ||
			!PAWSEmitWord(ctx, memory, &address, limit, emptyList))
			return false;
	}
	uint16_t processDirectory = (uint16_t)address;
	for (size_t p = 0; p < model->processCount; p++)
	{
		if (!PAWSEmitWord(ctx, memory, &address, limit, processTables[p]))
			return false;
	}

	// Text families, each as packed bodies followed by a pointer directory.
	// Object names are the one family PAW never compresses.
	struct
	{
		DC_Text* table;
		size_t count;
		bool compress;
		uint16_t directory;
	}
	families[4] = {
		{ model->objectTexts, model->objectTextCount, false, 0 },
		{ model->locationTexts, model->locationTextCount, true, 0 },
		{ model->messages, model->messageCount, true, 0 },
		{ model->systemMessages, model->systemMessageCount, true, 0 },
	};
	for (int f = 0; f < 4; f++)
	{
		uint16_t* pointers = Allocate<uint16_t>(ctx->arena, (unsigned)families[f].count, false);
		if (families[f].count != 0 && pointers == 0)
			return false;
		for (size_t n = 0; n < families[f].count; n++)
		{
			pointers[n] = (uint16_t)address;
			if (!AppendPAWSMessage(ctx, memory, &address, limit, &families[f].table[n], &donor, families[f].compress))
				return false;
		}
		families[f].directory = (uint16_t)address;
		for (size_t n = 0; n < families[f].count; n++)
		{
			if (!PAWSEmitWord(ctx, memory, &address, limit, pointers[n]))
				return false;
		}
	}

	// Connections
	uint16_t* connectionPointers = Allocate<uint16_t>(ctx->arena, (unsigned)locationCount, false);
	if (locationCount != 0 && connectionPointers == 0)
		return false;
	for (size_t n = 0; n < locationCount; n++)
	{
		connectionPointers[n] = (uint16_t)address;
		for (size_t c = 0; c < model->connections[n].count; c++)
		{
			if (!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)model->connections[n].items[c].word) ||
				!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)model->connections[n].items[c].destination))
				return false;
		}
		if (!PAWSEmitByte(ctx, memory, &address, limit, 0xFF))
			return false;
	}
	uint16_t connectionDirectory = (uint16_t)address;
	for (size_t n = 0; n < locationCount; n++)
	{
		if (!PAWSEmitWord(ctx, memory, &address, limit, connectionPointers[n]))
			return false;
	}

	// Vocabulary: the '*' and '_' pseudo-words bracket the table, real words
	// are sorted by word value, and a zeroed record ends the table.
	uint16_t vocabularyAddress = (uint16_t)address;
	static const uint8_t starEncoded[5] = { '*', ' ', ' ', ' ', ' ' };
	static const uint8_t nullEncoded[5] = { '_', ' ', ' ', ' ', ' ' };
	if (!AppendPAWSVocabularyWord(ctx, memory, &address, limit, starEncoded, 1, 0xFF))
		return false;
	DC_VocabWord* sorted = Allocate<DC_VocabWord>(ctx->arena, (unsigned)model->vocabularyCount, false);
	if (model->vocabularyCount != 0 && sorted == 0)
		return false;
	if (model->vocabularyCount != 0)
		MemCopy(sorted, model->vocabulary, sizeof(DC_VocabWord) * model->vocabularyCount);
	SortPAWSVocabulary(sorted, model->vocabularyCount);
	for (size_t n = 0; n < model->vocabularyCount; n++)
	{
		if (!AppendPAWSVocabularyWord(ctx, memory, &address, limit, sorted[n].encoded, (uint8_t)sorted[n].index, sorted[n].type))
			return false;
	}
	if (!AppendPAWSVocabularyWord(ctx, memory, &address, limit, nullEncoded, 255, 0xFF))
		return false;
	for (int n = 0; n < 7; n++)
	{
		if (!PAWSEmitByte(ctx, memory, &address, limit, 0x00))
			return false;
	}

	// Object tables
	uint16_t objectLocations = (uint16_t)address;
	for (size_t n = 0; n < model->objectCount; n++)
	{
		if (!PAWSEmitByte(ctx, memory, &address, limit, (uint8_t)model->objects[n].location))
			return false;
	}
	if (!PAWSEmitByte(ctx, memory, &address, limit, 0xFF))
		return false;
	uint16_t objectWords = (uint16_t)address;
	for (size_t n = 0; n < model->objectCount; n++)
	{
		if (!PAWSEmitByte(ctx, memory, &address, limit, model->objects[n].noun < 0 ? 255 : (uint8_t)model->objects[n].noun) ||
			!PAWSEmitByte(ctx, memory, &address, limit, model->objects[n].adjective < 0 ? 255 : (uint8_t)model->objects[n].adjective))
			return false;
	}
	if (!PAWSEmitByte(ctx, memory, &address, limit, 0x00) ||
		!PAWSEmitByte(ctx, memory, &address, limit, 0x00))
		return false;
	uint16_t objectAttributes = (uint16_t)address;
	for (size_t n = 0; n < model->objectCount; n++)
	{
		uint8_t flags = (uint8_t)model->objects[n].weight;
		if (model->objects[n].container) flags |= Obj_Container;
		if (model->objects[n].wearable) flags |= Obj_Wearable;
		if (!PAWSEmitByte(ctx, memory, &address, limit, flags))
			return false;
	}
	uint16_t partAEnd = (uint16_t)address;

	// Fixed trailer
	PAWSWrite16(memory + PAWS_TRAILER + 0x00, processDirectory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x02, families[0].directory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x04, families[1].directory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x06, families[2].directory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x08, families[3].directory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x0A, connectionDirectory);
	PAWSWrite16(memory + PAWS_TRAILER + 0x0C, vocabularyAddress);
	PAWSWrite16(memory + PAWS_TRAILER + 0x0E, objectLocations);
	PAWSWrite16(memory + PAWS_TRAILER + 0x10, objectWords);
	PAWSWrite16(memory + PAWS_TRAILER + 0x12, objectAttributes);
	PAWSWrite16(memory + PAWS_PART_A_END, partAEnd);
	PAWSWrite16(memory + PAWS_PART_B_START, (uint16_t)partBStart);
	memory[PAWS_TRAILER_COUNTS + 0] = (uint8_t)model->messageCount;
	memory[PAWS_TRAILER_COUNTS + 1] = (uint8_t)model->systemMessageCount;
	memory[PAWS_TRAILER_COUNTS + 2] = (uint8_t)model->objectCount;
	memory[PAWS_TRAILER_COUNTS + 3] = (uint8_t)locationCount;
	memory[PAWS_TRAILER_COUNTS + 4] = (uint8_t)model->processCount;
	PAWSWrite16(memory + PAWS_BASE_WORD, PAWS_BASE);
	memory[PAWS_PAGE_BYTE] = 0;

	// SDB container with the two 48K segments
	uint16_t lengthA = (uint16_t)(partAEnd - PAWS_BASE);
	uint32_t lengthB = 0x10000 - partBStart;
	size_t fileSize = 24 + 2 * 16 + lengthA + lengthB;
	DC_BufferBuilder data = {};
	if (!ResizeBytes(ctx, &data, fileSize))
		return false;
	uint8_t* header = data.data;
	MemClear(header, 24);
	header[0] = 'S'; header[1] = 'D'; header[2] = 'B'; header[3] = 0x1A;
	header[4] = 1; header[5] = 0; header[6] = 48; header[7] = 0;
	PAWSWrite16(header + 8, 24);
	PAWSWrite16(header + 10, 16);
	PAWSWrite16(header + 12, 2);
	PAWSWrite16(header + 16, 24);
	PAWSWrite16(header + 18, 0);
	PAWSWrite16(header + 20, (uint16_t)(fileSize & 0xFFFF));
	PAWSWrite16(header + 22, (uint16_t)(fileSize >> 16));
	uint32_t payloadOffset = 24 + 2 * 16;
	for (int segment = 0; segment < 2; segment++)
	{
		uint8_t* entry = header + 24 + segment * 16;
		uint16_t load = segment == 0 ? PAWS_BASE : (uint16_t)partBStart;
		uint32_t length = segment == 0 ? lengthA : lengthB;
		MemClear(entry, 16);
		entry[0] = 0xFF;
		entry[1] = (uint8_t)(segment + 1);
		PAWSWrite16(entry + 2, load);
		PAWSWrite16(entry + 4, (uint16_t)length);
		PAWSWrite16(entry + 8, (uint16_t)(payloadOffset & 0xFFFF));
		PAWSWrite16(entry + 10, (uint16_t)(payloadOffset >> 16));
		MemCopy(data.data + payloadOffset, memory + load, length);
		uint32_t crc = PAWSCRC32(data.data + payloadOffset, length);
		PAWSWrite16(entry + 12, (uint16_t)(crc & 0xFFFF));
		PAWSWrite16(entry + 14, (uint16_t)(crc >> 16));
		payloadOffset += length;
	}
	ctx->compiledSize = data.size;
	*output = data;
	return true;
}

static bool SeedDefines(DC_Context* ctx)
{
	SetDefine(ctx, MakeString("TRUE"), MakeString("1"));
	SetDefine(ctx, MakeString("FALSE"), MakeString("0"));
	SetDefine(ctx, MakeString("HERE"), MakeString("255"));
	SetDefine(ctx, MakeString("CARRIED"), MakeString("254"));
	SetDefine(ctx, MakeString("LIMBO"), MakeString("254"));
	SetDefine(ctx, MakeString("NOTCREATED"), MakeString("252"));
	SetDefine(ctx, MakeString("DESTROYED"), MakeString("252"));
	switch (ctx->opts.target)
	{
		case DDB_MACHINE_IBMPC: SetDefine(ctx, MakeString("PC"), MakeString("1")); break;
		case DDB_MACHINE_SPECTRUM: SetDefine(ctx, MakeString("SPE"), MakeString("1")); break;
		case DDB_MACHINE_C64: SetDefine(ctx, MakeString("CBM64"), MakeString("1")); break;
		case DDB_MACHINE_CPC: SetDefine(ctx, MakeString("CPC"), MakeString("1")); break;
		case DDB_MACHINE_MSX: SetDefine(ctx, MakeString("MSX"), MakeString("1")); break;
		case DDB_MACHINE_ATARIST: SetDefine(ctx, MakeString("ST"), MakeString("1")); break;
		case DDB_MACHINE_AMIGA: SetDefine(ctx, MakeString("AMIGA"), MakeString("1")); break;
		case DDB_MACHINE_PCW: SetDefine(ctx, MakeString("PCW"), MakeString("1")); break;
		case DDB_MACHINE_PLUS4: SetDefine(ctx, MakeString("CBM64"), MakeString("1")); break;
		case DDB_MACHINE_MSX2: SetDefine(ctx, MakeString("MSX"), MakeString("1")); break;
		default: break;
	}
	if (ctx->opts.version == DDB_VERSION_PAWS) SetDefine(ctx, MakeString("PAWS"), MakeString("1"));
	if (ctx->opts.language == DDB_ENGLISH) SetDefine(ctx, MakeString("ENGLISH"), MakeString("1"));
	if (ctx->opts.language == DDB_SPANISH) SetDefine(ctx, MakeString("SPANISH"), MakeString("1"));
	if (ctx->opts.target == DDB_MACHINE_IBMPC || ctx->opts.target == DDB_MACHINE_ATARIST || ctx->opts.target == DDB_MACHINE_AMIGA || ctx->opts.target == DDB_MACHINE_PCW)
		SetDefine(ctx, MakeString("BIGMEM"), MakeString("1"));
	if (ctx->opts.target == DDB_MACHINE_SPECTRUM || ctx->opts.target == DDB_MACHINE_C64 || ctx->opts.target == DDB_MACHINE_CPC || ctx->opts.target == DDB_MACHINE_MSX || ctx->opts.target == DDB_MACHINE_MSX2)
		SetDefine(ctx, MakeString("DRAW"), MakeString("1"));
	int columnCount = ctx->opts.version == DDB_VERSION_PAWS ? 32 : GetColumnCount(ctx->opts.target);
	char buffer[32];
	LongToChar(columnCount, buffer, 10);
	SetDefine(ctx, MakeString("COLS"), MakeString(buffer));
	if (columnCount == 40) SetDefine(ctx, MakeString("C40"), MakeString("1"));
	if (columnCount == 42) SetDefine(ctx, MakeString("C42"), MakeString("1"));
	if (columnCount == 53) SetDefine(ctx, MakeString("C53"), MakeString("1"));
	for (size_t i = 0; i < ctx->opts.defineCount; i++)
	{
		DC_String entry = MakeString(ctx->opts.defines[i] ? ctx->opts.defines[i] : "");
		const uint8_t* equal = entry.ptr;
		while (equal < entry.end && *equal != '=')
			equal++;
		DC_String name(entry.ptr, equal);
		DC_String value = equal < entry.end ? DC_String(equal + 1, entry.end) : MakeString("1");
		SetDefine(ctx, name, value);
	}
	return true;
}

static bool CompileContext(DC_Context* ctx, DC_BufferBuilder* output)
{
	if (!SeedDefines(ctx))
		return false;
	DC_SourceLine* preprocessed = 0;
	size_t preprocessedCount = 0;
	size_t preprocessedCapacity = 0;
	if (!PreprocessFile(ctx, ctx->inputFile, &preprocessed, &preprocessedCount, &preprocessedCapacity))
		return false;
	if (ctx->opts.dumpPreprocessed)
	{
		for (size_t i = 0; i < preprocessedCount; i++)
			printf("%s\n", preprocessed[i].text);
	}
	DC_ProgramModel model = {};
	if (!ParseSource(ctx, preprocessed, preprocessedCount, &model))
		return false;
	if (DC_GetError() != DCError_None)
		return false;
	if (IsPAWSMode(ctx))
		return BuildPAWS(ctx, &model, output);
	return BuildDDB(ctx, &model, output);
}

void DC_SetError(DC_Error error, const char* message)
{
	if (error == DCError_None)
	{
		dcError = DCError_None;
		dcErrorText[0] = 0;
		dcErrorTextSize = 0;
		dcErrorCount = 0;
		return;
	}
	if (dcError == DCError_None)
		dcError = error;
	if (message != 0 && message[0] != 0)
		AppendErrorText(message);
	dcErrorCount++;
}

DC_Error DC_GetError()
{
	return dcError;
}

int DC_GetErrorCount()
{
	return dcErrorCount;
}

const char* DC_GetErrorString()
{
	switch (dcError)
	{
		case DCError_None: return "No error";
		case DCError_FileNotFound: return "File not found";
		case DCError_ReadError: return "I/O error reading file";
		case DCError_WriteError: return "I/O error writing file";
		case DCError_SyntaxError: return "Syntax error";
		case DCError_SemanticError: return "Semantic error";
		case DCError_Unsupported: return "Unsupported feature";
		case DCError_OutOfMemory: return "Out of memory";
		default: return "Unknown error";
	}
}

const char* DC_GetErrorDetails()
{
	return dcErrorText;
}

const DC_Compilation* DC_Compile(const char* fileName, const DC_CompilerOptions* options)
{
	DC_SetError(DCError_None, 0);
	if (fileName == 0 || options == 0)
	{
		DC_SetError(DCError_SyntaxError, "Missing compiler input");
		return 0;
	}
	if (options->version != DDB_VERSION_1 && options->version != DDB_VERSION_2 &&
		options->version != DDB_VERSION_PAWS)
	{
		DC_SetError(DCError_Unsupported, "Only DAAD v1, v2 and PAWS compilation is implemented in this version of adpc");
		return 0;
	}

	Arena* arena = AllocateArena("ADPC compiler");
	if (arena == 0)
	{
		DC_SetError(DCError_OutOfMemory, "Unable to allocate compiler arena");
		return 0;
	}
	DC_Context ctx = {};
	ctx.arena = arena;
	ctx.inputFile = fileName;
	ctx.opts = *options;
	ctx.currentNullWordChar = '_';
	DC_BufferBuilder bytes = {};
	bool ok = CompileContext(&ctx, &bytes);
	if (!ok)
	{
		FreeArena(arena);
		return 0;
	}
	DC_Compilation* compilation = Allocate<DC_Compilation>("ADPC compilation");
	if (compilation == 0)
	{
		FreeArena(arena);
		DC_SetError(DCError_OutOfMemory, "Unable to allocate compilation object");
		return 0;
	}
	compilation->size = bytes.size;
	compilation->data = Allocate<uint8_t>("ADPC compilation bytes", (unsigned)bytes.size, false);
	if (compilation->data == 0)
	{
		Free(compilation);
		FreeArena(arena);
		DC_SetError(DCError_OutOfMemory, "Unable to allocate output buffer");
		return 0;
	}
	if (bytes.size != 0)
		MemCopy(compilation->data, bytes.data, bytes.size);
	FreeArena(arena);
	return compilation;
}

void DC_FreeCompilation(const DC_Compilation* compilation)
{
	if (compilation == 0)
		return;
	Free((void*)compilation->data);
	Free((void*)compilation);
}
