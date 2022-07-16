#include "structpack.h"
#include <algorithm>
#include <stdexcept>

static int Unpack(const char* format, char* buffer)
{
	int totalBytes = 0;
	int repeat = 0;

	for (const char* c2 = format; *c2; c2++)
	{
		char c = *c2;
		int fieldLength = -1;

		switch (c)
		{
		case '>': // big endian indicator (for compat with python's struct) - OK just ignore
			continue;

		case ' ':
		case '\r':
		case '\n':
		case '\t':
			continue;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (repeat) repeat *= 10;
			repeat += c - '0';
			continue;

		case 'x': // pad byte
		case 'c': // char
		case 'b': // signed char
		case 'B': // unsigned char
		case '?': // bool
			fieldLength = 1;
			break;

		case 'h': // short
		case 'H': // unsigned short
			fieldLength = 2;
			break;

		case 'i': // int
		case 'I': // unsigned int
		case 'l': // long
		case 'L': // unsigned long
		case 'f': // float
			fieldLength = 4;
			break;

		case 'q': // long long
		case 'Q': // unsigned long long
		case 'd': // double
			fieldLength = 8;
			break;

		default:
			throw std::invalid_argument("unknown format char in structpack format");
		}

		if (totalBytes % fieldLength != 0)
		{
			throw std::invalid_argument("illegal word alignment in structpack format");
		}

		if (!repeat)
			repeat = 1;

		bool doSwap = fieldLength > 1;

		if (buffer)
		{
			if (doSwap)
			{
				for (int i = 0; i < repeat; i++)
				{
					std::reverse(buffer, buffer + fieldLength);
					buffer += fieldLength;
				}
			}
			else
			{
				buffer += repeat * fieldLength;
			}
		}

		totalBytes += repeat * fieldLength;
		repeat = 0;
	}

	return totalBytes;
}

int ByteswapStructs(const char* format, int structSize, int structCount, void* buffer)
{
	char* byteBuffer = (char*) buffer;
	int totalBytes = 0;
	for (int i = 0; i < structCount; i++)
	{
		int newSize = Unpack(format, byteBuffer);
		byteBuffer += newSize;
		totalBytes += newSize;
	}
	if (totalBytes != structSize * structCount)
	{
		throw std::invalid_argument("unexpected length after byteswap");
	}
	return totalBytes;
}

int ByteswapInts(int intSize, int intCount, void* buffer)
{
	char* byteBuffer = (char*) buffer;
	for (int i = 0; i < intCount; i++)
	{
		std::reverse(byteBuffer, byteBuffer + intSize);
		byteBuffer += intSize;
	}
	return intCount * intSize;
}

// Convert 1555 to 5551 AND BGRA to RGBA (single pixel version)
uint16_t ConvertSingle1555To5551(uint16_t pixel)
{
    // https://stackoverflow.com/questions/70962623/convert-pixels-buffer-type-from-1555-to-5551-c-opengl-es
    // extract bgra from 1555 (1 bit alpha, 5 bits red, 5 bits green, 5 bits blue)
    uint16_t a = pixel >> 15;
    uint16_t r = (pixel >> 10) & 0x1f; // mask lowest five bits
    uint16_t g = (pixel >> 5) & 0x1f;
    uint16_t b = pixel & 0x1f;

    // compress rgba into 5551 (5 bits blue, 5 bits green, 5 bits red, 1 bit alpha)
    pixel = (r << 11) | (g << 6) | (b << 1) | a;
    return pixel;
}

// Convert 1555 to 5551 AND BGRA to RGBA
void Convert1555To5551(int intCount, void* buffer)
{
    uint16_t* byteBuffer = (uint16_t*) buffer;

    for (int i = 0; i < intCount; i++)
    {
        // https://stackoverflow.com/questions/70962623/convert-pixels-buffer-type-from-1555-to-5551-c-opengl-es
        // extract bgra from 1555 (1 bit alpha, 5 bits red, 5 bits green, 5 bits blue)
        uint16_t pixel = *byteBuffer;
        uint16_t a = pixel >> 15;
        uint16_t r = (pixel >> 10) & 0x1f; // mask lowest five bits
        uint16_t g = (pixel >> 5) & 0x1f;
        uint16_t b = pixel & 0x1f;

        // compress rgba into 5551 (5 bits blue, 5 bits green, 5 bits red, 1 bit alpha)
        pixel = (r << 11) | (g << 6) | (b << 1) | a;
        *byteBuffer = pixel;
        byteBuffer += 1;
    }
}

// Convert 1555 to 5551 AND BGRA to RGBA AND fix transparency
// Basically, if a pixel is transparent, we can't have r==g==b==0 or
// else the rendering gets messed up on vita. There's probably a
// smarter way to solve the issue, but we just set r=1 when a=0.
void Convert1555To5551FixAlpha(int intCount, void* buffer)
{
    uint16_t* byteBuffer = (uint16_t*) buffer;

    for (int i = 0; i < intCount; i++)
    {
        // https://stackoverflow.com/questions/70962623/convert-pixels-buffer-type-from-1555-to-5551-c-opengl-es
        // extract bgra from 1555 (1 bit alpha, 5 bits red, 5 bits green, 5 bits blue)
        uint16_t pixel = *byteBuffer;
        uint16_t a = pixel >> 15;
        uint16_t r = (pixel >> 10) & 0x1f; // mask lowest five bits
        uint16_t g = (pixel >> 5) & 0x1f;
        uint16_t b = pixel & 0x1f;

        if (a == 0) {
            // anything is fine here, we just don't allow r==g==b==0
            r = 1;
        }

        // compress rgba into 5551 (5 bits blue, 5 bits green, 5 bits red, 1 bit alpha)
        pixel = (r << 11) | (g << 6) | (b << 1) | a;
        *byteBuffer = pixel;
        byteBuffer += 1;
    }
}

// Convert 8888 Rev to 8888 AND BGRA to RGBA
// TODO: work around vitaGL transparency issue here too?
// I haven't seen it arise yet, so let's hold off.
void Convert8888RevTo8888(int intCount, void* buffer)
{
    uint32_t* byteBuffer = (uint32_t*) buffer;

    for (int i = 0; i < intCount; i++)
    {
        uint32_t pixel = *byteBuffer;
        uint32_t a = pixel >> 24;
        uint32_t r = (pixel >> 16) & 0xff; // mask lowest eight bits
        uint32_t g = (pixel >> 8) & 0xff;
        uint32_t b = pixel & 0xff;

        pixel = (r << 24) | (g << 16) | (b << 8) | a;
        *byteBuffer = pixel;
        byteBuffer += 1;
    }
}
