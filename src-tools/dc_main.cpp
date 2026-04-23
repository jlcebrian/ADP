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

struct DC_Process
{
	int index;
	DC_ProcessEntry* entries;
	size_t entryCount;
	size_t entryCapacity;
};

struct DC_ObjectDef
{
	int location;
	int weight;
	bool container;
	bool wearable;
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
	return s.ptr < s.end && IsDigit(*s.ptr);
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

static bool AppendTextLine(DC_Context* ctx, DC_Text* text, DC_String line)
{
	if (StringEmpty(line))
		return true;
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
	return AppendTextBytes(ctx, text, lineBreak, 2);
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
		if (StringIEqualsText(name, ctx->defines[i].name))
			return &ctx->defines[i];
	}
	return 0;
}

static bool SetDefine(DC_Context* ctx, DC_String name, DC_String value)
{
	name = TrimString(name);
	value = TrimString(value);
	if (StringEmpty(name))
		return true;
	DC_Define* define = FindDefine(ctx, name);
	if (define == 0)
		define = AppendArray(ctx->arena, &ctx->defines, &ctx->defineCount, &ctx->defineCapacity);
	if (define == 0)
		return false;
	define->name = CopyUpperString(ctx->arena, name);
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

	if (FileExists(includeBuffer))
	{
		StrCopy(resolved, (uint32_t)resolvedSize, includeBuffer);
		return true;
	}
	if (JoinPath(candidate, sizeof(candidate), includingFile, DirNameSize(includingFile), includeBuffer) && FileExists(candidate))
	{
		StrCopy(resolved, (uint32_t)resolvedSize, candidate);
		return true;
	}
	if (JoinPath(candidate, sizeof(candidate), includingFile, DirNameSize(includingFile), base) && FileExists(candidate))
	{
		StrCopy(resolved, (uint32_t)resolvedSize, candidate);
		return true;
	}
	for (size_t i = 0; i < ctx->opts.includePathCount; i++)
	{
		const char* path = ctx->opts.includePaths[i];
		if (path != 0 && JoinPath(candidate, sizeof(candidate), path, StrLen(path), base) && FileExists(candidate))
		{
			StrCopy(resolved, (uint32_t)resolvedSize, candidate);
			return true;
		}
	}
	static const char* testPaths[] = { "tests/sce", "tests/devdisk", "tests/pcw" };
	for (size_t i = 0; i < sizeof(testPaths) / sizeof(testPaths[0]); i++)
	{
		if (JoinPath(candidate, sizeof(candidate), testPaths[i], StrLen(testPaths[i]), base) && FileExists(candidate))
		{
			StrCopy(resolved, (uint32_t)resolvedSize, candidate);
			return true;
		}
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
			return 0;
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
	for (;;)
	{
		if (ExprConsume(parser, "==")) value = value == ParseExprAdd(parser);
		else if (ExprConsume(parser, "!=")) value = value != ParseExprAdd(parser);
		else if (ExprConsume(parser, "<=")) value = value <= ParseExprAdd(parser);
		else if (ExprConsume(parser, ">=")) value = value >= ParseExprAdd(parser);
		else if (ExprConsume(parser, "<")) value = value < ParseExprAdd(parser);
		else if (ExprConsume(parser, ">")) value = value > ParseExprAdd(parser);
		else break;
	}
	return value ? 1 : 0;
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
			DC_Text expanded = {};
			if (!ExpandMacros(ctx, args, &expanded))
				return false;
			DC_ExprParser parser;
			parser.ctx = ctx;
			parser.text = MakeString(expanded.data != 0 ? expanded.data : "");
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
			int depth = frameCount;
			while (depth > 0 && line.ptr < line.end && *line.ptr == ' ')
			{
				line.ptr++;
				depth--;
			}
			DC_Text expanded = {};
			if (!ExpandMacros(ctx, line, &expanded))
			{
				ctx->includeStackCount--;
				return false;
			}
			DC_SourceLine* sourceLine = AppendArray(ctx->arena, lines, lineCount, lineCapacity);
			if (sourceLine == 0)
			{
				ctx->includeStackCount--;
				return false;
			}
			sourceLine->file = path;
			sourceLine->line = lineNumber;
			sourceLine->text = expanded.data != 0 ? expanded.data : CopyString(ctx->arena, MakeString(""));
		}

		if (ptr >= end)
			break;
	}

	ctx->includeStackCount--;
	if (frameCount != 0)
		SetFailureAt(path, lineNumber, DCError_SyntaxError, "Unterminated conditional");
	return true;
}

static uint8_t EncodeCodepointToDAAD(uint32_t codepoint)
{
	if (codepoint < 0x80)
		return (uint8_t)codepoint;
	uint8_t daadCode = '?';
	if (!DC_ConvertUnicodeToDAAD((int)codepoint, &daadCode))
		daadCode = '?';
	return daadCode;
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
		case 'b': if (allowMessageControls) { *value = 0x0B; *index += 2; return true; } break;
		case 'k': if (allowMessageControls) { *value = 0x0C; *index += 2; return true; } break;
		case 'n': if (allowMessageControls) { *value = 0x0D; *index += 2; return true; } break;
		case 'g': if (allowMessageControls) { *value = 0x0E; *index += 2; return true; } break;
		case 't': if (allowMessageControls) { *value = 0x0F; *index += 2; return true; } break;
		case 'f': if (allowMessageControls) { *value = 0x7F; *index += 2; return true; } break;
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
			codepoint = '?';
		if (!AppendByte(ctx, out, EncodeCodepointToDAAD(codepoint)))
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

static bool AppendMessage(DC_Context* ctx, DC_BufferBuilder* data, DC_String text)
{
	size_t size = (size_t)(text.end - text.ptr);
	for (size_t i = 0; i < size;)
	{
		uint8_t escaped = 0;
		if (TryParseEscape(text, &i, true, &escaped))
		{
			if (!AppendByte(ctx, data, escaped ^ 0xFF))
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
					if (!AppendByte(ctx, data, (uint8_t)ParseInt(DC_String(text.ptr + start, text.ptr + end)) ^ 0xFF))
						return false;
					i = end + 1;
					continue;
				}
			}
		}
		uint32_t codepoint = 0;
		if (!DecodeUTF8Codepoint(text, &i, &codepoint))
			codepoint = '?';
		uint8_t legacyControl = 0;
		if (MapLegacySourceControl(codepoint, &legacyControl))
		{
			if (!AppendByte(ctx, data, legacyControl ^ 0xFF))
				return false;
			continue;
		}
		if (!AppendByte(ctx, data, EncodeCodepointToDAAD(codepoint) ^ 0xFF))
			return false;
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
		if (StringIEqualsText(name, ctx->processIds[i].name))
			return &ctx->processIds[i];
	}
	return 0;
}

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
		DC_ProcessID* id = FindProcessID(ctx, TrimString(DC_String(text.ptr, plus)));
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
	id = AppendArray(ctx->arena, &ctx->processIds, &ctx->processIdCount, &ctx->processIdCapacity);
	if (id == 0)
		return -1;
	id->name = CopyUpperString(ctx->arena, text);
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
		DC_ExprParser parser;
		parser.ctx = ctx;
		parser.text = MakeString(define->value);
		return ParseExprOr(&parser);
	}
	DC_ProcessID* proc = FindProcessID(ctx, text);
	return proc != 0 ? proc->value : 0;
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
	return ParseInt(text);
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
	connection->destination = ParseInt(fields[1]);
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
		SetFailure(ctx, DCError_SyntaxError, "Invalid object line");
		return false;
	}
	if (!EnsureArray(ctx->arena, &model->objects, &model->objectCount, &model->objectCapacity, (size_t)objectIndex + 1))
		return false;
	DC_ObjectDef* object = &model->objects[objectIndex];
	object->location = ParseObjectLocation(ctx, fields[0]);
	object->weight = ParseInt(fields[1]) & Obj_Weight;
	object->container = !IsNullWord(ctx, fields[2]);
	object->wearable = !IsNullWord(ctx, fields[3]);
	object->noun = ResolveWordReference(ctx, model, fields[4], false, WordType_Unknown);
	object->adjective = ResolveWordReference(ctx, model, fields[5], false, WordType_Adjective);
	return object->noun >= 0 && object->adjective >= -1;
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
			instruction->indirect = true;
			value.ptr++;
			value.end--;
		}
		instruction->parameters[i] = LookupNumeric(ctx, value);
	}
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
		localEntry.verb = IsNullWord(ctx, fields[0]) ? 255 : LookupWordNumber(ctx, fields[0], WordType_Verb);
		localEntry.noun = IsNullWord(ctx, fields[1]) ? 255 : LookupWordNumber(ctx, fields[1], WordType_Noun);
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
		if (StringEmpty(trimmed))
		{
			if (currentTextTable != 0 && currentIndex >= 0 && (size_t)currentIndex < *currentTextCount)
				AppendTextBreak(ctx, &currentTextTable[currentIndex]);
			continue;
		}
		if (*trimmed.ptr == '/')
		{
			DC_String header = TrimString(StripComment(DC_String(trimmed.ptr + 1, trimmed.end)));
			DC_String inlineRemainder = {};
			int inlineIndex = -1;
			if (StringIEqualsText(header, "CTL")) { section = Section_CTL; currentTextTable = 0; currentProcess = 0; continue; }
			if (StringIEqualsText(header, "TOK")) { section = Section_TOK; currentTextTable = 0; currentProcess = 0; continue; }
			if (StringIEqualsText(header, "VOC")) { section = Section_VOC; currentTextTable = 0; currentProcess = 0; continue; }
			if (StringIEqualsText(header, "STX")) { section = Section_STX; currentTextTable = model->systemMessages; currentTextCount = &model->systemMessageCount; currentTextCapacity = &model->systemMessageCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringIEqualsText(header, "MTX")) { section = Section_MTX; currentTextTable = model->messages; currentTextCount = &model->messageCount; currentTextCapacity = &model->messageCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringIEqualsText(header, "OTX")) { section = Section_OTX; currentTextTable = model->objectTexts; currentTextCount = &model->objectTextCount; currentTextCapacity = &model->objectTextCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringIEqualsText(header, "LTX")) { section = Section_LTX; currentTextTable = model->locationTexts; currentTextCount = &model->locationTextCount; currentTextCapacity = &model->locationTextCapacity; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringIEqualsText(header, "CON")) { section = Section_CON; currentTextTable = 0; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringIEqualsText(header, "OBJ")) { section = Section_OBJ; currentTextTable = 0; currentProcess = 0; currentEntry = 0; currentIndex = -1; continue; }
			if (StringStartsWithTextI(header, "PRO"))
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
				ParseIndexedSectionHeader(header, &inlineIndex, &inlineRemainder))
			{
				currentIndex = inlineIndex;
				if (section == Section_CON)
				{
					if (!EnsureArray(ctx->arena, &model->connections, &model->connectionCount, &model->connectionCapacity, (size_t)currentIndex + 1))
						return false;
					if (!StringEmpty(inlineRemainder) && !ParseConnectionLine(ctx, model, inlineRemainder, currentIndex))
						return false;
					continue;
				}
				if (!EnsureArray(ctx->arena, &currentTextTable, currentTextCount, currentTextCapacity, (size_t)currentIndex + 1))
					return false;
				if (section == Section_STX) model->systemMessages = currentTextTable;
				if (section == Section_MTX) model->messages = currentTextTable;
				if (section == Section_OTX) model->objectTexts = currentTextTable;
				if (section == Section_LTX) model->locationTexts = currentTextTable;
				if (!StringEmpty(inlineRemainder))
					AppendTextLine(ctx, &currentTextTable[currentIndex], inlineRemainder);
				continue;
			}
			if (section == Section_OBJ && ParseIndexedSectionHeader(header, &inlineIndex, &inlineRemainder))
			{
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
				AppendTextLine(ctx, &currentTextTable[currentIndex], rawLine);
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

static uint16_t AppendMessageTable(DC_Context* ctx, DC_BufferBuilder* data, DC_Text* table, size_t count, bool littleEndian)
{
	uint16_t tableOffset = (uint16_t)data->size;
	ResizeBytes(ctx, data, data->size + count * 2);
	for (size_t i = 0; i < count; i++)
	{
		uint16_t messageOffset = (uint16_t)data->size;
		if (table[i].data != 0)
			AppendMessage(ctx, data, MakeString(table[i].data));
		else
			AppendByte(ctx, data, 0x0A ^ 0xFF);
		Write16(data->data, tableOffset + i * 2, messageOffset, littleEndian);
	}
	return tableOffset;
}

static uint16_t AppendConnections(DC_Context* ctx, DC_BufferBuilder* data, DC_ConnectionList* connections, size_t count, bool littleEndian)
{
	uint16_t tableOffset = (uint16_t)data->size;
	ResizeBytes(ctx, data, data->size + count * 2);
	for (size_t i = 0; i < count; i++)
	{
		uint16_t entryOffset = (uint16_t)data->size;
		for (size_t n = 0; n < connections[i].count; n++)
		{
			AppendByte(ctx, data, (uint8_t)connections[i].items[n].word);
			AppendByte(ctx, data, (uint8_t)connections[i].items[n].destination);
		}
		AppendByte(ctx, data, 0xFF);
		Write16(data->data, tableOffset + i * 2, entryOffset, littleEndian);
	}
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

static uint16_t AppendProcesses(DC_Context* ctx, DC_BufferBuilder* data, DC_Process* processes, size_t count, bool littleEndian)
{
	uint16_t processTableOffset = (uint16_t)data->size;
	ResizeBytes(ctx, data, data->size + count * 2);
	for (size_t i = 0; i < count; i++)
	{
		uint16_t entriesOffset = (uint16_t)data->size;
		Write16(data->data, processTableOffset + i * 2, entriesOffset, littleEndian);
		size_t* patches = Allocate<size_t>(ctx->arena, (unsigned)processes[i].entryCount, false);
		for (size_t e = 0; e < processes[i].entryCount; e++)
		{
			AppendByte(ctx, data, (uint8_t)processes[i].entries[e].verb);
			AppendByte(ctx, data, (uint8_t)processes[i].entries[e].noun);
			patches[e] = data->size;
			AppendByte(ctx, data, 0);
			AppendByte(ctx, data, 0);
		}
		AppendByte(ctx, data, 0);
		AppendByte(ctx, data, 0);
		AppendByte(ctx, data, 0);
		AppendByte(ctx, data, 0);
		for (size_t e = 0; e < processes[i].entryCount; e++)
		{
			uint16_t codeOffset = (uint16_t)data->size;
			Write16(data->data, patches[e], codeOffset, littleEndian);
			DC_ProcessEntry* entry = &processes[i].entries[e];
			for (size_t n = 0; n < entry->instructionCount; n++)
			{
				DC_ProcessInstruction* inst = &entry->instructions[n];
				AppendByte(ctx, data, (uint8_t)(inst->opcode | (inst->indirect ? 0x80 : 0)));
				for (int p = 0; p < inst->parameterCount; p++)
					AppendByte(ctx, data, (uint8_t)inst->parameters[p]);
			}
			AppendByte(ctx, data, 0xFF);
		}
	}
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
	uint16_t conTableOffset, uint16_t vocabularyOffset, uint16_t objLocOffset, uint16_t objWordsOffset, uint16_t objAttrOffset, uint16_t tokensOffset)
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
	ResizeBytes(ctx, &data, 34);

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
		AppendByte(ctx, &data, 0xFF);
		for (size_t i = 0; i < model->tokenCount; i++)
			AppendToken(ctx, &data, MakeString(model->tokens[i].data != 0 ? model->tokens[i].data : ""));
		AppendByte(ctx, &data, 0);
	}

	DDB_Machine outputTarget = ctx->opts.target;
	bool littleEndian = IsLittleEndianTarget(outputTarget);
	uint16_t sysTableOffset = AppendMessageTable(ctx, &data, model->systemMessages, model->systemMessageCount, littleEndian);
	uint16_t msgTableOffset = AppendMessageTable(ctx, &data, model->messages, model->messageCount, littleEndian);
	uint16_t objTableOffset = AppendMessageTable(ctx, &data, model->objectTexts, model->objectTextCount, littleEndian);
	uint16_t locTableOffset = AppendMessageTable(ctx, &data, model->locationTexts, model->locationTextCount, littleEndian);
	uint16_t conTableOffset = AppendConnections(ctx, &data, model->connections, model->connectionCount, littleEndian);
	uint16_t objLocOffset = AppendObjectLocations(ctx, &data, model->objects, model->objectCount);
	uint16_t objWordsOffset = AppendObjectWords(ctx, &data, model->objects, model->objectCount);
	uint16_t objAttrOffset = AppendObjectAttributes(ctx, &data, model->objects, model->objectCount);
	uint16_t processTableOffset = AppendProcesses(ctx, &data, model->processes, model->processCount, littleEndian);
	if (data.size > MAX_DDB_SIZE)
	{
		SetFailure(ctx, DCError_WriteError, "Compiled DDB exceeds 64K");
		return false;
	}
	ctx->compiledSize = data.size;
	WriteHeader(ctx, data.data, outputTarget, littleEndian, GetBaseOffset(outputTarget), model, processTableOffset, objTableOffset, locTableOffset,
		msgTableOffset, sysTableOffset, conTableOffset, vocabularyOffset, objLocOffset, objWordsOffset, objAttrOffset, tokensOffset);
	*output = data;
	return true;
}

static bool SeedDefines(DC_Context* ctx)
{
	SetDefine(ctx, MakeString("TRUE"), MakeString("1"));
	SetDefine(ctx, MakeString("FALSE"), MakeString("0"));
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
	if (ctx->opts.language == DDB_ENGLISH) SetDefine(ctx, MakeString("ENGLISH"), MakeString("1"));
	if (ctx->opts.language == DDB_SPANISH) SetDefine(ctx, MakeString("SPANISH"), MakeString("1"));
	if (ctx->opts.target == DDB_MACHINE_SPECTRUM || ctx->opts.target == DDB_MACHINE_C64 || ctx->opts.target == DDB_MACHINE_CPC || ctx->opts.target == DDB_MACHINE_MSX || ctx->opts.target == DDB_MACHINE_MSX2)
		SetDefine(ctx, MakeString("DRAW"), MakeString("1"));
	char buffer[32];
	LongToChar(GetColumnCount(ctx->opts.target), buffer, 10);
	SetDefine(ctx, MakeString("COLS"), MakeString(buffer));
	if (GetColumnCount(ctx->opts.target) == 40) SetDefine(ctx, MakeString("C40"), MakeString("1"));
	if (GetColumnCount(ctx->opts.target) == 42) SetDefine(ctx, MakeString("C42"), MakeString("1"));
	if (GetColumnCount(ctx->opts.target) == 53) SetDefine(ctx, MakeString("C53"), MakeString("1"));
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
	if (options->version != DDB_VERSION_1)
	{
		DC_SetError(DCError_Unsupported, "Only DAAD v1 compilation is implemented in this version of adpc");
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
