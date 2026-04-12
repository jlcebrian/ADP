#include <bas_zx.h>

#include <stdio.h>

static uint16_t ReadUInt16BE(const uint8_t* data)
{
	return (uint16_t)((data[0] << 8) | data[1]);
}

static uint16_t ReadUInt16LE(const uint8_t* data)
{
	return (uint16_t)(data[0] | (data[1] << 8));
}

static const char* const ZXTokens[91] =
{
	"RND", "INKEY$", "PI", "FN", "POINT", "SCREEN$", "ATTR", "AT",
	"TAB", "VAL$", "CODE", "VAL", "LEN", "SIN", "COS", "TAN",
	"ASN", "ACS", "ATN", "LN", "EXP", "INT", "SQR", "SGN",
	"ABS", "PEEK", "IN", "USR", "STR$", "CHR$", "NOT", "BIN",
	"OR", "AND", "<=", ">=", "<>", "LINE", "THEN", "TO",
	"STEP", "DEF FN", "CAT", "FORMAT", "MOVE", "ERASE", "OPEN #", "CLOSE #",
	"MERGE", "VERIFY", "BEEP", "CIRCLE", "INK", "PAPER", "FLASH", "BRIGHT",
	"INVERSE", "OVER", "OUT", "LPRINT", "LLIST", "STOP", "READ", "DATA",
	"RESTORE", "NEW", "BORDER", "CONTINUE", "DIM", "REM", "FOR", "GO TO",
	"GO SUB", "INPUT", "LOAD", "LIST", "LET", "PAUSE", "NEXT", "POKE",
	"PRINT", "PLOT", "RUN", "SAVE", "RANDOMIZE", "IF", "CLS", "DRAW",
	"CLEAR", "RETURN", "COPY"
};

static bool TokenNeedsTrailingSpace(uint8_t token, uint8_t next)
{
	if (next == 0x0D || next == ':' || next == ';' || next == ',' || next == ')' || next == '(')
		return false;

	switch (token)
	{
		case 0xA5: // RND
		case 0xA6: // INKEY$
		case 0xA7: // PI
		case 0xAC: // AT
		case 0xAD: // TAB
		case 0xB2: // SIN
		case 0xB3: // COS
		case 0xB4: // TAN
		case 0xB5: // ASN
		case 0xB6: // ACS
		case 0xB7: // ATN
		case 0xB8: // LN
		case 0xB9: // EXP
		case 0xBA: // INT
		case 0xBB: // SQR
		case 0xBD: // ABS
		case 0xBE: // PEEK
		case 0xBF: // IN
		case 0xC0: // USR
		case 0xC1: // STR$
		case 0xC2: // CHR$
		case 0xC4: // BIN
		case 0xC7: // <=
		case 0xC8: // >=
		case 0xC9: // <>
			return next != ' ';
		default:
			return next != ' ';
	}
}

static bool TokenNeedsLeadingSpace(uint8_t token, uint8_t previous)
{
	if (previous == ' ')
		return false;
	if (previous == ':' || previous == ';' || previous == ',' || previous == '(')
		return false;

	switch (token)
	{
		case 0xC3: // NOT
		case 0xC5: // OR
		case 0xC6: // AND
		case 0xC7: // <=
		case 0xC8: // >=
		case 0xC9: // <>
			return previous == '"' || (previous >= '0' && previous <= '9');
		default:
			return previous == '"';
	}
}

static void WriteChar(FILE* output, uint8_t ch)
{
	if (ch >= 32 && ch <= 126)
	{
		fputc(ch, output);
		return;
	}

	switch (ch)
	{
		case 0x0D:
			break;
		case 0x5E:
			fputc('^', output);
			break;
		case 0x60:
			fputc('`', output);
			break;
		case 0x7F:
			fputs("(C)", output);
			break;
		default:
			fprintf(output, "{%02X}", ch);
			break;
	}
}

static bool DecodeLine(FILE* output, const uint8_t* data, uint16_t size)
{
	bool inString = false;
	bool inRemark = false;
	bool inData = false;

	for (uint16_t offset = 0; offset < size; offset++)
	{
		uint8_t ch = data[offset];
		if (ch == 0x0D)
			return offset + 1 == size;

		if (inRemark)
		{
			WriteChar(output, ch);
			continue;
		}

		if (inString)
		{
			WriteChar(output, ch);
			if (ch == '"')
				inString = false;
			continue;
		}

		if (inData)
		{
			WriteChar(output, ch);
			if (ch == '"')
				inString = true;
			else if (ch == ':')
				inData = false;
			continue;
		}

		if (ch == '"')
		{
			fputc('"', output);
			inString = true;
			continue;
		}

		if (ch == 0x0E)
		{
			if (offset + 5 >= size)
				return false;
			offset += 5;
			continue;
		}

		if (ch >= 0xA5)
		{
			const char* token = ZXTokens[ch - 0xA5];
			if (offset > 0 && TokenNeedsLeadingSpace(ch, data[offset - 1]))
				fputc(' ', output);
			fputs(token, output);
			if (ch == 0xEA)
				inRemark = true;
			else if (ch == 0xE4)
				inData = true;
			else if (offset + 1 < size && TokenNeedsTrailingSpace(ch, data[offset + 1]))
				fputc(' ', output);
			continue;
		}

		WriteChar(output, ch);
	}

	return false;
}

bool BASZX_DecodeToFile(const uint8_t* data, uint32_t dataSize, FILE* output)
{
	if (data == NULL || output == NULL)
		return false;

	uint32_t offset = 0;
	while (offset + 4 <= dataSize)
	{
		uint16_t lineNumber = ReadUInt16BE(data + offset);
		uint16_t lineSize = ReadUInt16LE(data + offset + 2);
		offset += 4;

		if (lineSize == 0 || offset + lineSize > dataSize)
			return false;
		if (data[offset + lineSize - 1] != 0x0D)
			return false;

		fprintf(output, "%u ", lineNumber);
		if (!DecodeLine(output, data + offset, lineSize))
			return false;
		fputc('\n', output);
		offset += lineSize;
	}

	return offset == dataSize;
}
