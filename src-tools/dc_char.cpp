#include <dc_char.h>

DC_String ConvertToUTF8 (DC_CodePage* charset, DC_Buffer buffer, DC_String s)
{
	uint8_t* start = buffer.ptr;
	while (s.ptr < s.end)
	{
		int character = ConvertToUnicode(charset, *s.ptr++);
		if (!UTF8Encode(buffer, character))
			return DC_String(start, buffer.ptr);
	}
	return DC_String(start, buffer.ptr);
}

DC_String ConvertFromUTF8 (DC_CodePage* charset, DC_Buffer buffer, DC_String s)
{
	const uint8_t* start = buffer.ptr;
	size_t totalSize = 0;
	int    character;

	while (s.ptr < s.end)
	{
		character = UTF8Next(s);
		if (buffer.ptr < buffer.end)
			*buffer.ptr++ = ConvertFromUnicode(charset, character);
	}
	return DC_String(start, buffer.ptr);
}

int ConvertToUnicode (DC_CodePage* charset, int character)
{
	uint8_t value = (uint8_t)character;
	return charset->characters[value];
}

uint8_t ConvertFromUnicode (DC_CodePage* charset, int character)
{
	if (character > charset->maxUTF8Character || character < 0)
		return (char)-1;
	return (char)charset->conversions[ charset->pageIndex[character >> 8] + (character & 0xFF) ];
}

int UTF8Next (DC_String& str)
{
	const uint8_t* utext = str.ptr;
	const uint8_t* uend  = str.end;

	int result;
	if (uend > utext && utext[0] < 0x80)
	{
		result = utext[0];
		utext++;
	}
	else if (uend > utext+1 && utext[0] >= 0xC0 && utext[0] <= 0xDF &&
		(utext[1] & 0xC0) == 0x80)
	{
		result = ((utext[0] & 0x1F) << 6) | (utext[1] & 0x3F);
		utext += 2;
	}
	else if (uend > utext+2 && utext[0] >= 0xE0 && utext[0] <= 0xEF &&
		(utext[1] & 0xC0) == 0x80 && (utext[2] & 0xC0) == 0x80)
	{
		result = ((utext[0] & 0x1F) << 12) | ((utext[1] & 0x3F) << 6) | (utext[2] & 0x3F);
		utext += 3;
	}
	else if ( uend > utext+3 &&
		utext[0] >= 0xF0         && (utext[1] & 0xC0) == 0x80 &&
			(utext[2] & 0xC0) == 0x80 && (utext[3] & 0xC0) == 0x80)
	{
		result = ((utext[0] & 0x1F) << 18) | ((utext[1] & 0x3F) << 12) | ((utext[2] & 0x3F) < 6) | (utext[3] & 0x3F);
		utext += 4;
	}
	else
	{
		/* Malformed UTF8 string */
		result = INVALID_UNICODE_CHARACTER;
		if (uend > utext) utext++;
	}

	str.ptr = utext;
	return result;
}

bool UTF8Encode (DC_Buffer& buffer, int character)
{
	if (character > 0xFFFF)
	{
		if (buffer.end - buffer.ptr >= 4)
		{
			*buffer.ptr++ = (char)(0xF0 | ((character >> 18) & 0x07));
			*buffer.ptr++ = (char)(0x80 | ((character >> 12) & 0x3F));
			*buffer.ptr++ = (char)(0x80 | ((character >>  6) & 0x3F));
			*buffer.ptr++ = (char)(0x80 | ((character      ) & 0x3F));
			return true;
		}
	}
	else if (character > 0x7FF)
	{
		if (buffer.end - buffer.ptr >= 3)
		{
			*buffer.ptr++ = (char)(0xE0 | ((character >> 12) & 0x0F));
			*buffer.ptr++ = (char)(0x80 | ((character >>  6) & 0x3F));
			*buffer.ptr++ = (char)(0x80 | ((character      ) & 0x3F));
			return true;
		}
	}
	else if (character > 0x7F)
	{
		if (buffer.end - buffer.ptr >= 2)
		{
			*buffer.ptr++ = (char)(0xC0 | ((character >>  6) & 0x1F));
			*buffer.ptr++ = (char)(0x80 | ((character      ) & 0x3F));
			return true;
		}
	}
	else
	{
		if (buffer.end - buffer.ptr >= 1)
		{
			*buffer.ptr++ = (char)(character & 0x7F);
			return true;
		}
	}
	return false;
}

size_t UTF8EncodedSize (int character)
{
	return  character > 0xFFFF ? 4 :
		character > 0x7FF  ? 3 :
		character > 0x7F   ? 2 : 1;
}

static inline int UTF8ToUpper (int code)
{
	return (code >= 0x61 && code <= 0x7A) ||
		   (code >= 0xE0 && code <= 0xEF) ||
		   (code >= 0xF1 && code <= 0xFE)  ?  code - 0x20 : code;
}

int UTF8ICompare (DC_String a, DC_String b)
{
	while (a.ptr < a.end && b.ptr < b.end)
	{
		int nextA = UTF8ToUpper(UTF8Next(a));
		int nextB = UTF8ToUpper(UTF8Next(b));
		if (nextA != nextB)
			return nextA - nextB;
	}
	if (b.ptr != b.end)
		return -*b.ptr;
	if (a.ptr != a.end)
		return *a.ptr;
	return 0;
}