#include <dmg.h>
#include <os_mem.h>

bool DMG_UncEGAToPacked(const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output, int outputSize)
{
	uint8_t* tempBuffer = 0;
	if (input >= output && input <= output + outputSize)
	{
		tempBuffer = Allocate<uint8_t>("Temporary EGA buffer", width*height/2);
		MemCopy(tempBuffer, input, width*height/2);
		input = tempBuffer;
	}

	const uint8_t* end = input + width*height/2;
	uint8_t* outputEnd = output + outputSize;
	int bitPlaneStride = (width*height)/8;

	while (height > 0 && input < end && output < outputEnd) {
		for (int x = 0; x < width; x += 2)
		{
			int offset = x >> 3;
			int mask = 0x80 >> (x & 7);
			uint8_t color0 = 
				((input[offset                   ] & mask) != 0 ? 0x01 : 0) |
				((input[offset + bitPlaneStride  ] & mask) != 0 ? 0x02 : 0) |
				((input[offset + bitPlaneStride*2] & mask) != 0 ? 0x04 : 0) |
				((input[offset + bitPlaneStride*3] & mask) != 0 ? 0x08 : 0);
			mask >>= 1;
			uint8_t color1 = 
				((input[offset                   ] & mask) != 0 ? 0x01 : 0) |
				((input[offset + bitPlaneStride  ] & mask) != 0 ? 0x02 : 0) |
				((input[offset + bitPlaneStride*2] & mask) != 0 ? 0x04 : 0) |
				((input[offset + bitPlaneStride*3] & mask) != 0 ? 0x08 : 0);
			*output++ = color1 | (color0 << 4);
		}
		input += width/8;
		height--;
	}
	
	if (tempBuffer != 0)
		Free(tempBuffer);
	return true;// input == end && output == outputEnd;
}

static void ProcessEGABuffer (const uint8_t* buffer, int width, int height, uint8_t* output)
{
	int x, y;
	int bitPlaneStride = (width*height)/8;
	int rowStride = width/8;

	for (y = height-1; y >= 0; y--)
	{
		int base = y*rowStride;
		uint8_t outc = 0;
		bool outcm = false;

		if (y & 1)
		{
			uint8_t* ptr = output + (y * width)/2;
			for (x = width-1; x >= 0; x--)
			{
				int offset = base + (x >> 3);
				int mask = 0x1 << (x & 7);
				uint8_t color = 
					((buffer[offset                   ] & mask) != 0 ? 0x01 : 0) |
					((buffer[offset + bitPlaneStride  ] & mask) != 0 ? 0x02 : 0) |
					((buffer[offset + bitPlaneStride*2] & mask) != 0 ? 0x04 : 0) |
					((buffer[offset + bitPlaneStride*3] & mask) != 0 ? 0x08 : 0);

				if (outcm)
					*ptr++ = outc | color;
				else
					outc = color << 4;
				outcm = !outcm;
			}
		}
		else
		{
			uint8_t* ptr = output + (y * width + width)/2 - 1;
			for (x = width-1; x >= 0; x--)
			{
				int offset = base + (x >> 3);
				int mask = 0x80 >> (x & 7);
				uint8_t color = 
					((buffer[offset                   ] & mask) != 0 ? 0x01 : 0) |
					((buffer[offset + bitPlaneStride  ] & mask) != 0 ? 0x02 : 0) |
					((buffer[offset + bitPlaneStride*2] & mask) != 0 ? 0x04 : 0) |
					((buffer[offset + bitPlaneStride*3] & mask) != 0 ? 0x08 : 0);
				
				if (outcm)
					*ptr-- = outc | (color << 4);
				else
					outc = color;
				outcm = !outcm;
			}
		}
	}
}

bool DMG_DecompressEGA (const uint8_t* data, uint16_t dataLength, uint8_t* buffer, int width, int height)
{
	uint8_t  color;
	uint8_t  repetitions;
	uint8_t  rleColorCount = *data;

	const uint8_t* rleColors = data+1;
	const uint8_t* dataEnd = data + dataLength;

	uint8_t* tempBuffer = Allocate<uint8_t>("EGA decompression buffer", width * height / 2);
	uint8_t* ptr = tempBuffer;
	uint8_t* end = tempBuffer + width * height / 2;

	if (dataLength < 6 || rleColorCount > 4)
	{
		DMG_Warning("Compressed data is invalid");
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

	ProcessEGABuffer(tempBuffer, width, height, buffer);
	Free(tempBuffer);
	return true;
}