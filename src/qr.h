#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Makes a QR code for a short piece of text (up to about 180 characters).
// `modules` is size x size, row by row: 1 for a dark square, 0 for a light one.
// The code needs a light border four squares wide around it when drawn.
bool qrEncode(const std::string& text, std::vector<uint8_t>& modules, int& size);
