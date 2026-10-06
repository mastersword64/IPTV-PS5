#include "xmltv.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- unpacking gzip ("inflate") ------------------------------------------------------------
// A small, plain decoder for the DEFLATE format: slower than a tuned library, but it needs nothing
// the console may not have.

namespace
{
	struct Bits
	{
		const unsigned char* in;
		size_t size, pos = 0;
		unsigned hold = 0;
		int count = 0;
		bool bad = false;
		int take(int need)
		{
			while (count < need)
			{
				if (pos >= size) { bad = true; return 0; }
				hold |= (unsigned)in[pos++] << count;
				count += 8;
			}
			const int value = (int)(hold & ((1u << need) - 1));
			hold >>= need;
			count -= need;
			return value;
		}
	};

	struct Huffman { short count[16]; short symbol[288]; };

	void build(Huffman& h, const short* lengths, int n)
	{
		memset(h.count, 0, sizeof(h.count));
		for (int i = 0; i < n; i++)
			h.count[lengths[i]]++;
		h.count[0] = 0;
		short offset[16];
		offset[1] = 0;
		for (int len = 1; len < 15; len++)
			offset[len + 1] = (short)(offset[len] + h.count[len]);
		for (int i = 0; i < n; i++)
			if (lengths[i])
				h.symbol[offset[lengths[i]]++] = (short)i;
	}

	int decode(Bits& b, const Huffman& h)
	{
		int code = 0, first = 0, index = 0;
		for (int len = 1; len <= 15; len++)
		{
			code |= b.take(1);
			if (b.bad)
				return -1;
			const int count = h.count[len];
			if (code - count < first)
				return h.symbol[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		return -1;
	}

	bool inflateData(const unsigned char* in, size_t size, std::string& out, size_t limit)
	{
		static const short lengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
		static const short lengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
		static const short distBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
		static const short distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
		Bits b{ in, size };
		bool last = false;
		while (!last)
		{
			last = b.take(1) != 0;
			const int type = b.take(2);
			if (b.bad)
				return false;
			if (type == 0)
			{
				// stored as it is
				b.hold = 0;
				b.count = 0;
				if (b.pos + 4 > size)
					return false;
				const unsigned length = in[b.pos] | (in[b.pos + 1] << 8);
				b.pos += 4;
				if (b.pos + length > size || out.size() + length > limit)
					return false;
				out.append((const char*)in + b.pos, length);
				b.pos += length;
				continue;
			}
			if (type == 3)
				return false;
			Huffman lengthCode, distCode;
			short lengths[320];
			if (type == 1)
			{
				for (int i = 0; i < 144; i++) lengths[i] = 8;
				for (int i = 144; i < 256; i++) lengths[i] = 9;
				for (int i = 256; i < 280; i++) lengths[i] = 7;
				for (int i = 280; i < 288; i++) lengths[i] = 8;
				build(lengthCode, lengths, 288);
				for (int i = 0; i < 30; i++) lengths[i] = 5;
				build(distCode, lengths, 30);
			}
			else
			{
				static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
				const int nlen = b.take(5) + 257, ndist = b.take(5) + 1, ncode = b.take(4) + 4;
				if (b.bad || nlen > 286 || ndist > 30)
					return false;
				for (int i = 0; i < 19; i++)
					lengths[order[i]] = (short)(i < ncode ? b.take(3) : 0);
				Huffman codeCode;
				build(codeCode, lengths, 19);
				int index = 0;
				while (index < nlen + ndist)
				{
					const int symbol = decode(b, codeCode);
					if (symbol < 0)
						return false;
					if (symbol < 16)
						lengths[index++] = (short)symbol;
					else
					{
						int repeat, value = 0;
						if (symbol == 16)
						{
							if (index == 0)
								return false;
							value = lengths[index - 1];
							repeat = 3 + b.take(2);
						}
						else if (symbol == 17)
							repeat = 3 + b.take(3);
						else
							repeat = 11 + b.take(7);
						if (b.bad || index + repeat > nlen + ndist)
							return false;
						while (repeat--)
							lengths[index++] = (short)value;
					}
				}
				build(lengthCode, lengths, nlen);
				build(distCode, lengths + nlen, ndist);
			}
			for (;;)
			{
				const int symbol = decode(b, lengthCode);
				if (symbol < 0)
					return false;
				if (symbol < 256)
				{
					if (out.size() >= limit)
						return false;
					out.push_back((char)symbol);
				}
				else if (symbol == 256)
					break;
				else
				{
					const int which = symbol - 257;
					if (which >= 29)
						return false;
					const int length = lengthBase[which] + b.take(lengthExtra[which]);
					const int distSymbol = decode(b, distCode);
					if (distSymbol < 0 || distSymbol >= 30)
						return false;
					const size_t distance = (size_t)((unsigned short)distBase[distSymbol]) + (size_t)b.take(distExtra[distSymbol]);
					if (b.bad || distance > out.size() || out.size() + (size_t)length > limit)
						return false;
					const size_t from = out.size() - distance;
					for (int i = 0; i < length; i++)
						out.push_back(out[from + (size_t)i]);
				}
			}
		}
		return true;
	}
}

bool gunzip(const std::string& packed, std::string& out, size_t limit)
{
	out.clear();
	const unsigned char* in = (const unsigned char*)packed.data();
	const size_t size = packed.size();
	if (size < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8)
		return false;
	const int flags = in[3];
	size_t pos = 10;
	if (flags & 4)
	{
		if (pos + 2 > size) return false;
		pos += 2 + (size_t)(in[pos] | (in[pos + 1] << 8));
	}
	if (flags & 8) { while (pos < size && in[pos]) pos++; pos++; }
	if (flags & 16) { while (pos < size && in[pos]) pos++; pos++; }
	if (flags & 2) pos += 2;
	if (pos >= size)
		return false;
	out.reserve(size * 6 < limit ? size * 6 : limit);
	return inflateData(in + pos, size - pos, out, limit);
}

// ---- reading the guide -----------------------------------------------------------------------

namespace
{
	std::string lowered(std::string text)
	{
		for (char& c : text)
			if (c >= 'A' && c <= 'Z')
				c = (char)(c + 32);
		return text;
	}

	// &amp; and friends, as they appear in titles
	std::string plain(const std::string& text)
	{
		if (text.find('&') == std::string::npos)
			return text;
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '&')
			{
				if (text.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; continue; }
				if (text.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; continue; }
				if (text.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; continue; }
				if (text.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; continue; }
				if (text.compare(i, 6, "&apos;") == 0) { out += '\''; i += 5; continue; }
			}
			out += text[i];
		}
		return out;
	}

	// start="20261006093000 +0200" -> seconds since 1970
	long long stamp(const std::string& text)
	{
		if (text.size() < 12)
			return 0;
		const auto number = [&text](size_t at, size_t digits) { return atoi(text.substr(at, digits).c_str()); };
		int year = number(0, 4), month = number(4, 2);
		const int day = number(6, 2), hour = number(8, 2), minute = number(10, 2), second = text.size() >= 14 ? number(12, 2) : 0;
		// days since 1970 for a calendar date
		year -= month <= 2;
		const long long era = (year >= 0 ? year : year - 399) / 400;
		const unsigned yoe = (unsigned)(year - era * 400);
		const unsigned doy = (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		long long seconds = (era * 146097 + (long long)doe - 719468) * 86400 + hour * 3600 + minute * 60 + second;
		const size_t sign = text.find_first_of("+-", 12);
		if (sign != std::string::npos && sign + 4 < text.size())
		{
			const int offset = atoi(text.substr(sign + 1, 2).c_str()) * 3600 + atoi(text.substr(sign + 3, 2).c_str()) * 60;
			seconds += text[sign] == '+' ? -offset : offset;
		}
		return seconds;
	}

	// the value of name="..." inside a tag's text
	std::string attribute(const std::string& xml, size_t tagStart, size_t tagEnd, const char* name)
	{
		const std::string key = std::string(name) + "=\"";
		const size_t at = xml.find(key, tagStart);
		if (at == std::string::npos || at > tagEnd)
			return "";
		const size_t from = at + key.size(), to = xml.find('"', from);
		return to == std::string::npos || to > tagEnd ? "" : xml.substr(from, to - from);
	}

	// the words inside <name ...>...</name>, searched for between two places
	std::string element(const std::string& xml, size_t from, size_t to, const char* name)
	{
		const std::string open = std::string("<") + name;
		const size_t at = xml.find(open, from);
		if (at == std::string::npos || at >= to)
			return "";
		const size_t start = xml.find('>', at);
		const size_t end = xml.find(std::string("</") + name, start == std::string::npos ? at : start);
		if (start == std::string::npos || end == std::string::npos || end > to)
			return "";
		return plain(xml.substr(start + 1, end - start - 1));
	}

	std::string clock(long long seconds)
	{
		// hours and minutes of world time; the app shifts guide times to the viewer's clock itself
		char text[8];
		snprintf(text, sizeof(text), "%02d:%02d", (int)((seconds % 86400 + 86400) % 86400 / 3600), (int)((seconds % 3600 + 3600) % 3600 / 60));
		return text;
	}
}

bool parseXmltv(const std::string& xml, long long from, long long to, XmltvGuide& out)
{
	out.programmes.clear();
	out.idByName.clear();
	size_t pos = 0;
	long kept = 0;
	for (;;)
	{
		const size_t tag = xml.find('<', pos);
		if (tag == std::string::npos)
			break;
		if (xml.compare(tag, 9, "<channel ") == 0)
		{
			const size_t tagEnd = xml.find('>', tag), close = xml.find("</channel>", tag);
			if (tagEnd == std::string::npos || close == std::string::npos)
				break;
			const std::string id = lowered(attribute(xml, tag, tagEnd, "id"));
			const std::string name = lowered(element(xml, tagEnd, close, "display-name"));
			if (!id.empty() && !name.empty())
				out.idByName[name] = id;
			pos = close + 10;
		}
		else if (xml.compare(tag, 11, "<programme ") == 0)
		{
			const size_t tagEnd = xml.find('>', tag), close = xml.find("</programme>", tag);
			if (tagEnd == std::string::npos || close == std::string::npos)
				break;
			Programme programme;
			programme.start = stamp(attribute(xml, tag, tagEnd, "start"));
			programme.stop = stamp(attribute(xml, tag, tagEnd, "stop"));
			if (programme.start > 0 && programme.stop > from && programme.start < to)
			{
				const std::string id = lowered(attribute(xml, tag, tagEnd, "channel"));
				programme.title = element(xml, tagEnd, close, "title");
				programme.description = element(xml, tagEnd, close, "desc");
				programme.from = clock(programme.start);
				programme.to = clock(programme.stop);
				if (!id.empty() && !programme.title.empty())
				{
					out.programmes[id].push_back(std::move(programme));
					kept++;
				}
			}
			pos = close + 12;
		}
		else
			pos = tag + 1;
	}
	return kept > 0 || !out.idByName.empty();
}
