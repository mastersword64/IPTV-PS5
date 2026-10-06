#include "qr.h"

#include <cstdlib>
#include <cstring>

// A compact QR encoder: byte mode, error correction level M, versions 1 to 9.

namespace
{
	struct Layout { int total, ecPerBlock, blocks1, data1, blocks2, data2; };
	// per version (1..9), level M: all code words, check words per block, then the blocks and how much data each holds
	const Layout kLayout[10] = {
		{ 0, 0, 0, 0, 0, 0 },
		{ 26, 10, 1, 16, 0, 0 }, { 44, 16, 1, 28, 0, 0 }, { 70, 26, 1, 44, 0, 0 },
		{ 100, 18, 2, 32, 0, 0 }, { 134, 24, 2, 43, 0, 0 }, { 172, 16, 4, 27, 0, 0 },
		{ 196, 18, 4, 31, 0, 0 }, { 242, 22, 2, 38, 2, 39 }, { 292, 22, 3, 36, 2, 37 } };

	uint8_t multiply(uint8_t a, uint8_t b)
	{
		// multiplication in the field the codes use (polynomial 0x11D)
		int result = 0;
		for (int i = 7; i >= 0; i--)
		{
			result = (result << 1) ^ ((result >> 7) * 0x11D);
			result ^= ((b >> i) & 1) * a;
		}
		return (uint8_t)result;
	}

	std::vector<uint8_t> checkWords(const std::vector<uint8_t>& data, int degree)
	{
		// the generator polynomial, then the remainder of the data divided by it
		std::vector<uint8_t> generator((size_t)degree, 0);
		generator[(size_t)degree - 1] = 1;
		uint8_t root = 1;
		for (int i = 0; i < degree; i++)
		{
			for (int j = 0; j < degree; j++)
			{
				generator[(size_t)j] = multiply(generator[(size_t)j], root);
				if (j + 1 < degree)
					generator[(size_t)j] ^= generator[(size_t)j + 1];
			}
			root = multiply(root, 2);
		}
		std::vector<uint8_t> remainder((size_t)degree, 0);
		for (const uint8_t value : data)
		{
			const uint8_t factor = value ^ remainder[0];
			remainder.erase(remainder.begin());
			remainder.push_back(0);
			for (int j = 0; j < degree; j++)
				remainder[(size_t)j] ^= multiply(generator[(size_t)j], factor);
		}
		return remainder;
	}

	struct Grid
	{
		int size;
		std::vector<uint8_t> dark, fixed;       // fixed: part of the pattern every code has, not data
		explicit Grid(int s) : size(s), dark((size_t)s * s, 0), fixed((size_t)s * s, 0) {}
		void set(int x, int y, bool on)
		{
			dark[(size_t)y * size + x] = on ? 1 : 0;
			fixed[(size_t)y * size + x] = 1;
		}
		bool at(int x, int y) const { return dark[(size_t)y * size + x] != 0; }
	};

	void finder(Grid& g, int cx, int cy)
	{
		for (int dy = -4; dy <= 4; dy++)
			for (int dx = -4; dx <= 4; dx++)
			{
				const int x = cx + dx, y = cy + dy;
				if (x < 0 || y < 0 || x >= g.size || y >= g.size)
					continue;
				const int distance = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
				g.set(x, y, distance != 2 && distance != 4);
			}
	}

	void formatBits(Grid& g, int mask)
	{
		// level M is written as 0; then five bits of check, and a fixed pattern over the top
		const int data = (0 << 3) | mask;
		int rem = data;
		for (int i = 0; i < 10; i++)
			rem = (rem << 1) ^ ((rem >> 9) * 0x537);
		const int bits = ((data << 10) | rem) ^ 0x5412;
		const auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
		for (int i = 0; i <= 5; i++) g.set(8, i, bit(i));
		g.set(8, 7, bit(6));
		g.set(8, 8, bit(7));
		g.set(7, 8, bit(8));
		for (int i = 9; i < 15; i++) g.set(14 - i, 8, bit(i));
		for (int i = 0; i < 8; i++) g.set(g.size - 1 - i, 8, bit(i));
		for (int i = 8; i < 15; i++) g.set(8, g.size - 15 + i, bit(i));
		g.set(8, g.size - 8, true);
	}

	bool masked(int mask, int x, int y)
	{
		switch (mask)
		{
		case 0: return (x + y) % 2 == 0;
		case 1: return y % 2 == 0;
		case 2: return x % 3 == 0;
		case 3: return (x + y) % 3 == 0;
		case 4: return (x / 3 + y / 2) % 2 == 0;
		case 5: return x * y % 2 + x * y % 3 == 0;
		case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
		default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
		}
	}

	// How hard a pattern is to read: long runs, blocks, shapes like the corner marks, and an uneven balance all count against it.
	long penalty(const Grid& g)
	{
		long total = 0;
		const int n = g.size;
		for (int pass = 0; pass < 2; pass++)
			for (int a = 0; a < n; a++)
			{
				int run = 0;
				bool colour = false;
				unsigned history = 0;
				for (int b = 0; b < n; b++)
				{
					const bool here = pass == 0 ? g.at(b, a) : g.at(a, b);
					if (b > 0 && here == colour)
					{
						run++;
						if (run == 5) total += 3;
						else if (run > 5) total++;
					}
					else
					{
						colour = here;
						run = 1;
					}
					history = ((history << 1) | (here ? 1u : 0u)) & 0x7ff;
					if (b >= 10 && (history == 0x5d0 || history == 0x05d))     // 10111010000 or 00001011101
						total += 40;
				}
			}
		for (int y = 0; y + 1 < n; y++)
			for (int x = 0; x + 1 < n; x++)
			{
				const bool c = g.at(x, y);
				if (c == g.at(x + 1, y) && c == g.at(x, y + 1) && c == g.at(x + 1, y + 1))
					total += 3;
			}
		long darkCount = 0;
		for (const uint8_t value : g.dark)
			darkCount += value;
		const long cells = (long)n * n;
		long off = darkCount * 20 - cells * 10;
		if (off < 0) off = -off;
		total += (off + cells - 1) / cells == 0 ? 0 : ((off + cells - 1) / cells - 1) * 10;
		return total;
	}
}

bool qrEncode(const std::string& text, std::vector<uint8_t>& modules, int& size)
{
	// the smallest version that holds the text
	int version = 0;
	for (int v = 1; v <= 9 && !version; v++)
	{
		const Layout& l = kLayout[v];
		const int capacity = l.blocks1 * l.data1 + l.blocks2 * l.data2;
		if ((int)text.size() + 2 <= capacity)        // (the 12 bits of heading round up to 2 bytes)
			version = v;
	}
	if (!version)
		return false;
	const Layout& layout = kLayout[version];
	const int dataWords = layout.blocks1 * layout.data1 + layout.blocks2 * layout.data2;

	// the message as bits: "bytes follow", how many, the bytes, then padding
	std::vector<uint8_t> data;
	int bitCount = 0;
	const auto put = [&](unsigned value, int count) {
		for (int i = count - 1; i >= 0; i--, bitCount++)
		{
			if (bitCount % 8 == 0)
				data.push_back(0);
			data.back() |= (uint8_t)(((value >> i) & 1) << (7 - bitCount % 8));
		}
	};
	put(4, 4);
	put((unsigned)text.size(), 8);
	for (const char c : text)
		put((unsigned char)c, 8);
	for (int i = 0; i < 4 && bitCount < dataWords * 8; i++)
		put(0, 1);
	while (bitCount % 8)
		put(0, 1);
	for (unsigned pad = 0xEC; (int)data.size() < dataWords; pad ^= 0xEC ^ 0x11)
		data.push_back((uint8_t)pad);

	// split into blocks, add each block's check words, and deal them out in turn
	std::vector<std::vector<uint8_t>> blocks, checks;
	size_t at = 0;
	for (int b = 0; b < layout.blocks1 + layout.blocks2; b++)
	{
		const int length = b < layout.blocks1 ? layout.data1 : layout.data2;
		blocks.emplace_back(data.begin() + (long)at, data.begin() + (long)at + length);
		checks.push_back(checkWords(blocks.back(), layout.ecPerBlock));
		at += (size_t)length;
	}
	std::vector<uint8_t> all;
	for (int i = 0; i < layout.data1 + 1; i++)
		for (const auto& block : blocks)
			if (i < (int)block.size())
				all.push_back(block[(size_t)i]);
	for (int i = 0; i < layout.ecPerBlock; i++)
		for (const auto& check : checks)
			all.push_back(check[(size_t)i]);
	if ((int)all.size() != layout.total)
		return false;

	// the fixed parts: corner marks, timing lines, alignment marks, version
	size = 17 + 4 * version;
	Grid g(size);
	for (int i = 0; i < size; i++)
	{
		g.set(6, i, i % 2 == 0);
		g.set(i, 6, i % 2 == 0);
	}
	finder(g, 3, 3);
	finder(g, size - 4, 3);
	finder(g, 3, size - 4);
	if (version >= 2)
	{
		const int count = version / 7 + 2;
		const int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
		std::vector<int> places;
		for (int pos = size - 7; (int)places.size() < count - 1; pos -= step)
			places.insert(places.begin(), pos);
		places.insert(places.begin(), 6);
		for (int i = 0; i < count; i++)
			for (int j = 0; j < count; j++)
			{
				if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0))
					continue;                              // where the corner marks are
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
						g.set(places[(size_t)i] + dx, places[(size_t)j] + dy, (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) != 1);
			}
	}
	formatBits(g, 0);                                   // reserves the places; the real bits come once the mask is chosen
	if (version >= 7)
	{
		int rem = version;
		for (int i = 0; i < 12; i++)
			rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
		const long bits = ((long)version << 12) | rem;
		for (int i = 0; i < 18; i++)
		{
			const bool bit = ((bits >> i) & 1) != 0;
			const int a = size - 11 + i % 3, b = i / 3;
			g.set(a, b, bit);
			g.set(b, a, bit);
		}
	}

	// the data, zig-zagging up and down in pairs of columns from the bottom right
	size_t bitIndex = 0;
	for (int right = size - 1; right >= 1; right -= 2)
	{
		if (right == 6)
			right = 5;
		for (int vertical = 0; vertical < size; vertical++)
			for (int j = 0; j < 2; j++)
			{
				const int x = right - j;
				const bool upward = ((right + 1) & 2) == 0;
				const int y = upward ? size - 1 - vertical : vertical;
				if (g.fixed[(size_t)y * size + x] || bitIndex >= all.size() * 8)
					continue;
				g.dark[(size_t)y * size + x] = (all[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1;
				bitIndex++;
			}
	}

	// of the eight ways of flipping squares to break up patterns, keep the most readable
	long best = -1;
	int bestMask = 0;
	for (int mask = 0; mask < 8; mask++)
	{
		Grid trial = g;
		for (int y = 0; y < size; y++)
			for (int x = 0; x < size; x++)
				if (!trial.fixed[(size_t)y * size + x] && masked(mask, x, y))
					trial.dark[(size_t)y * size + x] ^= 1;
		formatBits(trial, mask);
		const long score = penalty(trial);
		if (best < 0 || score < best)
		{
			best = score;
			bestMask = mask;
		}
	}
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			if (!g.fixed[(size_t)y * size + x] && masked(bestMask, x, y))
				g.dark[(size_t)y * size + x] ^= 1;
	formatBits(g, bestMask);
	modules = g.dark;
	return true;
}
