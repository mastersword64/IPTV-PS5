#include "cursors.h"
#include "images.h"

#include <cstdio>
#include <cstring>

namespace
{
	uint32_t u16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
	uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

	bool readWhole(const std::string& path, std::vector<uint8_t>& out)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		std::vector<uint8_t> block(65536);
		size_t n;
		while ((n = fread(block.data(), 1, block.size(), f)) > 0 && out.size() < 32u * 1024 * 1024)
			out.insert(out.end(), block.begin(), block.begin() + (long)n);
		fclose(f);
		return !out.empty();
	}

	// One picture inside a cursor file: a Windows bitmap without its file header, twice as tall as
	// the picture because a one-bit "see-through" mask follows the colours.
	bool decodeBitmap(const uint8_t* data, size_t size, int width, int height, CursorFrame& out)
	{
		if (size < 40)
			return false;
		const uint32_t headerSize = u32(data);
		const int w = (int)u32(data + 4), h2 = (int)u32(data + 8);
		const int bits = (int)u16(data + 14);
		if (headerSize < 40 || headerSize > size || u32(data + 16) != 0)   // compressed bitmaps are not used in cursors
			return false;
		const int h = h2 / 2;
		if (w <= 0 || h <= 0 || w > 512 || h > 512 || (width > 0 && w != width && width != 256))
			return false;
		if (bits != 32 && bits != 24 && bits != 8 && bits != 4 && bits != 1)
			return false;

		size_t paletteCount = 0;
		if (bits <= 8)
		{
			paletteCount = u32(data + 32);
			if (paletteCount == 0 || paletteCount > (1u << bits))
				paletteCount = 1u << bits;
		}
		const uint8_t* palette = data + headerSize;
		const size_t colourStride = (((size_t)w * (size_t)bits + 31) / 32) * 4;
		const size_t maskStride = (((size_t)w + 31) / 32) * 4;
		const size_t colourStart = headerSize + paletteCount * 4;
		const size_t maskStart = colourStart + colourStride * (size_t)h;
		if (maskStart > size)
			return false;
		const bool haveMask = maskStart + maskStride * (size_t)h <= size;

		out.width = w;
		out.height = h;
		out.rgba.assign((size_t)w * (size_t)h * 4, 0);
		bool anyAlpha = false;
		for (int y = 0; y < h; y++)
		{
			// rows are stored bottom to top
			const uint8_t* row = data + colourStart + colourStride * (size_t)(h - 1 - y);
			const uint8_t* maskRow = haveMask ? data + maskStart + maskStride * (size_t)(h - 1 - y) : nullptr;
			for (int x = 0; x < w; x++)
			{
				uint8_t* pixel = &out.rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
				const bool masked = maskRow && (maskRow[x / 8] & (0x80 >> (x % 8))) != 0;
				if (bits == 32)
				{
					pixel[0] = row[x * 4 + 2];
					pixel[1] = row[x * 4 + 1];
					pixel[2] = row[x * 4];
					pixel[3] = row[x * 4 + 3];
					if (pixel[3])
						anyAlpha = true;
				}
				else if (bits == 24)
				{
					pixel[0] = row[x * 3 + 2];
					pixel[1] = row[x * 3 + 1];
					pixel[2] = row[x * 3];
					pixel[3] = masked ? 0 : 255;
				}
				else
				{
					size_t index;
					if (bits == 8)
						index = row[x];
					else if (bits == 4)
						index = (row[x / 2] >> (x % 2 ? 0 : 4)) & 0x0f;
					else
						index = (row[x / 8] >> (7 - x % 8)) & 0x01;
					if (index >= paletteCount)
						index = 0;
					pixel[0] = palette[index * 4 + 2];
					pixel[1] = palette[index * 4 + 1];
					pixel[2] = palette[index * 4];
					pixel[3] = masked ? 0 : 255;
				}
			}
		}
		if (bits == 32 && !anyAlpha)
		{
			// an older 32-bit cursor that leaves transparency to the mask
			for (int y = 0; y < h; y++)
			{
				const uint8_t* maskRow = haveMask ? data + maskStart + maskStride * (size_t)(h - 1 - y) : nullptr;
				for (int x = 0; x < w; x++)
					out.rgba[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] = maskRow && (maskRow[x / 8] & (0x80 >> (x % 8))) ? 0 : 255;
			}
		}
		return true;
	}

	// A .cur / .ico file (or one frame of a .ani): picks the largest picture in it.
	bool decodeCursor(const uint8_t* data, size_t size, CursorFrame& out)
	{
		if (size < 22 || u16(data) != 0 || (u16(data + 2) != 1 && u16(data + 2) != 2))
			return false;
		const bool cursor = u16(data + 2) == 2;
		const size_t count = u16(data + 4);
		if (count == 0 || 6 + count * 16 > size)
			return false;
		size_t best = 0;
		int bestArea = -1;
		for (size_t i = 0; i < count; i++)
		{
			const uint8_t* entry = data + 6 + i * 16;
			const int w = entry[0] ? entry[0] : 256, h = entry[1] ? entry[1] : 256;
			if (w * h > bestArea)
			{
				bestArea = w * h;
				best = i;
			}
		}
		const uint8_t* entry = data + 6 + best * 16;
		const size_t length = u32(entry + 8), offset = u32(entry + 12);
		if (offset >= size || length > size - offset || length < 8)
			return false;
		const uint8_t* picture = data + offset;
		bool ok;
		if (memcmp(picture, "\x89PNG", 4) == 0)
			ok = loadImageMemory(picture, length, out.rgba, out.width, out.height);
		else
			ok = decodeBitmap(picture, length, entry[0] ? entry[0] : 256, entry[1] ? entry[1] : 256, out);
		if (!ok)
			return false;
		out.hotX = cursor ? (int)u16(entry + 4) : 0;
		out.hotY = cursor ? (int)u16(entry + 6) : 0;
		if (out.hotX >= out.width) out.hotX = 0;
		if (out.hotY >= out.height) out.hotY = 0;
		return true;
	}

	// An animated cursor is a container (RIFF "ACON") holding a cursor per frame and a frame rate.
	void walkAnimation(const uint8_t* data, size_t start, size_t end, std::vector<CursorFrame>& frames, uint32_t& jiffies, int depth)
	{
		size_t pos = start;
		while (pos + 8 <= end && frames.size() < 64)
		{
			const uint8_t* chunk = data + pos;
			const size_t length = u32(chunk + 4), body = pos + 8;
			if (length > end - body)
				break;
			if (memcmp(chunk, "LIST", 4) == 0 && length >= 4 && depth < 4)
				walkAnimation(data, body + 4, body + length, frames, jiffies, depth + 1);
			else if (memcmp(chunk, "icon", 4) == 0)
			{
				CursorFrame frame;
				if (decodeCursor(data + body, length, frame))
					frames.push_back(std::move(frame));
			}
			else if (memcmp(chunk, "anih", 4) == 0 && length >= 36)
				jiffies = u32(data + body + 28);
			pos = body + length + (length & 1);
		}
	}
}

bool loadCursorFile(const std::string& path, std::vector<CursorFrame>& frames, double& frameSeconds)
{
	frames.clear();
	frameSeconds = 0.1;
	std::vector<uint8_t> data;
	if (!readWhole(path, data))
		return false;

	if (data.size() >= 12 && memcmp(data.data(), "RIFF", 4) == 0 && memcmp(data.data() + 8, "ACON", 4) == 0)
	{
		uint32_t jiffies = 6;                    // sixtieths of a second per frame
		walkAnimation(data.data(), 12, data.size(), frames, jiffies, 0);
		if (jiffies == 0 || jiffies > 600)
			jiffies = 6;
		frameSeconds = (double)jiffies / 60.0;
		// every frame must be the same size to be shown in turn
		for (size_t i = 1; i < frames.size(); i++)
			if (frames[i].width != frames[0].width || frames[i].height != frames[0].height)
			{
				frames.resize(1);
				break;
			}
		return !frames.empty();
	}

	CursorFrame frame;
	if (data.size() >= 6 && u16(data.data()) == 0 && (u16(data.data() + 2) == 1 || u16(data.data() + 2) == 2))
	{
		if (!decodeCursor(data.data(), data.size(), frame))
			return false;
	}
	else if (!loadImageMemory(data.data(), data.size(), frame.rgba, frame.width, frame.height))
		return false;
	frames.push_back(std::move(frame));
	return true;
}
