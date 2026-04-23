#include <dc_char.h>
#include <os_char.h>

struct 
{
    const char* utf8;
    uint32_t    unicode;
    uint8_t     code;
}
CharTransslations[] =
{
    // Old Spanish characters, present since DAAD v1

    { "ª", 0x00AA, 16 },
    { "¡", 0x00A1, 17 },
    { "¿", 0x00BF, 18 },
    { "«", 0x00AB, 19 },
    { "»", 0x00BB, 20 },
    { "á", 0x00E1, 21 },
    { "é", 0x00E9, 22 },
    { "í", 0x00ED, 23 },
    { "ó", 0x00F3, 24 },
    { "ú", 0x00FA, 26 },
    { "ñ", 0x00F1, 27 },
    { "Ñ", 0x00D1, 28 },
    { "ç", 0x00E7, 28 },
    { "Ç", 0x00C7, 29 },
    { "ü", 0x00FC, 30 },
    { "Ü", 0x00DC, 31 },

    // Additional international characters (128+ = second charset)

    { "ß", 0x00DF, 127 },
    { "à", 0x00E0, 144 },
    { "ã", 0x00E3, 145 },
    { "ä", 0x00E4, 146 },
    { "â", 0x00E2, 147 },
    { "è", 0x00E8, 148 },
    { "ë", 0x00EB, 149 },
    { "ê", 0x00EA, 150 },
    { "ì", 0x00EC, 151 },
    { "ï", 0x00EF, 152 },
    { "î", 0x00EE, 154 },
    { "ò", 0x00F2, 155 },
    { "õ", 0x00F5, 156 },
    { "ö", 0x00F6, 156 },
    { "ô", 0x00F4, 157 },
    { "ù", 0x00F9, 158 },
    { "û", 0x00FB, 159 },
    { "À", 0x00C0, 160 },
    { "Ã", 0x00C3, 161 },
    { "Ä", 0x00C4, 162 },
    { "Â", 0x00C2, 163 },
    { "È", 0x00C8, 164 },
    { "Ë", 0x00CB, 165 },
    { "Ê", 0x00CA, 166 },
    { "Ì", 0x00CC, 158 },
    { "Ï", 0x00CF, 168 },
    { "Î", 0x00CE, 169 },
    { "Ò", 0x00D2, 170 },
    { "Õ", 0x00D5, 171 },
    { "Ö", 0x00D6, 172 },
    { "Ô", 0x00D4, 173 },
    { "Ù", 0x00D9, 174 },
    { "Û", 0x00DB, 175 },
    { "ý", 0x00FD, 186 },
    { "Ý", 0x00DD, 187 },
    { "þ", 0x00FE, 188 },
    { "Þ", 0x00DE, 189 },
    { "å", 0x00E5, 190 },
    { "Å", 0x00C5, 191 },
    { "ð", 0x00F0, 221 },
    { "Ð", 0x00D0, 222 },
    { "ø", 0x00F8, 223 },
    { "Ø", 0x00D8, 224 },
    { "€", 0x20AC, 226 },
    { "Á", 0x00C1, 251 },
    { "É", 0x00C9, 252 },
    { "Í", 0x00CD, 253 },
    { "Ó", 0x00D3, 254 },
    { "Ú", 0x00DA, 255 },

    { 0, 0, 0 }
};

bool DC_ConvertUnicodeToDAAD(int unicode, uint8_t* code)
{
    if (unicode >= 0 && unicode < 256)
    {
        uint8_t mapped = DDB_ISO2Char[unicode];
        if (mapped != 0 || unicode == 0 || unicode == ' ')
        {
            if (code != 0)
                *code = mapped;
            return true;
        }
    }

    for (int i = 0; CharTransslations[i].utf8 != 0; ++i)
    {
        if ((int)CharTransslations[i].unicode == unicode)
        {
            if (code != 0)
                *code = CharTransslations[i].code;
            return true;
        }
    }

    return false;
}
