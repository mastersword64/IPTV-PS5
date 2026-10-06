#pragma once
#include <cstdint>
#include <string>

// Drawing on a virtual 1920x1080 screen, scaled to whatever the console outputs.
// Colours are 0xRRGGBBAA.
namespace gfx
{
	bool init(int drawableWidth, int drawableHeight, const char* fontPath, std::string& error);
	void begin(uint32_t clearColour);
	void end();                       // sends everything queued to the GPU
	void rect(float x, float y, float w, float h, uint32_t colour);
	void roundRect(float x, float y, float w, float h, float radius, uint32_t colour);
	// A rounded panel with an outline of the given thickness.
	void panel(float x, float y, float w, float h, float radius, float thickness, uint32_t border, uint32_t fill);
	void triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t colour);
	// Pictures (RGBA, 8 bits each): made once, then drawn as often as wanted. -1 if it could not be made.
	int imageCreate(const uint8_t* rgba, int width, int height, bool smooth = true);
	bool imageSize(int image, int& width, int& height);
	void image(int image, float x, float y, float w, float h, uint32_t tint = 0xffffffff);
	int drawCalls();                  // how many batches the last finished frame took
	// Times how quickly the graphics layer accepts picture data, picks the quicker of two ways to
	// send video, and returns the bytes per millisecond it manages.
	double measureUploads();
	// Themes can make every corner rounder or sharper.
	void setRadiusScale(float scale);
	float radiusScale();
	// Draws text with its top at y; cut short with "..." if wider than maxWidth (0 = no limit).
	float text(float x, float y, float size, uint32_t colour, const std::string& utf8, float maxWidth = 0);
	float textWidth(float size, const std::string& utf8);

	// Video: one decoded picture as three planes (Y, U, V; 4:2:0).
	// matrix: 0 BT.601, 1 BT.709, 2 BT.2020; transfer: 0 ordinary, 1 HDR (PQ), 2 HDR (HLG)
	void videoUpload(const uint8_t* const planes[3], const int linesize[3], int width, int height, bool fullRange, int matrix, int transfer);
	void videoDraw(float x, float y, float w, float h);   // fitted inside this area, keeping its shape
	bool videoReady();
	void videoClear();
}
