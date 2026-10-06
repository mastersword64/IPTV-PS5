#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Decodes a picture file (PNG, JPEG, ...) into RGBA. Returns false if the file cannot be read,
// or if this build was made without the picture decoder.
bool loadImageFile(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height);
bool loadImageMemory(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& width, int& height);
bool imagesSupported();
