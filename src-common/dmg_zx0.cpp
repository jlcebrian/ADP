#include <dmg.h>
#include <os_lib.h>
#include <os_mem.h>

#if defined(_UNIX) || defined(_OSX) || defined(_WEB)
#define DMG_USE_SALVADOR_COMPRESSOR 1
#endif

#if DMG_USE_SALVADOR_COMPRESSOR
extern "C" {
#include "../src-salvador/libsalvador.h"
}
#endif

namespace
{
#ifdef _AMIGA
    extern "C" void zx0_decompress(const uint8_t* input, uint8_t* output);
    extern "C" void zx0_decompress_fast(const uint8_t* input, uint8_t* output);
    static const uint32_t ZX0_FAST_68K_MAX_OUTPUT = 65535;
#else
    struct ZX0_State
    {
        const uint8_t* input;
        const uint8_t* inputEnd;
        uint8_t* output;
        uint8_t* outputStart;
        uint8_t* outputEnd;
        int bitMask;
        int bitValue;
        bool backtrack;
        int lastByte;
    };

    static inline bool ZX0_ReadByte(ZX0_State& s, int* value)
    {
        if (s.input >= s.inputEnd)
            return false;
        s.lastByte = *s.input++;
        *value = s.lastByte;
        return true;
    }

    static inline bool ZX0_ReadBit(ZX0_State& s, int* value)
    {
        if (s.backtrack)
        {
            s.backtrack = false;
            *value = s.lastByte & 1;
            return true;
        }

        s.bitMask >>= 1;
        if (s.bitMask == 0)
        {
            s.bitMask = 128;
            if (!ZX0_ReadByte(s, &s.bitValue))
                return false;
        }

        *value = (s.bitValue & s.bitMask) ? 1 : 0;
        return true;
    }

    static bool ZX0_ReadInterlacedEliasGamma(ZX0_State& s, bool inverted, int* value)
    {
        int bit;
        int out = 1;

        while (true)
        {
            if (!ZX0_ReadBit(s, &bit))
                return false;
            if (bit)
                break;
            if (!ZX0_ReadBit(s, &bit))
                return false;
            out = (out << 1) | (bit ^ (inverted ? 1 : 0));
        }

        *value = out;
        return true;
    }

    static bool ZX0_WriteByte(ZX0_State& s, int value)
    {
        if (s.output >= s.outputEnd)
            return false;
        *s.output++ = (uint8_t)value;
        return true;
    }

    static bool ZX0_WriteBytes(ZX0_State& s, int offset, int length)
    {
        if (offset <= 0 || s.output - offset < s.outputStart)
            return false;

        while (length-- > 0)
        {
            if (s.output >= s.outputEnd)
                return false;
            *s.output = *(s.output - offset);
            s.output++;
        }
        return true;
    }
#endif
}

bool DMG_DecompressZX0(const uint8_t* data, uint32_t dataLength, uint8_t* buffer, uint32_t outputSize)
{
#ifdef _AMIGA
    (void)dataLength;
    if (outputSize <= ZX0_FAST_68K_MAX_OUTPUT)
        zx0_decompress_fast(data, buffer);
    else
        zx0_decompress(data, buffer);
    return true;
#else
    ZX0_State state;
    state.input = data;
    state.inputEnd = data + dataLength;
    state.output = buffer;
    state.outputStart = buffer;
    state.outputEnd = buffer + outputSize;
    state.bitMask = 0;
    state.bitValue = 0;
    state.backtrack = false;
    state.lastByte = 0;

    int lastOffset = 1;
    int length = 0;
    int bit = 0;
    int value = 0;

COPY_LITERALS:
    if (!ZX0_ReadInterlacedEliasGamma(state, false, &length))
        return false;
    while (length-- > 0)
    {
        if (!ZX0_ReadByte(state, &value) || !ZX0_WriteByte(state, value))
            return false;
    }
    if (state.output >= state.outputEnd)
        return true;
    if (!ZX0_ReadBit(state, &bit))
        return false;
    if (bit)
        goto COPY_FROM_NEW_OFFSET;

//COPY_FROM_LAST_OFFSET:
    if (!ZX0_ReadInterlacedEliasGamma(state, false, &length))
        return false;
    if (!ZX0_WriteBytes(state, lastOffset, length))
        return false;
    if (state.output >= state.outputEnd)
        return true;
    if (!ZX0_ReadBit(state, &bit))
        return false;
    if (!bit)
        goto COPY_LITERALS;

COPY_FROM_NEW_OFFSET:
    if (!ZX0_ReadInterlacedEliasGamma(state, true, &value))
        return false;
    if (value == 256)
        return state.output == state.outputEnd && state.input == state.inputEnd;
    if (!ZX0_ReadByte(state, &bit))
        return false;
    lastOffset = value * 128 - (bit >> 1);
    state.backtrack = true;
    if (!ZX0_ReadInterlacedEliasGamma(state, false, &length))
        return false;
    if (!ZX0_WriteBytes(state, lastOffset, length + 1))
        return false;
    if (state.output >= state.outputEnd)
        return true;
    if (!ZX0_ReadBit(state, &bit))
        return false;
    if (bit)
        goto COPY_FROM_NEW_OFFSET;
    goto COPY_LITERALS;
#endif
}

uint8_t* DMG_CompressZX0(const uint8_t* data, uint32_t dataLength, uint32_t* outputSize)
{
#if !DMG_USE_SALVADOR_COMPRESSOR
    (void)data;
    (void)dataLength;
    if (outputSize)
        *outputSize = 0;
    return 0;
#else
    const uint32_t maxCompressedSize = (uint32_t)salvador_get_max_compressed_size((size_t)dataLength);
    uint8_t* compressed = Allocate<uint8_t>("ZX0 compressed", maxCompressedSize, false);
    if (compressed == 0)
    {
        if (outputSize)
            *outputSize = 0;
        return 0;
    }

    const size_t compressedSize = salvador_compress(
        data,
        compressed,
        (size_t)dataLength,
        (size_t)maxCompressedSize,
        FLG_IS_INVERTED,
        0,
        0,
        0,
        0);

    if (compressedSize == (size_t)-1)
    {
        Free(compressed);
        if (outputSize)
            *outputSize = 0;
        return 0;
    }

    if (outputSize)
        *outputSize = (uint32_t)compressedSize;
    return compressed;
#endif
}
