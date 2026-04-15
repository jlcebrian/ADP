
#include <dmg.h>
#include <img.h>
#include <dmg_pack.h>
#include <ddb_pal.h>
#include <os_file.h>
#include <os_bito.h>
#include <os_mem.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <png.h>

static uint16_t ReadBE16(const uint8_t* ptr)
{
    return (uint16_t)((ptr[0] << 8) | ptr[1]);
}

static int CompareUint32(const void* a, const void* b)
{
    uint32_t av = *(const uint32_t*)a;
    uint32_t bv = *(const uint32_t*)b;
    if (av < bv)
        return -1;
    if (av > bv)
        return 1;
    return 0;
}

static uint32_t ColorDistance(uint32_t a, uint32_t b)
{
    int ar = (int)((a >> 16) & 0xFF);
    int ag = (int)((a >> 8) & 0xFF);
    int ab = (int)(a & 0xFF);
    int aa = (int)((a >> 24) & 0xFF);
    int br = (int)((b >> 16) & 0xFF);
    int bg = (int)((b >> 8) & 0xFF);
    int bb = (int)(b & 0xFF);
    int ba = (int)((b >> 24) & 0xFF);
    int dr = ar - br;
    int dg = ag - bg;
    int db = ab - bb;
    int da = aa - ba;
    return (uint32_t)(dr * dr + dg * dg + db * db + da * da);
}

static bool ReduceIndexedPalette(uint8_t* pixels, uint32_t pixelCount, uint32_t* palette, uint8_t* paletteAlpha, int* paletteSize, int maxColors)
{
    if (paletteSize == 0 || *paletteSize <= maxColors || maxColors <= 0)
        return true;

    uint32_t counts[256];
    uint8_t remap[256];
    for (int i = 0; i < 256; i++)
    {
        counts[i] = 0;
        remap[i] = 0;
    }

    for (uint32_t i = 0; i < pixelCount; i++)
        counts[pixels[i]]++;

    uint32_t reducedPalette[256];
    uint8_t reducedAlpha[256];
    int kept[256];
    int keptCount = 0;
    for (int slot = 0; slot < maxColors; slot++)
    {
        int best = -1;
        uint32_t bestCount = 0;
        for (int i = 0; i < *paletteSize; i++)
        {
            if (counts[i] > bestCount)
            {
                best = i;
                bestCount = counts[i];
            }
        }
        if (best < 0)
            break;
        kept[slot] = best;
        reducedPalette[slot] = palette[best];
        reducedAlpha[slot] = paletteAlpha ? paletteAlpha[best] : 255;
        counts[best] = 0;
        remap[best] = (uint8_t)slot;
        keptCount++;
    }

    if (keptCount <= 0)
    {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        return false;
    }

    for (int i = 0; i < *paletteSize; i++)
    {
        bool found = false;
        for (int slot = 0; slot < keptCount; slot++)
        {
            if (kept[slot] == i)
            {
                found = true;
                break;
            }
        }
        if (found)
            continue;

        uint32_t sourceColor = (paletteAlpha ? ((uint32_t)paletteAlpha[i] << 24) : 0xFF000000u) | (palette[i] & 0x00FFFFFFu);
        int bestSlot = 0;
        uint32_t bestDistance = ColorDistance(sourceColor, ((uint32_t)reducedAlpha[0] << 24) | (reducedPalette[0] & 0x00FFFFFFu));
        for (int slot = 1; slot < keptCount; slot++)
        {
            uint32_t distance = ColorDistance(sourceColor, ((uint32_t)reducedAlpha[slot] << 24) | (reducedPalette[slot] & 0x00FFFFFFu));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestSlot = slot;
            }
        }
        remap[i] = (uint8_t)bestSlot;
    }

    for (uint32_t i = 0; i < pixelCount; i++)
        pixels[i] = remap[pixels[i]];
    for (int i = 0; i < keptCount; i++)
    {
        palette[i] = reducedPalette[i];
        if (paletteAlpha)
            paletteAlpha[i] = reducedAlpha[i];
    }
    *paletteSize = keptCount;
    return true;
}

static bool BuildIndexedFromRGBA(const uint8_t* rgbaData, png_size_t rowBytes, uint8_t* buffer, uint32_t pixelCount, uint16_t pngWidth, uint16_t pngHeight, uint32_t* palette, uint8_t* paletteAlpha, int* paletteSize, int maxColors, bool* reduced, int* sourceColorCount)
{
    uint32_t* pixelKeys = Allocate<uint32_t>("PNG colors", pixelCount);
    uint32_t* sortedKeys = Allocate<uint32_t>("PNG colors", pixelCount);
    if (pixelKeys == 0 || sortedKeys == 0)
    {
        if (pixelKeys) Free(pixelKeys);
        if (sortedKeys) Free(sortedKeys);
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }

    uint32_t pos = 0;
    for (uint16_t y = 0; y < pngHeight; y++)
    {
        const uint8_t* row = rgbaData + y * rowBytes;
        for (uint16_t x = 0; x < pngWidth; x++)
        {
            pixelKeys[pos++] = ((uint32_t)row[x * 4 + 3] << 24) |
                ((uint32_t)row[x * 4 + 0] << 16) |
                ((uint32_t)row[x * 4 + 1] << 8) |
                (uint32_t)row[x * 4 + 2];
        }
    }

    MemCopy(sortedKeys, pixelKeys, pixelCount * sizeof(uint32_t));
    qsort(sortedKeys, pixelCount, sizeof(uint32_t), CompareUint32);

    uint32_t* uniqueColors = Allocate<uint32_t>("PNG palette", pixelCount);
    uint32_t* uniqueCounts = Allocate<uint32_t>("PNG palette", pixelCount);
    if (uniqueColors == 0 || uniqueCounts == 0)
    {
        if (uniqueColors) Free(uniqueColors);
        if (uniqueCounts) Free(uniqueCounts);
        Free(sortedKeys);
        Free(pixelKeys);
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        return false;
    }

    int uniqueCount = 0;
    for (uint32_t i = 0; i < pixelCount; i++)
    {
        if (i == 0 || sortedKeys[i] != sortedKeys[i - 1])
        {
            uniqueColors[uniqueCount] = sortedKeys[i];
            uniqueCounts[uniqueCount] = 1;
            uniqueCount++;
        }
        else
            uniqueCounts[uniqueCount - 1]++;
    }
    if (sourceColorCount)
        *sourceColorCount = uniqueCount;

    if (uniqueCount <= maxColors)
    {
        int outCount = 0;
        for (uint32_t i = 0; i < pixelCount; i++)
        {
            int index = -1;
            for (int p = 0; p < outCount; p++)
            {
                uint32_t key = ((paletteAlpha ? (uint32_t)paletteAlpha[p] : 0xFFu) << 24) | (palette[p] & 0x00FFFFFFu);
                if (key == pixelKeys[i])
                {
                    index = p;
                    break;
                }
            }
            if (index < 0)
            {
                index = outCount++;
                palette[index] = 0xFF000000u | (pixelKeys[i] & 0x00FFFFFFu);
                if (paletteAlpha)
                    paletteAlpha[index] = (uint8_t)(pixelKeys[i] >> 24);
            }
            buffer[i] = (uint8_t)index;
        }
        *paletteSize = outCount;
        if (reduced)
            *reduced = false;
    }
    else
    {
        if (reduced)
            *reduced = true;

        for (int slot = 0; slot < maxColors; slot++)
        {
            int best = slot;
            for (int i = slot + 1; i < uniqueCount; i++)
            {
                if (uniqueCounts[i] > uniqueCounts[best])
                    best = i;
            }
            if (best != slot)
            {
                uint32_t colorTmp = uniqueColors[slot];
                uniqueColors[slot] = uniqueColors[best];
                uniqueColors[best] = colorTmp;
                uint32_t countTmp = uniqueCounts[slot];
                uniqueCounts[slot] = uniqueCounts[best];
                uniqueCounts[best] = countTmp;
            }
            palette[slot] = 0xFF000000u | (uniqueColors[slot] & 0x00FFFFFFu);
            if (paletteAlpha)
                paletteAlpha[slot] = (uint8_t)(uniqueColors[slot] >> 24);
        }
        *paletteSize = maxColors;

        for (uint32_t i = 0; i < pixelCount; i++)
        {
            int best = 0;
            uint32_t bestDistance = ColorDistance(pixelKeys[i], ((paletteAlpha ? (uint32_t)paletteAlpha[0] : 0xFFu) << 24) | (palette[0] & 0x00FFFFFFu));
            for (int p = 1; p < maxColors; p++)
            {
                uint32_t distance = ColorDistance(pixelKeys[i], ((paletteAlpha ? (uint32_t)paletteAlpha[p] : 0xFFu) << 24) | (palette[p] & 0x00FFFFFFu));
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = p;
                }
            }
            buffer[i] = (uint8_t)best;
        }
    }

    Free(uniqueCounts);
    Free(uniqueColors);
    Free(sortedKeys);
    Free(pixelKeys);
    return true;
}

static uint32_t ReadBE32(const uint8_t* ptr)
{
    return ((uint32_t)ptr[0] << 24) |
           ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8) |
           (uint32_t)ptr[3];
}

static void WriteBE16(uint8_t* ptr, uint16_t value)
{
    ptr[0] = (uint8_t)(value >> 8);
    ptr[1] = (uint8_t)value;
}

static void WriteBE32(uint8_t* ptr, uint32_t value)
{
    ptr[0] = (uint8_t)(value >> 24);
    ptr[1] = (uint8_t)(value >> 16);
    ptr[2] = (uint8_t)(value >> 8);
    ptr[3] = (uint8_t)value;
}

static uint16_t RGB32ToSTColor(uint32_t color)
{
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    return (uint16_t)(((r >> 5) & 0x07) << 8 |
                      ((g >> 5) & 0x07) << 4 |
                      ((b >> 5) & 0x07));
}

static bool DecodeILBMBodyToIndexed(
    const uint8_t* body,
    uint32_t bodySize,
    uint8_t compression,
    uint16_t width,
    uint16_t height,
    uint8_t planes,
    uint8_t* output,
    size_t outputSize)
{
    if (planes == 0 || planes > 8)
        return false;
    if (outputSize < (size_t)width * height)
        return false;

    uint32_t rowBytes = ((uint32_t)width + 15u) >> 4;
    rowBytes <<= 1;
    uint32_t decodedSize = rowBytes * planes * height;
    uint8_t* decoded = Allocate<uint8_t>("ILBM body", decodedSize);
    if (decoded == 0)
        return false;

    bool ok = true;
    if (compression == 0)
    {
        if (bodySize < decodedSize)
            ok = false;
        else
            MemCopy(decoded, body, decodedSize);
    }
    else if (compression == 1)
    {
        const uint8_t* src = body;
        const uint8_t* end = body + bodySize;
        uint8_t* dst = decoded;
        uint8_t* dstEnd = decoded + decodedSize;
        while (src < end && dst < dstEnd)
        {
            int8_t control = (int8_t)*src++;
            if (control >= 0)
            {
                uint32_t count = (uint32_t)control + 1;
                if ((uint32_t)(end - src) < count || (uint32_t)(dstEnd - dst) < count)
                {
                    ok = false;
                    break;
                }
                MemCopy(dst, src, count);
                src += count;
                dst += count;
            }
            else if (control != -128)
            {
                uint32_t count = (uint32_t)(1 - control);
                if (src >= end || (uint32_t)(dstEnd - dst) < count)
                {
                    ok = false;
                    break;
                }
                uint8_t value = *src++;
                MemSet(dst, value, count);
                dst += count;
            }
        }
        ok = ok && dst == dstEnd;
    }
    else
    {
        ok = false;
    }

    if (ok)
    {
        for (uint16_t y = 0; y < height; y++)
        {
            const uint8_t* row = decoded + y * rowBytes * planes;
            uint8_t* dst = output + y * width;
            for (uint16_t x = 0; x < width; x++)
            {
                uint8_t color = 0;
                uint8_t mask = (uint8_t)(0x80u >> (x & 7));
                uint32_t byteIndex = x >> 3;
                for (uint8_t plane = 0; plane < planes; plane++)
                {
                    if (row[plane * rowBytes + byteIndex] & mask)
                        color |= (uint8_t)(1u << plane);
                }
                dst[x] = color;
            }
        }
    }

    Free(decoded);
    return ok;
}

static bool EncodeIndexedToILBMBody(
    const uint8_t* input,
    uint16_t width,
    uint16_t height,
    uint8_t planes,
    uint8_t* output,
    uint32_t outputSize)
{
    uint32_t rowBytes = ((uint32_t)width + 15u) >> 4;
    rowBytes <<= 1;
    uint32_t required = rowBytes * height * planes;
    if (planes == 0 || planes > 8 || outputSize < required)
        return false;

    MemClear(output, required);
    for (uint16_t y = 0; y < height; y++)
    {
        const uint8_t* src = input + y * width;
        uint8_t* row = output + y * rowBytes * planes;
        for (uint8_t plane = 0; plane < planes; plane++)
        {
            uint8_t* planeRow = row + plane * rowBytes;
            uint8_t planeMask = (uint8_t)(1u << plane);
            for (uint16_t x = 0; x < width; x++)
            {
                if ((src[x] & planeMask) == 0)
                    continue;
                uint32_t byteIndex = x >> 3;
                planeRow[byteIndex] |= (uint8_t)(0x80u >> (x & 7));
            }
        }
    }
    return true;
}

bool LoadPI1Indexed(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize)
{
    File* file = File_Open(filename, ReadOnly);
    if (file == 0)
        return false;

    uint64_t size = File_GetSize(file);
    if (size != 32034)
    {
        File_Close(file);
        return false;
    }

    uint8_t* fileData = Allocate<uint8_t>("PI1 file", (uint32_t)size);
    if (fileData == 0)
    {
        File_Close(file);
        return false;
    }

    bool ok = File_Read(file, fileData, size) == size;
    File_Close(file);
    if (!ok)
    {
        Free(fileData);
        return false;
    }

    uint16_t resolution = ReadBE16(fileData);
    if (resolution != 0 || bufferSize < 320u * 200u)
    {
        Free(fileData);
        return false;
    }

    for (int i = 0; i < 16; i++)
        palette[i] = Pal2RGB(ReadBE16(fileData + 2 + i * 2), false);
    if (paletteSize)
        *paletteSize = 16;
    *width = 320;
    *height = 200;

    const uint8_t* planar = fileData + 34;
    for (int y = 0; y < 200; y++)
    {
        const uint8_t* row = planar + y * 160;
        uint8_t* out = buffer + y * 320;
        for (int block = 0; block < 20; block++)
        {
            uint16_t bits0 = ReadBE16(row + block * 8 + 0);
            uint16_t bits1 = ReadBE16(row + block * 8 + 2);
            uint16_t bits2 = ReadBE16(row + block * 8 + 4);
            uint16_t bits3 = ReadBE16(row + block * 8 + 6);
            for (int bit = 0; bit < 16; bit++)
            {
                uint16_t mask = (uint16_t)(0x8000u >> bit);
                uint8_t color = (bits0 & mask) ? 0x01 : 0x00;
                if (bits1 & mask) color |= 0x02;
                if (bits2 & mask) color |= 0x04;
                if (bits3 & mask) color |= 0x08;
                *out++ = color;
            }
        }
    }

    Free(fileData);
    return true;
}

bool SavePI1Indexed(const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize)
{
    if (width != 320 || height != 200 || paletteSize <= 0 || paletteSize > 16)
        return false;

    uint8_t* fileData = Allocate<uint8_t>("PI1 output", 32034);
    if (fileData == 0)
        return false;

    MemClear(fileData, 32034);
    WriteBE16(fileData, 0);
    for (int i = 0; i < 16; i++)
    {
        uint16_t color = i < paletteSize ? RGB32ToSTColor(palette[i]) : 0;
        WriteBE16(fileData + 2 + i * 2, color);
    }

    if (!DMG_PackBitplaneWords(pixels, width, height, 4, fileData + 34))
    {
        Free(fileData);
        return false;
    }

    File* file = File_Create(filename);
    if (file == 0)
    {
        Free(fileData);
        return false;
    }

    bool ok = File_Write(file, fileData, 32034) == 32034;
    File_Close(file);
    Free(fileData);
    return ok;
}

bool LoadIFFIndexed(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize)
{
    File* file = File_Open(filename, ReadOnly);
    if (file == 0)
        return false;

    uint32_t fileSize = (uint32_t)File_GetSize(file);
    if (fileSize < 12)
    {
        File_Close(file);
        return false;
    }

    uint8_t* data = Allocate<uint8_t>("IFF file", fileSize);
    if (data == 0)
    {
        File_Close(file);
        return false;
    }

    bool ok = File_Read(file, data, fileSize) == fileSize;
    File_Close(file);
    if (!ok)
    {
        Free(data);
        return false;
    }

    if (memcmp(data, "FORM", 4) != 0 || memcmp(data + 8, "ILBM", 4) != 0)
    {
        Free(data);
        return false;
    }

    const uint8_t* ptr = data + 12;
    const uint8_t* end = data + fileSize;
    const uint8_t* body = 0;
    uint32_t bodySize = 0;
    uint16_t ilbmWidth = 0;
    uint16_t ilbmHeight = 0;
    uint8_t ilbmPlanes = 0;
    uint8_t ilbmCompression = 0;
    int colors = 0;

    while (ptr + 8 <= end)
    {
        uint32_t chunkSize = ReadBE32(ptr + 4);
        const uint8_t* chunkData = ptr + 8;
        if (chunkData + chunkSize > end)
        {
            ok = false;
            break;
        }

        if (memcmp(ptr, "BMHD", 4) == 0)
        {
            if (chunkSize < 20)
            {
                ok = false;
                break;
            }
            ilbmWidth = ReadBE16(chunkData + 0);
            ilbmHeight = ReadBE16(chunkData + 2);
            ilbmPlanes = chunkData[8];
            uint8_t masking = chunkData[9];
            ilbmCompression = chunkData[10];
            if (masking != 0 || ilbmWidth == 0 || ilbmHeight == 0 || ilbmPlanes == 0 || ilbmPlanes > 8)
            {
                ok = false;
                break;
            }
        }
        else if (memcmp(ptr, "CMAP", 4) == 0)
        {
            colors = (int)(chunkSize / 3);
            if (colors <= 0 || colors > 256)
            {
                ok = false;
                break;
            }
            for (int i = 0; i < colors; i++)
            {
                palette[i] = 0xFF000000u |
                    ((uint32_t)chunkData[i * 3 + 0] << 16) |
                    ((uint32_t)chunkData[i * 3 + 1] << 8) |
                    (uint32_t)chunkData[i * 3 + 2];
            }
        }
        else if (memcmp(ptr, "BODY", 4) == 0)
        {
            body = chunkData;
            bodySize = chunkSize;
        }

        ptr = chunkData + chunkSize + (chunkSize & 1u);
    }

    if (ok && (body == 0 || ilbmWidth == 0 || ilbmHeight == 0 || colors == 0))
        ok = false;
    if (ok && !DecodeILBMBodyToIndexed(body, bodySize, ilbmCompression, ilbmWidth, ilbmHeight, ilbmPlanes, buffer, bufferSize))
        ok = false;

    if (ok)
    {
        *width = ilbmWidth;
        *height = ilbmHeight;
        if (paletteSize)
            *paletteSize = colors;
    }

    Free(data);
    return ok;
}

static uint32_t EncodeByteRun1(const uint8_t* input, uint32_t size, uint8_t* output, uint32_t outputCapacity)
{
    uint32_t in = 0;
    uint32_t out = 0;

    while (in < size)
    {
        uint32_t run = 1;
        while (in + run < size && input[in] == input[in + run] && run < 128)
            run++;
        if (run >= 2)
        {
            if (out + 2 > outputCapacity)
                return 0;
            output[out++] = (uint8_t)(1 - (int)run);
            output[out++] = input[in];
            in += run;
            continue;
        }

        uint32_t literalStart = in++;
        while (in < size)
        {
            run = 1;
            while (in + run < size && input[in] == input[in + run] && run < 128)
                run++;
            if (run >= 2 || in - literalStart >= 128)
                break;
            in++;
        }

        uint32_t literalCount = in - literalStart;
        if (out + 1 + literalCount > outputCapacity)
            return 0;
        output[out++] = (uint8_t)(literalCount - 1);
        MemCopy(output + out, input + literalStart, literalCount);
        out += literalCount;
    }

    return out;
}

bool SaveIFFIndexed(const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize)
{
    if (width == 0 || height == 0 || paletteSize <= 0 || paletteSize > 256)
        return false;

    uint8_t planes = 1;
    while ((1u << planes) < (uint32_t)paletteSize && planes < 8)
        planes++;
    if ((1u << planes) < (uint32_t)paletteSize)
        return false;

    uint32_t rowBytes = ((uint32_t)width + 15u) >> 4;
    rowBytes <<= 1;
    uint32_t bodyRawSize = rowBytes * height * planes;
    uint8_t* bodyRaw = Allocate<uint8_t>("ILBM raw", bodyRawSize);
    if (bodyRaw == 0)
        return false;
    if (!EncodeIndexedToILBMBody(pixels, width, height, planes, bodyRaw, bodyRawSize))
    {
        Free(bodyRaw);
        return false;
    }

    uint32_t compressedCapacity = bodyRawSize * 2 + 256;
    uint8_t* bodyCompressed = Allocate<uint8_t>("ILBM body", compressedCapacity);
    if (bodyCompressed == 0)
    {
        Free(bodyRaw);
        return false;
    }
    uint32_t compressedSize = EncodeByteRun1(bodyRaw, bodyRawSize, bodyCompressed, compressedCapacity);
    const uint8_t* body = bodyRaw;
    uint32_t bodySize = bodyRawSize;
    uint8_t compression = 0;
    if (compressedSize != 0 && compressedSize < bodyRawSize)
    {
        body = bodyCompressed;
        bodySize = compressedSize;
        compression = 1;
    }

    uint32_t cmapSize = (uint32_t)paletteSize * 3;
    uint32_t formSize = 4 + (8 + 20) + (8 + cmapSize + (cmapSize & 1u)) + (8 + bodySize + (bodySize & 1u));
    uint8_t* fileData = Allocate<uint8_t>("ILBM file", formSize + 8);
    if (fileData == 0)
    {
        Free(bodyCompressed);
        Free(bodyRaw);
        return false;
    }

    uint8_t* ptr = fileData;
    MemCopy(ptr, "FORM", 4); ptr += 4;
    WriteBE32(ptr, formSize); ptr += 4;
    MemCopy(ptr, "ILBM", 4); ptr += 4;

    MemCopy(ptr, "BMHD", 4); ptr += 4;
    WriteBE32(ptr, 20); ptr += 4;
    WriteBE16(ptr + 0, width);
    WriteBE16(ptr + 2, height);
    WriteBE16(ptr + 4, 0);
    WriteBE16(ptr + 6, 0);
    ptr[8] = planes;
    ptr[9] = 0;
    ptr[10] = compression;
    ptr[11] = 0;
    WriteBE16(ptr + 12, 0);
    ptr[14] = 10;
    ptr[15] = 10;
    WriteBE16(ptr + 16, width);
    WriteBE16(ptr + 18, height);
    ptr += 20;

    MemCopy(ptr, "CMAP", 4); ptr += 4;
    WriteBE32(ptr, cmapSize); ptr += 4;
    for (int i = 0; i < paletteSize; i++)
    {
        *ptr++ = (uint8_t)(palette[i] >> 16);
        *ptr++ = (uint8_t)(palette[i] >> 8);
        *ptr++ = (uint8_t)palette[i];
    }
    if (cmapSize & 1u)
        *ptr++ = 0;

    MemCopy(ptr, "BODY", 4); ptr += 4;
    WriteBE32(ptr, bodySize); ptr += 4;
    MemCopy(ptr, body, bodySize);
    ptr += bodySize;
    if (bodySize & 1u)
        *ptr++ = 0;

    File* file = File_Create(filename);
    if (file == 0)
    {
        Free(fileData);
        Free(bodyCompressed);
        Free(bodyRaw);
        return false;
    }

    bool ok = File_Write(file, fileData, (uint32_t)(ptr - fileData)) == (uint32_t)(ptr - fileData);
    File_Close(file);
    Free(fileData);
    Free(bodyCompressed);
    Free(bodyRaw);
    return ok;
}

bool LoadPNGIndexed16(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette)
{
	return LoadPNGIndexed16(filename, buffer, bufferSize, width, height, palette, 0);
}

bool LoadPNGIndexed16(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, uint8_t* paletteAlpha)
{
    int paletteSize = 0;
    return LoadPNGIndexed(filename, buffer, bufferSize, width, height, palette, &paletteSize, paletteAlpha, 16) && paletteSize <= 16;
}

bool LoadPNGIndexed(const char* filename, uint8_t* buffer, size_t bufferSize, uint16_t* width, uint16_t* height, uint32_t* palette, int* paletteSize, uint8_t* paletteAlpha, int maxColors, bool* reduced, int* sourceColorCount)
{
	png_uint_32 y;
	png_structp png;
	png_infop info;
	png_uint_32 pngWidth;
	png_uint_32 pngHeight;
	int colorType, bitDepth;
	png_colorp pngPalette;
    int numPaletteEntries;
	png_bytep transparency = 0;
	int numTransparency = 0;
	png_bytep* rowPointers;

    if (reduced)
        *reduced = false;
    if (sourceColorCount)
        *sourceColorCount = 0;
    if (maxColors <= 0)
        maxColors = 256;
    if (maxColors > 256)
        maxColors = 256;

	FILE* file = fopen(filename, "rb");
    if (!file) {
        DMG_SetError(DMG_ERROR_FILE_NOT_FOUND);
        return false; // Failed to open the file
    }
    
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        if (png)
            png_destroy_read_struct(&png, NULL, NULL);
        fclose(file);
        return false; // Failed to create png_struct
    }
    
    if (setjmp(png_jmpbuf(png))) {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false; // Error during PNG read
    }
    
    png_init_io(png, file);
    png_read_info(png, info);
    
    colorType = png_get_color_type(png, info);
    pngWidth = png_get_image_width(png, info);
    pngHeight = png_get_image_height(png, info);
    if (bufferSize < (size_t)pngWidth * pngHeight) {
        DMG_SetError(DMG_ERROR_BUFFER_TOO_SMALL);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false;
    }
    bitDepth = png_get_bit_depth(png, info);
    
    if (bitDepth > 8) {
        png_set_strip_16(png);
    }

    if (colorType == PNG_COLOR_TYPE_PALETTE && bitDepth < 8) {
        png_set_packing(png);
        png_read_update_info(png, info);
        bitDepth = png_get_bit_depth(png, info);
    }

    *width = (uint16_t)pngWidth;
    *height = (uint16_t)pngHeight;

    if (paletteAlpha)
    {
        for (int i = 0; i < 256; i++)
            paletteAlpha[i] = 255;
    }

    if (colorType != PNG_COLOR_TYPE_PALETTE) {
        if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
            png_set_expand_gray_1_2_4_to_8(png);
        if (png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(png);
        if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);
        if ((colorType & PNG_COLOR_MASK_ALPHA) == 0 && !png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

        png_read_update_info(png, info);

        png_size_t rowBytes = png_get_rowbytes(png, info);
        uint8_t* rgbaData = Allocate<uint8_t>("PNG RGBA", rowBytes * pngHeight);
        if (rgbaData == 0) {
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            png_destroy_read_struct(&png, &info, NULL);
            fclose(file);
            return false;
        }

        rowPointers = Allocate<png_bytep>("PNG buffer", pngHeight * sizeof(png_bytep));
        if (rowPointers == 0) {
            DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
            Free(rgbaData);
            png_destroy_read_struct(&png, &info, NULL);
            fclose(file);
            return false;
        }
        for (y = 0; y < pngHeight; y++)
            rowPointers[y] = (png_bytep)(rgbaData + y * rowBytes);

        png_read_image(png, rowPointers);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        Free(rowPointers);

        if (!BuildIndexedFromRGBA(rgbaData, rowBytes, buffer, (uint32_t)pngWidth * pngHeight, (uint16_t)pngWidth, (uint16_t)pngHeight, palette, paletteAlpha, paletteSize, maxColors, reduced, sourceColorCount))
        {
            Free(rgbaData);
            return false;
        }
        Free(rgbaData);
        return true;
    }
    
    png_get_PLTE(png, info, &pngPalette, &numPaletteEntries);
	png_get_tRNS(png, info, &transparency, &numTransparency, 0);
    
    if (numPaletteEntries > 256 || numPaletteEntries <= 0) {
        DMG_SetError(DMG_ERROR_INVALID_IMAGE);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false; // Invalid palette size
    }
	
	#ifdef _DMG
	if (numPaletteEntries > 16) {
		bool colorFound = false;
    	for (int i = 16; i < numPaletteEntries; i++) {
			if (pngPalette[i].red != pngPalette[i].green || pngPalette[i].red != pngPalette[i].blue) {
				colorFound = true;
				break;
			}
		}
		if (colorFound) {
			DMG_Warning("Image has palette size %d, larger than 16", numPaletteEntries);
		}
	}
	#endif
    
    for (int i = 0; i < numPaletteEntries && i < 256; i++) {
        palette[i] = 0xFF000000 | (pngPalette[i].red << 16) | (pngPalette[i].green << 8) | pngPalette[i].blue;
    }
    if (sourceColorCount)
        *sourceColorCount = numPaletteEntries;
	if (paletteAlpha)
		for (int i = 0; i < numTransparency && i < 256; i++)
			paletteAlpha[i] = transparency[i];

	if (paletteAlpha)
		for (int i = numTransparency; i < 256; i++)
			paletteAlpha[i] = 255;
    rowPointers = Allocate<png_bytep>("PNG buffer", pngHeight * sizeof(png_bytep));
    if (rowPointers == 0) {
        DMG_SetError(DMG_ERROR_OUT_OF_MEMORY);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return false;
    }
    for (y = 0; y < pngHeight; y++) {
        rowPointers[y] = (png_bytep)(buffer + y * pngWidth);
    }
    
    png_read_image(png, rowPointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(file);
    Free(rowPointers);

    int entryCount = numPaletteEntries;
    if (numPaletteEntries > maxColors)
    {
        if (!ReduceIndexedPalette(buffer, (uint32_t)pngWidth * pngHeight, palette, paletteAlpha, &entryCount, maxColors))
            return false;
    }
    if (reduced)
        *reduced = entryCount != numPaletteEntries;
    if (paletteSize)
        *paletteSize = entryCount;
    
	return true; // Successfully loaded the image
}

bool SaveCOLPalette16 (const char* filename, uint32_t* palette)
{
    return SaveCOLPalette(filename, palette, 16);
}

bool SaveCOLPalette (const char* filename, uint32_t* palette, int paletteSize)
{
	int n;
    if (paletteSize <= 0 || paletteSize > 256)
        return false;

    uint32_t dataSize = 8 + paletteSize * 3;
	uint8_t* data = Allocate<uint8_t>("COL palette", dataSize);
    if (!data)
        return false;

	File* file = File_Create(filename);
	if (!file)
    {
        Free(data);
		return false; // Failed to open the file
    }

	write32(data, dataSize, true);
	write16(data + 4, 0xB123, true);
	write16(data + 6, 0x0000, true);
	for (n = 0; n < paletteSize; n++)
	{
		uint8_t r = (uint8_t)(palette[n] >> 16);
		uint8_t g = (uint8_t)(palette[n] >> 8);
		uint8_t b = (uint8_t)palette[n];
		data[8 + n*3] = r;
		data[8 + n*3 + 1] = g;
		data[8 + n*3 + 2] = b;
	}

	bool success = File_Write(file, data, dataSize) == dataSize;
	File_Close(file);
    Free(data);
	return success;
}

bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette)
{
	return SavePNGIndexed16(filename, pixels, width, height, palette, 16, 0);
}

bool SavePNGIndexed16 (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize, const uint8_t* paletteAlpha)
{
    return SavePNGIndexed(filename, pixels, width, height, palette, paletteSize, paletteAlpha);
}

bool SavePNGIndexed (const char* filename, uint8_t* pixels, uint16_t width, uint16_t height, uint32_t* palette, int paletteSize, const uint8_t* paletteAlpha)
{
	FILE* file = fopen(filename, "wb");
	if (!file)
		return false; // Failed to open the file

	if (paletteSize <= 0 || paletteSize > 256)
	{
		fclose(file);
		return false;
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info || !png) {
		png_destroy_write_struct(&png, NULL);
		fclose(file);
		return false; // Failed to create png_info
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return false; // Error during PNG write
	}

	png_init_io(png, file);
	png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_colorp pngPalette = (png_colorp)png_malloc(png, paletteSize * sizeof(png_color));
	for (int i = 0; i < paletteSize; i++) {
		pngPalette[i].red = (png_byte)(palette[i] >> 16);
		pngPalette[i].green = (png_byte)(palette[i] >> 8);
		pngPalette[i].blue = (png_byte)palette[i];
	}

	png_set_PLTE(png, info, pngPalette, paletteSize);
	if (paletteAlpha)
		png_set_tRNS(png, info, (png_bytep)paletteAlpha, paletteSize, 0);

	png_bytep* rowPointers = (png_bytep*)png_malloc(png, height * sizeof(png_bytep));
	for (int y = 0; y < height; y++) {
		rowPointers[y] = (png_bytep)(pixels + y * width);
	}

	png_set_rows(png, info, rowPointers);
	png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
	png_free(png, pngPalette);
	png_free(png, rowPointers);
	png_destroy_write_struct(&png, &info);
	fclose(file);
	return true; // Successfully saved the image
}

bool SavePNGRGB32 (const char* filename, uint32_t* pixels, uint16_t width, uint16_t height)
{
	int n;

	FILE* file = fopen(filename, "wb");
	if (!file)
	{
		return false; // Failed to open the file
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info || !png) {
		png_destroy_write_struct(&png, NULL);
		fclose(file);
		return false; // Failed to create png_info
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return false; // Error during PNG write
	}

	png_init_io(png, file);
	png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_bytep* rowPointers = (png_bytep*)png_malloc(png, height * sizeof(png_bytep));
	for (n = 0; n < height; n++) {
		rowPointers[n] = (png_bytep)(pixels + n * width);
	}

	png_set_rows(png, info, rowPointers);
	png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
	png_free(png, rowPointers);
	png_destroy_write_struct(&png, &info);
	fclose(file);

	return true; // Successfully saved the image
}
