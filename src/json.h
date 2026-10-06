#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

// A small reader for the JSON that IPTV servers send.
// Objects are flattened: {"user_info":{"auth":1}} becomes the field "user_info.auth" = "1".
struct JsonFields
{
	std::vector<std::pair<std::string, std::string>> items;
	const std::string& get(const std::string& key) const;   // "" if absent
	bool has(const std::string& key) const;
};

// Reads everything outside `arrayPath` into `fields`, and calls `each` once per element of the
// array found at `arrayPath` ("" means the whole document is the array; "a.*" means every list
// directly under "a", with the list's own key given to `each` as the field "@key").
// Returns false if the text is not JSON.
bool jsonRead(const std::string& text, const std::string& arrayPath, JsonFields& fields,
              const std::function<void(const JsonFields&)>& each);
