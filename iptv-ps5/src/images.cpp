#include "images.h"

#ifdef IPTV_HAVE_STB
// The decoder is the single-header stb_image that ships with the EmulationStation kit.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

bool imagesSupported()
{
	return true;
}

bool loadImageFile(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height)
{
	int channels = 0;
	unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
	if (!pixels)
		return false;
	const bool sane = width > 0 && height > 0 && width <= 2048 && height <= 2048;
	if (sane)
		rgba.assign(pixels, pixels + (size_t)width * (size_t)height * 4);
	stbi_image_free(pixels);
	return sane;
}

bool loadImageMemory(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& width, int& height)
{
	int channels = 0;
	unsigned char* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
	if (!pixels)
		return false;
	const bool sane = width > 0 && height > 0 && width <= 2048 && height <= 2048;
	if (sane)
		rgba.assign(pixels, pixels + (size_t)width * (size_t)height * 4);
	stbi_image_free(pixels);
	return sane;
}
#else
bool imagesSupported()
{
	return false;
}

bool loadImageMemory(const uint8_t*, size_t, std::vector<uint8_t>&, int&, int&)
{
	return false;
}

bool loadImageFile(const std::string&, std::vector<uint8_t>&, int&, int&)
{
	return false;
}
#endif
