#include <dmg.h>
#include <os_mem.h>

bool DMG_UncCGAToPacked(const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output, int pixels)
{
	const uint8_t* end = input + width*height/2;
	uint8_t* outputEnd = output + pixels;

	while (height > 0 && input < end && output < outputEnd) 
	{
		const uint8_t* src = input;
		for (int x = 0; x < width; x += 4)
		{
			uint8_t c = *src++;
			*output++ = ((c & 0xC0) >> 2) | ((c & 0x30) >> 4);
			*output++ = ((c & 0x0C) << 2) | ((c & 0x03)     );
		}
		input += width/4;
		height--;
	}
	return true;// input == end && output == outputEnd;
}

static void ProcessCGABuffer (const uint8_t* buffer, int width, int height, uint8_t* output)
{
	int x, y;
	int rowStride = width/4;

	for (y = 0; y < height; y++)
	{
		int base = y*rowStride;
		if (y & 1)
		{
			uint8_t* ptr = output + (y * width)/2 + width/2 - 1;
			for (x = 0; x < width; x += 4)
			{
				*ptr-- = ( buffer[base] & 0x03)       | ((buffer[base] & 0x0C) << 2);
				*ptr-- = ((buffer[base] & 0x30) >> 4) | ((buffer[base] & 0xC0) >> 2);
				base++;
			}
		}
		else
		{
			uint8_t* ptr = output + (y * width)/2;
			for (x = 0; x < width; x += 4)
			{
				*ptr++ = ((buffer[base] & 0x30) >> 4) | ((buffer[base] & 0xC0) >> 2);
				*ptr++ =  (buffer[base] & 0x03)       | ((buffer[base] & 0x0C) << 2);
				base++;
			}
		}
	}
}

bool DMG_DecompressCGA (const uint8_t* data, uint16_t dataLength, uint8_t* buffer, int width, int height)
{
	uint8_t  color;
	uint8_t  repetitions;
	uint8_t  rleColorCount = *data;

	const uint8_t* rleColors = data+1;
	const uint8_t* dataEnd = data + dataLength;

	uint8_t* tempBuffer = Allocate<uint8_t>("CGA decompression buffer", width * height / 4);
	uint8_t* ptr = tempBuffer;
	uint8_t* end = tempBuffer + width * height / 4;

	if (dataLength < 6 || rleColorCount > 4)
	{
		DMG_SetError(DMG_ERROR_CORRUPTED_DATA_STREAM);
		return false;
	}
	data += 5;

	while (ptr < end && data < dataEnd)
	{
		color = *data++;
		*ptr++ = color;
		if (ptr == end) break;
		if (data == dataEnd) break;
		for (int n = 0; n < rleColorCount; n++)
		{
			if (color == rleColors[n])
			{
				repetitions = *data++;
				if (repetitions > 0)
				{
					repetitions--;
					while (repetitions-- > 0 && ptr < end)
						*ptr++ = color;
				}
				break;
			}
		}
	}

	ProcessCGABuffer(tempBuffer, width, height, buffer);
	Free(tempBuffer);
	return true;
}