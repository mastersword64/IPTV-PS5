#include "json.h"
#include <cstdlib>

const std::string& JsonFields::get(const std::string& key) const
{
	static const std::string none;
	for (const auto& item : items)
		if (item.first == key)
			return item.second;
	return none;
}

bool JsonFields::has(const std::string& key) const
{
	for (const auto& item : items)
		if (item.first == key)
			return true;
	return false;
}

namespace
{
	struct Reader
	{
		const std::string& s;
		size_t i = 0;
		bool ok = true;
		const std::string& arrayPath;
		const std::function<void(const JsonFields&)>& each;
		int depth = 0;

		Reader(const std::string& text, const std::string& path, const std::function<void(const JsonFields&)>& fn) : s(text), arrayPath(path), each(fn) {}

		void space()
		{
			while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
				i++;
		}

		static void utf8(std::string& out, unsigned code)
		{
			if (code < 0x80) out += (char)code;
			else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
			else if (code < 0x10000) { out += (char)(0xE0 | (code >> 12)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
			else { out += (char)(0xF0 | (code >> 18)); out += (char)(0x80 | ((code >> 12) & 0x3F)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
		}

		unsigned hex4()
		{
			unsigned value = 0;
			for (int k = 0; k < 4 && i < s.size(); k++, i++)
			{
				const char c = s[i];
				value = value * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
			}
			return value;
		}

		std::string string()
		{
			std::string out;
			i++; // opening quote
			while (i < s.size() && s[i] != '"')
			{
				if (s[i] != '\\')
				{
					out += s[i++];
					continue;
				}
				if (++i >= s.size())
					break;
				const char c = s[i++];
				switch (c)
				{
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': case 'f': break;
				case 'u':
				{
					unsigned code = hex4();
					if (code >= 0xD800 && code < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u')
					{
						i += 2;
						code = 0x10000 + ((code - 0xD800) << 10) + (hex4() - 0xDC00);
					}
					utf8(out, code);
					break;
				}
				default: out += c; break;
				}
			}
			if (i >= s.size())
				ok = false;
			else
				i++; // closing quote
			return out;
		}

		// Reads one value. `fields` receives scalars (and lists of scalars, joined with commas) under `path`.
		void value(const std::string& path, JsonFields* fields)
		{
			if (!ok || ++depth > 64)
			{
				ok = false;
				return;
			}
			space();
			if (i >= s.size())
				ok = false;
			else if (s[i] == '{')
			{
				i++;
				space();
				if (i < s.size() && s[i] == '}')
					i++;
				else
					for (;;)
					{
						space();
						if (i >= s.size() || s[i] != '"') { ok = false; break; }
						const std::string key = string();
						space();
						if (!ok || i >= s.size() || s[i] != ':') { ok = false; break; }
						i++;
						value(path.empty() ? key : path + "." + key, fields);
						space();
						if (!ok || i >= s.size()) { ok = false; break; }
						if (s[i] == ',') { i++; continue; }
						if (s[i] == '}') { i++; break; }
						ok = false;
						break;
					}
			}
			else if (s[i] == '[')
			{
				// "episodes.*" matches a list under any key of "episodes" (the key is passed on as "@key")
				const bool wildcard = arrayPath.size() > 2 && arrayPath.compare(arrayPath.size() - 2, 2, ".*") == 0
					&& path.size() >= arrayPath.size() - 1 && path.compare(0, arrayPath.size() - 1, arrayPath, 0, arrayPath.size() - 1) == 0;
				const bool wanted = (path == arrayPath || wildcard) && depth <= 8;
				i++;
				space();
				if (i < s.size() && s[i] == ']')
					i++;
				else
					for (;;)
					{
						if (wanted)
						{
							JsonFields element;
							value("", &element);
							if (wildcard)
								element.items.emplace_back("@key", path.substr(arrayPath.size() - 1));
							if (ok)
								each(element);
						}
						else
						{
							// a list inside an object: scalars are joined, anything deeper is skipped
							JsonFields element;
							value("", &element);
							if (fields && element.items.size() == 1 && element.items[0].first.empty())
							{
								bool found = false;
								for (auto& item : fields->items)
									if (item.first == path) { item.second += "," + element.items[0].second; found = true; break; }
								if (!found)
									fields->items.emplace_back(path, element.items[0].second);
							}
						}
						space();
						if (!ok || i >= s.size()) { ok = false; break; }
						if (s[i] == ',') { i++; continue; }
						if (s[i] == ']') { i++; break; }
						ok = false;
						break;
					}
			}
			else if (s[i] == '"')
			{
				std::string text = string();
				if (fields)
					fields->items.emplace_back(path, std::move(text));
			}
			else
			{
				const size_t start = i;
				while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t')
					i++;
				std::string text = s.substr(start, i - start);
				if (text.empty())
					ok = false;
				else if (text == "null")
					text.clear();
				if (fields && ok)
					fields->items.emplace_back(path, std::move(text));
			}
			depth--;
		}
	};
}

bool jsonRead(const std::string& text, const std::string& arrayPath, JsonFields& fields, const std::function<void(const JsonFields&)>& each)
{
	Reader reader(text, arrayPath, each);
	reader.space();
	if (reader.i >= text.size() || (text[reader.i] != '{' && text[reader.i] != '['))
		return false;
	reader.value("", &fields);
	return reader.ok;
}
