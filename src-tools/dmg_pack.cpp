#include <dmg_pack.h>

#include <os_lib.h>

bool DMG_PackChunkyPixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
	if (bitsPerPixel != 2 && bitsPerPixel != 4)
		return false;

	const uint32_t pixels = (uint32_t)width * height;
	const uint8_t mask = (1u << bitsPerPixel) - 1;
	uint8_t value = 0;
	int bitsFilled = 0;

	for (uint32_t i = 0; i < pixels; i++)
	{
		value = (uint8_t)((value << bitsPerPixel) | (input[i] & mask));
		bitsFilled += bitsPerPixel;
		if (bitsFilled == 8)
		{
			*output++ = value;
			value = 0;
			bitsFilled = 0;
		}
	}

	if (bitsFilled != 0)
		*output = (uint8_t)(value << (8 - bitsFilled));
	return true;
}

bool DMG_PackPlanar8Pixels(const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output)
{
	const uint32_t blocksPerRow = ((uint32_t)width + 7) >> 3;

	for (uint16_t y = 0; y < height; y++)
	{
		const uint8_t* row = input + (uint32_t)y * width;
		for (uint32_t block = 0; block < blocksPerRow; block++)
		{
			uint8_t planes[4] = { 0, 0, 0, 0 };
			for (uint32_t bit = 0; bit < 8; bit++)
			{
				uint32_t x = (block << 3) + bit;
				uint8_t color = x < width ? row[x] : 0;
				uint8_t mask = (uint8_t)(0x80u >> bit);
				if (color & 0x01) planes[0] |= mask;
				if (color & 0x02) planes[1] |= mask;
				if (color & 0x04) planes[2] |= mask;
				if (color & 0x08) planes[3] |= mask;
			}
			*output++ = planes[0];
			*output++ = planes[1];
			*output++ = planes[2];
			*output++ = planes[3];
		}
	}

	return true;
}

bool DMG_PackBitplaneBytes(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
	if (bitsPerPixel == 0 || bitsPerPixel > 8)
		return false;

	const uint32_t wordsPerRow = (width + 15) >> 4;
	const uint32_t planeStride = wordsPerRow * height * 2;
	MemClear(output, planeStride * bitsPerPixel);

	for (uint8_t plane = 0; plane < bitsPerPixel; plane++)
	{
		uint8_t* planePtr = output + plane * planeStride;
		uint8_t planeMask = (uint8_t)(1u << plane);
		for (uint16_t y = 0; y < height; y++)
		{
			const uint8_t* src = input + y * width;
			uint8_t* rowPtr = planePtr + y * wordsPerRow * 2;
			for (uint32_t wordIndex = 0; wordIndex < wordsPerRow; wordIndex++)
			{
				uint16_t baseX = (uint16_t)(wordIndex << 4);
				uint16_t word = 0;
				for (uint16_t bit = 0; bit < 16; bit++)
				{
					uint16_t x = (uint16_t)(baseX + bit);
					if (x < width && (src[x] & planeMask))
						word |= (uint16_t)(0x8000 >> bit);
				}
				uint8_t* dst = rowPtr + wordIndex * 2;
				dst[0] = (uint8_t)(word >> 8);
				dst[1] = (uint8_t)word;
			}
		}
	}
	return true;
}

bool DMG_PackBitplaneWords(const uint8_t* input, uint16_t width, uint16_t height, uint8_t bitsPerPixel, uint8_t* output)
{
	if (bitsPerPixel == 0 || bitsPerPixel > 8)
		return false;

	const uint32_t wordsPerRow = (width + 15) >> 4;
	const uint32_t rowStride = wordsPerRow * bitsPerPixel * 2;
	MemClear(output, rowStride * height);

	for (uint16_t y = 0; y < height; y++)
	{
		const uint8_t* src = input + y * width;
		uint8_t* rowPtr = output + y * rowStride;
		for (uint32_t wordIndex = 0; wordIndex < wordsPerRow; wordIndex++)
		{
			uint16_t baseX = (uint16_t)(wordIndex << 4);
			for (uint8_t plane = 0; plane < bitsPerPixel; plane++)
			{
				uint16_t word = 0;
				uint8_t planeMask = (uint8_t)(1u << plane);
				for (uint16_t bit = 0; bit < 16; bit++)
				{
					uint16_t x = (uint16_t)(baseX + bit);
					if (x < width && (src[x] & planeMask))
						word |= (uint16_t)(0x8000 >> bit);
				}
				uint8_t* dst = rowPtr + (wordIndex * bitsPerPixel + plane) * 2;
				dst[0] = (uint8_t)(word >> 8);
				dst[1] = (uint8_t)word;
			}
		}
	}
	return true;
}