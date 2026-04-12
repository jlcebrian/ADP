#include <bas_cpc.h>

#include <math.h>
#include <stdio.h>

static uint16_t ReadUInt16(const uint8_t* data)
{
	return (uint16_t)(data[0] | (data[1] << 8));
}

static void WriteDecimal(FILE* output, int value)
{
	fprintf(output, "%d", value);
}

static void WriteUnsignedDecimal(FILE* output, unsigned int value)
{
	fprintf(output, "%u", value);
}

static void WriteHex16(FILE* output, uint16_t value)
{
	if (value == 0)
	{
		fputc('0', output);
		return;
	}

	bool started = false;
	for (int shift = 12; shift >= 0; shift -= 4)
	{
		uint8_t digit = (uint8_t)((value >> shift) & 0x0F);
		if (!started && digit == 0)
			continue;
		started = true;
		fputc(digit < 10 ? '0' + digit : 'A' + (digit - 10), output);
	}
}

static void WriteBinary16(FILE* output, uint16_t value)
{
	if (value == 0)
	{
		fputc('0', output);
		return;
	}

	bool started = false;
	for (int shift = 15; shift >= 0; shift--)
	{
		uint8_t digit = (uint8_t)((value >> shift) & 0x01);
		if (!started && digit == 0)
			continue;
		started = true;
		fputc('0' + digit, output);
	}
}

static void WriteFloat(FILE* output, const uint8_t* data)
{
	uint8_t exponentByte = data[4];
	if (exponentByte == 0)
	{
		fputc('0', output);
		return;
	}

	uint32_t mantissa =
		((uint32_t)(data[3] & 0x7F) << 24) |
		((uint32_t)data[2] << 16) |
		((uint32_t)data[1] << 8) |
		data[0];
	mantissa |= 0x80000000U;

	double value = (double)mantissa / 4294967296.0;
	value = ldexp(value, (int)exponentByte - 128);
	if ((data[3] & 0x80) != 0)
		value = -value;

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.9g", value);
	fputs(buffer, output);
}

static bool WriteVariable(FILE* output, const uint8_t* data, uint32_t dataSize, uint32_t* offset, uint8_t token)
{
	if (*offset + 2 > dataSize)
		return false;

	*offset += 2;
	bool terminated = false;
	while (*offset < dataSize)
	{
		uint8_t ch = data[*offset];
		fputc(ch & 0x7F, output);
		(*offset)++;
		if ((ch & 0x80) != 0)
		{
			terminated = true;
			break;
		}
	}
	if (!terminated)
		return false;

	switch (token)
	{
		case 0x02:
		case 0x05:
		case 0x08:
			fputc('%', output);
			break;
		case 0x03:
		case 0x06:
		case 0x09:
			fputc('$', output);
			break;
		case 0x04:
		case 0x07:
		case 0x0A:
			fputc('!', output);
			break;
	}

	return true;
}

static bool WriteQuotedString(FILE* output, const uint8_t* data, uint32_t dataSize, uint32_t* offset)
{
	fputc('"', output);
	while (*offset < dataSize)
	{
		uint8_t ch = data[*offset];
		(*offset)++;
		fputc(ch, output);
		if (ch == '"')
			return true;
	}

	// Some CPC BASIC file commands store the trailing quote implicitly
	// at end-of-line, so close the string when the token stream ends.
	fputc('"', output);
	return true;
}

static bool WriteRSX(FILE* output, const uint8_t* data, uint32_t dataSize, uint32_t* offset)
{
	if (*offset >= dataSize)
		return false;

	(*offset)++;
	fputc('|', output);
	while (*offset < dataSize)
	{
		uint8_t ch = data[*offset];
		fputc(ch & 0x7F, output);
		(*offset)++;
		if ((ch & 0x80) != 0)
			return true;
	}
	return false;
}

static const char* const BasicTokens[128] =
{
	"AFTER", "AUTO", "BORDER", "CALL", "CAT", "CHAIN", "CLEAR", "CLG",
	"CLOSEIN", "CLOSEOUT", "CLS", "CONT", "DATA", "DEF", "DEFINT", "DEFREAL",
	"DEFSTR", "DEG", "DELETE", "DIM", "DRAW", "DRAWR", "EDIT", "ELSE",
	"END", "ENT", "ENV", "ERASE", "ERROR", "EVERY", "FOR", "GOSUB",
	"GOTO", "IF", "INK", "INPUT", "KEY", "LET", "LINE", "LIST",
	"LOAD", "LOCATE", "MEMORY", "MERGE", "MID$", "MODE", "MOVE", "MOVER",
	"NEXT", "NEW", "ON", "ON BREAK", "ON ERROR GOTO", "ON SQ", "OPENIN", "OPENOUT",
	"ORIGIN", "OUT", "PAPER", "PEN", "PLOT", "PLOTR", "POKE", "PRINT",
	"'", "RAD", "RANDOMIZE", "READ", "RELEASE", "REM", "RENUM", "RESTORE",
	"RESUME", "RETURN", "RUN", "SAVE", "SOUND", "SPEED", "STOP", "SYMBOL",
	"TAG", "TAGOFF", "TROFF", "TRON", "WAIT", "WEND", "WHILE", "WIDTH",
	"WINDOW", "WRITE", "ZONE", "DI", "EI", "FILL", "GRAPHICS", "MASK",
	"FRAME", "CURSOR", 0, "ERL", "FN", "SPC", "STEP", "SWAP",
	0, 0, "TAB", "THEN", "TO", "USING", ">", "=",
	">=", "<", "<>", "<=", "+", "-", "*", "/",
	"^", "\\", "AND", "MOD", "OR", "XOR", "NOT", 0
};

static const char* const PrefixedTokens[128] =
{
	"ABS", "ASC", "ATN", "CHR$", "CINT", "COS", "CREAL", "EXP",
	"FIX", "FRE", "INKEY", "INP", "INT", "JOY", "LEN", "LOG",
	"LOG10", "LOWER$", "PEEK", "REMAIN", "SGN", "SIN", "SPACE$", "SQ",
	"SQR", "STR$", "TAN", "UNT", "UPPER$", "VAL", 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	"EOF", "ERR", "HIMEM", "INKEY$", "PI", "RND", "TIME", "XPOS",
	"YPOS", "DERR", 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, "BIN$", "DEC$", "HEX$", "INSTR", "LEFT$", "MAX", "MIN",
	"POS", "RIGHT$", "ROUND", "STRING$", "TEST", "TESTR", "COPYCHR$", "VPOS",
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static bool DecodeLineBody(FILE* output, const uint8_t* data, uint32_t dataSize)
{
	for (uint32_t offset = 0; offset < dataSize;)
	{
		uint8_t token = data[offset++];

		if (token <= 0x0D)
		{
			if (token == 0x00)
				return true;
			if (token == 0x01)
			{
				fputc(':', output);
				continue;
			}
			if (!WriteVariable(output, data, dataSize, &offset, token))
				return false;
			continue;
		}

		if (token <= 0x18)
		{
			WriteUnsignedDecimal(output, token - 0x0E);
			continue;
		}

		switch (token)
		{
			case 0x19:
				if (offset >= dataSize)
					return false;
				WriteUnsignedDecimal(output, data[offset++]);
				break;
			case 0x1A:
				if (offset + 2 > dataSize)
					return false;
				WriteDecimal(output, (int16_t)ReadUInt16(data + offset));
				offset += 2;
				break;
			case 0x1B:
				if (offset + 2 > dataSize)
					return false;
				fputs("&X", output);
				WriteBinary16(output, ReadUInt16(data + offset));
				offset += 2;
				break;
			case 0x1C:
				if (offset + 2 > dataSize)
					return false;
				fputc('&', output);
				WriteHex16(output, ReadUInt16(data + offset));
				offset += 2;
				break;
			case 0x1D:
			case 0x1E:
				if (offset + 2 > dataSize)
					return false;
				WriteUnsignedDecimal(output, ReadUInt16(data + offset));
				offset += 2;
				break;
			case 0x1F:
				if (offset + 5 > dataSize)
					return false;
				WriteFloat(output, data + offset);
				offset += 5;
				break;
			case 0x22:
				if (!WriteQuotedString(output, data, dataSize, &offset))
					return false;
				break;
			case 0x7C:
				if (!WriteRSX(output, data, dataSize, &offset))
					return false;
				break;
			case 0xC0:
			case 0xC5:
			{
				fputs(token == 0xC0 ? "'" : "REM", output);
				while (offset < dataSize && data[offset] != 0x01)
					fputc(data[offset++], output);
				break;
			}
			case 0xFF:
				if (offset >= dataSize)
					return false;
				if (PrefixedTokens[data[offset]] != 0)
				{
					fputs(PrefixedTokens[data[offset]], output);
					offset++;
				}
				else
				{
					fprintf(output, "{FF%02X}", data[offset++]);
				}
				break;
			default:
				if (token >= 0x20 && token <= 0x7B)
					fputc(token, output);
				else if (token >= 0x80 && BasicTokens[token - 0x80] != 0)
					fputs(BasicTokens[token - 0x80], output);
				else
					fprintf(output, "{%02X}", token);
				break;
		}
	}
	return true;
}

bool BASCPC_DecodeToFile(const uint8_t* data, uint32_t dataSize, FILE* output)
{
	if (data == NULL || output == NULL)
		return false;

	uint32_t offset = 0;
	while (offset + 2 <= dataSize)
	{
		uint16_t lineSize = ReadUInt16(data + offset);
		if (lineSize == 0)
			return true;
		if (lineSize < 5 || offset + lineSize > dataSize)
			return false;

		uint16_t lineNumber = ReadUInt16(data + offset + 2);
		fprintf(output, "%u ", lineNumber);
		if (!DecodeLineBody(output, data + offset + 4, lineSize - 5))
			return false;
		fputc('\n', output);
		offset += lineSize;
	}

	return offset == dataSize;
}
