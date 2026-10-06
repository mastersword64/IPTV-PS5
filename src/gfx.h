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
	void imageUpdate(int image, const uint8_t* rgba, int width, int height);   // new contents for an existing picture
	void image(int image, float x, float y, float w, float h, uint32_t tint = 0xffffffff);
	// Limits drawing to a box (for lists that scroll smoothly under a heading); unclip() lifts it.
	void clip(float x, float y, float w, float h);
	void unclip();
	// Heavy lettering for what follows (titles, headings), until switched off again. Without a bold
	// font file the ordinary weight is used.
	void bold(bool on);
	// A picture with rounded corners, showing the part of it between (u0,v0) and (u1,v1), where
	// 0..1 spans the whole picture: used to fit a poster into a card of another shape.
	void picture(int image, float x, float y, float w, float h, float radius, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, float alpha = 1);
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
	// Whether the picture now showing is interlaced (two woven half-pictures); if so its combing is smoothed out.
	void videoInterlaced(bool on);
	// fit: 0 fitted inside the area keeping its shape, 1 zoomed to fill it (edges cut off), 2 stretched to it
	void videoDraw(float x, float y, float w, float h, int fit = 0);
	bool videoReady();
	void videoClear();
}
