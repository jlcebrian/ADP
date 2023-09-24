#pragma once

#include <os_types.h>
#include <os_mem.h>
#include <dc.h>

// ---------- UTF8 handling ---------

// We're actually handling only a subset of UTF-8, because that's
// enough for DAAD supported languages. Specifically, we don't care
// about folding or case matching for anything outside the ISO88591 range.

#define INVALID_UNICODE_CHARACTER -1

extern int    UTF8Next        (DC_String& s);
extern size_t UTF8EncodedSize (int character);
extern bool   UTF8Encode      (DC_Buffer& buffer, int character);
extern int    UTF8ICompare    (DC_String a, DC_String b);

// ---------- Character set (byte codes) to UTF8 conversions ---------

struct DC_CodePage
{
	int         maxUTF8Character;     
	uint16_t*   characters;           
	int16_t*    pageIndex;           
	uint8_t*    conversions;          
};

extern DC_String ConvertFromUTF8    (DC_CodePage* charset, DC_Buffer buffer, DC_String s);
extern DC_String ConvertToUTF8      (DC_CodePage* charset, DC_Buffer buffer, DC_String s);
extern uint8_t   ConvertFromUnicode (DC_CodePage* charset, int character);
extern int       ConvertToUnicode   (DC_CodePage* charset, int character);

extern DC_CodePage CP437;
extern DC_CodePage CP850;
extern DC_CodePage CP1252;