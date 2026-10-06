#include "gfx.h"
#include "log.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/glcorearb.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
	const float kVirtualW = 1920.0f, kVirtualH = 1080.0f;
	int g_atlasSize = 2048;       // doubled, with the glyph size, when drawing at 4K so text stays sharp
	int g_glyphPx = 48;
	float g_radiusScale = 1.0f;

	struct Glyph { float u0, v0, u1, v1; int w, h, left, top, advance; bool ok; };

	int g_w = 1920, g_h = 1080;
	GLuint g_uiProgram = 0, g_vao = 0, g_vbo = 0;
	GLint g_uiScreen = -1, g_uiPixel = -1, g_uiTex = -1, g_uiImage = -1;
	GLuint g_boundImage = 0;      // the picture the batch being built draws from (0: none)
	int g_drawCalls = 0, g_lastDrawCalls = 0;
	struct Image { GLuint texture; int w, h; };
	std::vector<Image> g_images;
	struct VideoProgram { GLuint program = 0; GLint screen = -1, sheet = -1, yuv = -1, coef = -1, tap = -1, hdr = -1, region[3] = { -1, -1, -1 }; };
	VideoProgram g_videoPlain, g_videoPacked;
	bool g_packed = false;        // which of the two ways the video sheet is sent (chosen by measuring)
	GLuint g_atlasTex = 0;
	int g_penX = 1, g_penY = 1, g_rowH = 0;
	FT_Library g_ft = nullptr;
	FT_Face g_face = nullptr;
	int g_ascent = 40;
	std::unordered_map<uint32_t, Glyph> g_glyphs;

	// Everything on screen except the video goes through one list of vertices and one shader, so a
	// whole frame is a handful of draw calls. Per vertex: x y | u v | r g b a | kind w h radius.
	// kind: 0 flat colour, 1 text, 2 rounded box, 3 rounded outline (its thickness / 100 is added), 4 picture
	std::vector<float> g_verts;
	const int kVertexFloats = 12;

	// The picture is sent to the graphics chip as ONE sheet per frame (three separate sends each made
	// the chip wait), into one of three textures used in turn, so the texture being written is never
	// the one the frame on screen is still drawn from.
	GLuint g_videoTex[3] = { 0, 0, 0 };
	int g_videoTexW[3] = { 0, 0, 0 }, g_videoTexH[3] = { 0, 0, 0 };
	int g_videoTurn = 0, g_videoShown = 0;
	int g_videoW = 0, g_videoH = 0, g_sheetW = 0, g_sheetH = 0;
	std::vector<uint8_t> g_sheet;

	bool g_videoFull = false, g_videoReady = false;
	int g_videoMatrix = 1, g_videoTransfer = 0;

	const char* kVertex =
		"layout(location=0) in vec2 aPos;\n"
		"layout(location=1) in vec2 aUV;\n"
		"layout(location=2) in vec4 aColor;\n"
		"layout(location=3) in vec4 aShape;\n"
		"uniform vec2 uScreen;\n"
		"out vec2 vUV;\n"
		"out vec4 vColor;\n"
		"out vec4 vShape;\n"
		"void main(){\n"
		"  vUV = aUV; vColor = aColor; vShape = aShape;\n"
		"  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);\n"
		"}\n";

	const char* kUiFragment =
		"in vec2 vUV;\n"
		"in vec4 vColor;\n"
		"in vec4 vShape;\n"
		"uniform sampler2D uTex;\n"
		"uniform sampler2D uImage;\n"
		"uniform float uPixel;\n"      // screen pixels per unit, for smooth edges at any resolution
		"out vec4 oColor;\n"
		"void main(){\n"
		"  vec4 c = vColor;\n"
		"  float kind = floor(vShape.x + 0.001);\n"
		"  if (kind > 3.5) {\n"
		"    c = texture(uImage, vUV) * vColor;\n"
		"  } else if (kind > 1.5) {\n"
		"    vec2 hs = vShape.yz * 0.5;\n"
		"    float r = min(vShape.w, min(hs.x, hs.y));\n"
		"    vec2 q = abs(vUV - hs) - (hs - vec2(r));\n"
		"    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
		"    float cover = clamp(0.5 - d * uPixel, 0.0, 1.0);\n"
		"    if (kind > 2.5) cover *= clamp(0.5 + (d + fract(vShape.x) * 100.0) * uPixel, 0.0, 1.0);\n"
		"    c.a *= cover;\n"
		"  } else if (kind > 0.5) {\n"
		"    c.a *= texture(uTex, vUV).r;\n"
		"  }\n"
		"  oColor = c;\n"
		"}\n";

	const char* kVideoHead =
		"in vec2 vUV;\n"
		"in vec4 vColor;\n"
		"uniform sampler2D uY;\n"   // one sheet: brightness on top, the two colour planes side by side below
		"uniform vec4 uRegionY;\n"  // where each plane sits on the sheet: left, top, right, bottom
		"uniform vec4 uRegionU;\n"
		"uniform vec4 uRegionV;\n"
		"uniform vec3 uYuv;\n"     // luma offset, luma scale, chroma scale
		"uniform vec4 uCoef;\n"    // Cr->R, Cb->G, Cr->G, Cb->B
		"uniform vec2 uTap;\n"     // a quarter of one screen dot, in picture space (0 when not shrinking)
		"uniform float uHdr;\n"    // 0 ordinary, 1 HDR (PQ), 2 HDR (HLG)
		"out vec4 oColor;\n";

	// How one dot of one plane is read. Two ways, because this console's graphics layer accepts
	// pictures slowly and by the dot rather than by the byte: either the sheet is an ordinary
	// one-byte-per-dot texture that the chip smooths itself...
	const char* kVideoReadPlain =
		"float one(vec4 region, vec2 uv){\n"
		"  return texture(uY, region.xy + clamp(uv, 0.0, 1.0) * (region.zw - region.xy)).r;\n"
		"}\n";

	// ...or four bytes of the sheet travel in each dot of a four-byte texture (a quarter as many dots
	// to accept), and the shader picks the bytes back out and does the smoothing itself.
	const char* kVideoReadPacked =
		"float byteAt(ivec2 p){\n"
		"  vec4 t = texelFetch(uY, ivec2(p.x >> 2, p.y), 0);\n"
		"  int c = p.x & 3;\n"
		"  return c == 0 ? t.r : (c == 1 ? t.g : (c == 2 ? t.b : t.a));\n"
		"}\n"
		"float one(vec4 region, vec2 uv){\n"
		"  vec2 lo = region.xy, hi = region.zw - 1.0;\n"
		"  vec2 p = region.xy + clamp(uv, 0.0, 1.0) * (region.zw - region.xy) - 0.5;\n"
		"  vec2 b = floor(p);\n"
		"  vec2 f = p - b;\n"
		"  ivec2 p0 = ivec2(clamp(b, lo, hi));\n"
		"  ivec2 p1 = ivec2(clamp(b + 1.0, lo, hi));\n"
		"  float top = mix(byteAt(ivec2(p0.x, p0.y)), byteAt(ivec2(p1.x, p0.y)), f.x);\n"
		"  float bottom = mix(byteAt(ivec2(p0.x, p1.y)), byteAt(ivec2(p1.x, p1.y)), f.x);\n"
		"  return mix(top, bottom, f.y);\n"
		"}\n";

	// When the picture is drawn smaller than it is, each screen dot covers several picture dots.
	// Four samples spread over the screen dot average them, instead of picking one (which sparkles).
	const char* kVideoMain =
		"float plane(vec4 region){\n"
		"  return (one(region, vUV + vec2(-uTap.x, -uTap.y)) + one(region, vUV + vec2(uTap.x, -uTap.y))\n"
		"        + one(region, vUV + vec2(-uTap.x, uTap.y)) + one(region, vUV + vec2(uTap.x, uTap.y))) * 0.25;\n"
		"}\n"
		"void main(){\n"
		"  float y = (plane(uRegionY) - uYuv.x) * uYuv.y;\n"
		"  float cb = (plane(uRegionU) - 0.5) * uYuv.z;\n"
		"  float cr = (plane(uRegionV) - 0.5) * uYuv.z;\n"
		"  vec3 rgb = vec3(y + uCoef.x * cr, y - uCoef.y * cb - uCoef.z * cr, y + uCoef.w * cb);\n"
		"  if (uHdr > 0.5) {\n"
		// HDR pictures hold far brighter highlights and wider colours than this screen mode shows.
		// Shown as they are they look grey and flat, so: decode to light, compress the highlights,
		// bring the colours into the ordinary range, and encode again.
		"    rgb = clamp(rgb, 0.0, 1.0);\n"
		"    vec3 lin;\n"
		"    if (uHdr < 1.5) {\n"
		"      vec3 p = pow(rgb, vec3(1.0 / 78.84375));\n"
		"      lin = pow(max(p - 0.8359375, 0.0) / (18.8515625 - 18.6875 * p), vec3(1.0 / 0.1593017578125));\n"
		"      lin *= 10000.0 / 203.0 * 1.5;\n"       // 1.0 = paper white, lifted a little for an ordinary screen
		"      float peak = 1000.0 / 203.0 * 1.5;\n"  // highlights up to 1000 nits are kept, compressed
		"      lin = lin * (1.0 + lin / (peak * peak)) / (1.0 + lin);\n"
		"    } else {\n"
		"      lin = pow(rgb, vec3(2.2));\n"
		"    }\n"
		"    lin = mat3(1.6605, -0.1246, -0.0182, -0.5876, 1.1329, -0.1006, -0.0728, -0.0083, 1.1187) * lin;\n"
		"    rgb = pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));\n"
		"  }\n"
		"  oColor = vec4(rgb, 1.0);\n"
		"}\n";

	GLuint compile(GLenum type, const char* header, const char* body, std::string& log)
	{
		const GLuint shader = glCreateShader(type);
		const char* parts[2] = { header, body };
		glShaderSource(shader, 2, parts, nullptr);
		glCompileShader(shader);
		GLint ok = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char text[1024] = "";
			glGetShaderInfoLog(shader, sizeof(text) - 1, nullptr, text);
			log = text;
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	GLuint build(const char* header, const char* fragment, std::string& log)
	{
		const GLuint vs = compile(GL_VERTEX_SHADER, header, kVertex, log);
		if (!vs)
			return 0;
		const GLuint fs = compile(GL_FRAGMENT_SHADER, header, fragment, log);
		if (!fs)
		{
			glDeleteShader(vs);
			return 0;
		}
		const GLuint program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char text[1024] = "";
			glGetProgramInfoLog(program, sizeof(text) - 1, nullptr, text);
			log = text;
			glDeleteProgram(program);
			return 0;
		}
		return program;
	}

	void flush()
	{
		if (g_verts.empty())
			return;
		glUseProgram(g_uiProgram);
		glUniform2f(g_uiScreen, kVirtualW, kVirtualH);
		glUniform1f(g_uiPixel, (float)g_w / kVirtualW);
		glUniform1i(g_uiTex, 0);
		glUniform1i(g_uiImage, 1);
		if (g_boundImage)
		{
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, g_boundImage);
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, g_atlasTex);
		glBindVertexArray(g_vao);
		glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_verts.size() * sizeof(float)), g_verts.data(), GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_verts.size() / kVertexFloats));
		g_verts.clear();
		g_drawCalls++;
	}

	void vertex(float x, float y, float u, float v, const float c[4], const float shape[4])
	{
		g_verts.insert(g_verts.end(), { x, y, u, v, c[0], c[1], c[2], c[3], shape[0], shape[1], shape[2], shape[3] });
	}

	void shapedQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const float c[4], const float shape[4])
	{
		vertex(x, y, u0, v0, c, shape);
		vertex(x + w, y, u1, v0, c, shape);
		vertex(x + w, y + h, u1, v1, c, shape);
		vertex(x, y, u0, v0, c, shape);
		vertex(x + w, y + h, u1, v1, c, shape);
		vertex(x, y + h, u0, v1, c, shape);
	}

	// A box whose corners (and, for an outline, inner edge) are worked out per pixel by the shader.
	void roundedQuad(float x, float y, float w, float h, float radius, float kind, const float c[4])
	{
		if (w <= 0 || h <= 0)
			return;
		const float shape[4] = { kind, w, h, radius };
		// one unit of margin all round, so the smoothed edge is not cut off
		shapedQuad(x - 1, y - 1, w + 2, h + 2, -1, -1, w + 1, h + 1, c, shape);
	}

	void quad(int mode, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const float c[4])
	{
		const float shape[4] = { (float)mode, 0, 0, 0 };
		shapedQuad(x, y, w, h, u0, v0, u1, v1, c, shape);
	}

	void unpack(uint32_t colour, float c[4])
	{
		c[0] = (float)((colour >> 24) & 0xff) / 255.0f;
		c[1] = (float)((colour >> 16) & 0xff) / 255.0f;
		c[2] = (float)((colour >> 8) & 0xff) / 255.0f;
		c[3] = (float)(colour & 0xff) / 255.0f;
	}

	const Glyph& glyph(uint32_t code)
	{
		const auto found = g_glyphs.find(code);
		if (found != g_glyphs.end())
			return found->second;

		Glyph g = {};
		if (g_face && FT_Load_Char(g_face, code, FT_LOAD_RENDER) == 0)
		{
			const FT_GlyphSlot slot = g_face->glyph;
			const int w = (int)slot->bitmap.width, h = (int)slot->bitmap.rows;
			g.advance = (int)(slot->advance.x >> 6);
			g.left = slot->bitmap_left;
			g.top = slot->bitmap_top;
			g.w = w;
			g.h = h;
			g.ok = true;
			if (w > 0 && h > 0 && slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
			{
				if (g_penX + w + 1 > g_atlasSize)
				{
					g_penX = 1;
					g_penY += g_rowH + 1;
					g_rowH = 0;
				}
				if (g_penY + h + 1 <= g_atlasSize)
				{
					std::vector<unsigned char> tight((size_t)w * h);
					for (int row = 0; row < h; row++)
						memcpy(&tight[(size_t)row * w], slot->bitmap.buffer + (ptrdiff_t)row * slot->bitmap.pitch, (size_t)w);
					glBindTexture(GL_TEXTURE_2D, g_atlasTex);
					glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
					glTexSubImage2D(GL_TEXTURE_2D, 0, g_penX, g_penY, w, h, GL_RED, GL_UNSIGNED_BYTE, tight.data());
					g.u0 = (float)g_penX / g_atlasSize;
					g.v0 = (float)g_penY / g_atlasSize;
					g.u1 = (float)(g_penX + w) / g_atlasSize;
					g.v1 = (float)(g_penY + h) / g_atlasSize;
					g_penX += w + 1;
					if (h > g_rowH)
						g_rowH = h;
				}
				else
					g.w = g.h = 0; // the glyph sheet is full: this character is left blank
			}
			else
				g.w = g.h = 0;
		}
		return g_glyphs.emplace(code, g).first->second;
	}

	// Reads the next character of UTF-8 text; malformed bytes become '?'.
	uint32_t nextCode(const std::string& s, size_t& i)
	{
		const unsigned char c = (unsigned char)s[i++];
		int extra = 0;
		uint32_t code = c;
		if (c >= 0xF0) { extra = 3; code = c & 0x07; }
		else if (c >= 0xE0) { extra = 2; code = c & 0x0F; }
		else if (c >= 0xC0) { extra = 1; code = c & 0x1F; }
		else if (c >= 0x80) return '?';
		while (extra-- > 0)
		{
			if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80)
				return '?';
			code = (code << 6) | ((unsigned char)s[i++] & 0x3F);
		}
		return code;
	}
}

bool gfx::init(int drawableWidth, int drawableHeight, const char* fontPath, std::string& error)
{
	g_w = drawableWidth > 0 ? drawableWidth : 1920;
	g_h = drawableHeight > 0 ? drawableHeight : 1080;
	if (g_h >= 2000)
	{
		// 4K: text is drawn from glyphs twice the size, so it is as sharp as the screen
		g_glyphPx = 96;
		g_atlasSize = 4096;
	}

	// The PS5 OpenGL layer is desktop OpenGL; the second header is tried in case it prefers the embedded dialect.
	static const char* const headers[] = { "#version 330 core\n", "#version 300 es\nprecision highp float;\n" };
	std::string log;
	const char* used = nullptr;
	for (const char* header : headers)
	{
		g_uiProgram = build(header, kUiFragment, log);
		if (g_uiProgram)
		{
			const std::string plain = std::string(kVideoHead) + kVideoReadPlain + kVideoMain;
			const std::string packed = std::string(kVideoHead) + kVideoReadPacked + kVideoMain;
			g_videoPlain.program = build(header, plain.c_str(), log);
			std::string packedLog;
			g_videoPacked.program = build(header, packed.c_str(), packedLog);
			if (!g_videoPacked.program)
				logLine("gfx: the packed video shader did not build (the plain one is used): %s", packedLog.c_str());
			used = header;
			break;
		}
		logLine("gfx: shaders did not build with %.16s: %s", header, log.c_str());
	}
	if (!g_uiProgram || !g_videoPlain.program)
	{
		error = "the graphics shaders could not be built: " + log;
		return false;
	}
	logLine("gfx: shaders built with %.16s", used);

	g_uiScreen = glGetUniformLocation(g_uiProgram, "uScreen");
	g_uiPixel = glGetUniformLocation(g_uiProgram, "uPixel");
	g_uiImage = glGetUniformLocation(g_uiProgram, "uImage");
	g_uiTex = glGetUniformLocation(g_uiProgram, "uTex");
	for (VideoProgram* v : { &g_videoPlain, &g_videoPacked })
	{
		if (!v->program)
			continue;
		v->screen = glGetUniformLocation(v->program, "uScreen");
		v->sheet = glGetUniformLocation(v->program, "uY");
		v->yuv = glGetUniformLocation(v->program, "uYuv");
		v->coef = glGetUniformLocation(v->program, "uCoef");
		v->tap = glGetUniformLocation(v->program, "uTap");
		v->hdr = glGetUniformLocation(v->program, "uHdr");
		v->region[0] = glGetUniformLocation(v->program, "uRegionY");
		v->region[1] = glGetUniformLocation(v->program, "uRegionU");
		v->region[2] = glGetUniformLocation(v->program, "uRegionV");
	}

	glGenVertexArrays(1, &g_vao);
	glBindVertexArray(g_vao);
	glGenBuffers(1, &g_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kVertexFloats * sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kVertexFloats * sizeof(float), (void*)(2 * sizeof(float)));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kVertexFloats * sizeof(float), (void*)(4 * sizeof(float)));
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, kVertexFloats * sizeof(float), (void*)(8 * sizeof(float)));

	glGenTextures(1, &g_atlasTex);
	glBindTexture(GL_TEXTURE_2D, g_atlasTex);
	{
		std::vector<unsigned char> blank((size_t)g_atlasSize * g_atlasSize, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g_atlasSize, g_atlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, blank.data());
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (FT_Init_FreeType(&g_ft) != 0 || FT_New_Face(g_ft, fontPath, 0, &g_face) != 0)
	{
		g_face = nullptr;
		error = std::string("the font could not be loaded: ") + fontPath;
		return false;
	}
	FT_Set_Pixel_Sizes(g_face, 0, g_glyphPx);
	g_ascent = (int)(g_face->size->metrics.ascender >> 6);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	return true;
}

void gfx::begin(uint32_t clearColour)
{
	float c[4];
	unpack(clearColour, c);
	glViewport(0, 0, g_w, g_h);
	glClearColor(c[0], c[1], c[2], 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	g_verts.clear();
}

void gfx::end()
{
	flush();
	g_lastDrawCalls = g_drawCalls;
	g_drawCalls = 0;
}

void gfx::rect(float x, float y, float w, float h, uint32_t colour)
{
	float c[4];
	unpack(colour, c);
	quad(0, x, y, w, h, 0, 0, 0, 0, c);
}

void gfx::setRadiusScale(float scale)
{
	g_radiusScale = scale;
}

float gfx::radiusScale()
{
	return g_radiusScale;
}

void gfx::roundRect(float x, float y, float w, float h, float radius, uint32_t colour)
{
	float c[4];
	unpack(colour, c);
	radius *= g_radiusScale;
	if (radius < 0.5f)
		quad(0, x, y, w, h, 0, 0, 0, 0, c);
	else
		roundedQuad(x, y, w, h, radius, 2.0f, c);
}

void gfx::panel(float x, float y, float w, float h, float radius, float thickness, uint32_t border, uint32_t fill)
{
	float c[4];
	radius *= g_radiusScale;
	if ((fill & 0xff) != 0)
	{
		unpack(fill, c);
		roundedQuad(x, y, w, h, radius, 2.0f, c);
	}
	if ((border & 0xff) != 0 && thickness > 0)
	{
		// drawn as a ring, so a see-through fill is not tinted by its outline
		unpack(border, c);
		roundedQuad(x, y, w, h, radius, 3.0f + (thickness > 49 ? 49 : thickness) / 100.0f, c);
	}
}

void gfx::triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t colour)
{
	float c[4];
	unpack(colour, c);
	const float shape[4] = { 0, 0, 0, 0 };
	vertex(x1, y1, 0, 0, c, shape);
	vertex(x2, y2, 0, 0, c, shape);
	vertex(x3, y3, 0, 0, c, shape);
}

int gfx::imageCreate(const uint8_t* rgba, int width, int height, bool smooth)
{
	if (!rgba || width <= 0 || height <= 0)
		return -1;
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	// small pixel-drawn pictures are kept crisp; larger ones are smoothed
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glActiveTexture(GL_TEXTURE0);
	g_boundImage = 0;                                  // unit 1 no longer holds what the batch thinks
	g_images.push_back({ texture, width, height });
	return (int)g_images.size() - 1;
}

bool gfx::imageSize(int image, int& width, int& height)
{
	if (image < 0 || image >= (int)g_images.size())
		return false;
	width = g_images[(size_t)image].w;
	height = g_images[(size_t)image].h;
	return true;
}

void gfx::image(int image, float x, float y, float w, float h, uint32_t tint)
{
	if (image < 0 || image >= (int)g_images.size())
		return;
	const GLuint texture = g_images[(size_t)image].texture;
	if (g_boundImage != texture)
	{
		flush();                                       // a batch draws from one picture at a time
		g_boundImage = texture;
	}
	float c[4];
	unpack(tint, c);
	const float shape[4] = { 4, 0, 0, 0 };
	shapedQuad(x, y, w, h, 0, 0, 1, 1, c, shape);
}

// Sends a megabyte of picture each way a few times and times it. The quicker way is used for video
// from then on. Returns how many bytes per millisecond that way manages.
double gfx::measureUploads()
{
	const int side = 1024;
	std::vector<uint8_t> data((size_t)side * side, 128);
	GLuint textures[2] = { 0, 0 };
	double best[2] = { 1e9, 1e9 };
	glGenTextures(2, textures);
	glActiveTexture(GL_TEXTURE0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (int way = 0; way < 2; way++)
	{
		const int w = way ? side / 4 : side;
		const GLenum inside = way ? GL_RGBA8 : GL_R8, layout = way ? GL_RGBA : GL_RED;
		glBindTexture(GL_TEXTURE_2D, textures[way]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)inside, w, side, 0, layout, GL_UNSIGNED_BYTE, data.data());
		glFinish();
		for (int round = 0; round < 3; round++)
		{
			data[(size_t)round] = (uint8_t)round;          // not the same bytes each time
			const auto began = std::chrono::steady_clock::now();
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, side, layout, GL_UNSIGNED_BYTE, data.data());
			glFinish();
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
			if (ms < best[way])
				best[way] = ms;
		}
	}
	glBindTexture(GL_TEXTURE_2D, g_atlasTex);
	glDeleteTextures(2, textures);
	const double megabyte = (double)side * side;
	logLine("gfx: accepting 1 MB of picture takes %.1f ms one byte per dot, %.1f ms four bytes per dot", best[0], best[1]);
	g_packed = g_videoPacked.program != 0 && best[1] < best[0] * 0.7;
	logLine("gfx: video will be sent %s", g_packed ? "four bytes per dot" : "one byte per dot");
	const double ms = g_packed ? best[1] : best[0];
	return megabyte / (ms > 0.05 ? ms : 0.05);
}

int gfx::drawCalls()
{
	return g_lastDrawCalls;
}

float gfx::textWidth(float size, const std::string& utf8)
{
	const float scale = size / (float)g_glyphPx;
	float width = 0;
	for (size_t i = 0; i < utf8.size();)
		width += (float)glyph(nextCode(utf8, i)).advance * scale;
	return width;
}

float gfx::text(float x, float y, float size, uint32_t colour, const std::string& utf8, float maxWidth)
{
	float c[4];
	unpack(colour, c);
	const float scale = size / (float)g_glyphPx;
	const float baseline = y + (float)g_ascent * scale;
	const bool limited = maxWidth > 0 && textWidth(size, utf8) > maxWidth;
	const float dots = limited ? textWidth(size, "...") : 0;
	float pen = x;

	for (size_t i = 0; i < utf8.size();)
	{
		const Glyph& g = glyph(nextCode(utf8, i));
		if (limited && pen - x + (float)g.advance * scale > maxWidth - dots)
		{
			for (int k = 0; k < 3; k++)
			{
				const Glyph& dot = glyph('.');
				if (dot.w > 0)
					quad(1, pen + (float)dot.left * scale, baseline - (float)dot.top * scale, (float)dot.w * scale, (float)dot.h * scale, dot.u0, dot.v0, dot.u1, dot.v1, c);
				pen += (float)dot.advance * scale;
			}
			break;
		}
		if (g.w > 0)
			quad(1, pen + (float)g.left * scale, baseline - (float)g.top * scale, (float)g.w * scale, (float)g.h * scale, g.u0, g.v0, g.u1, g.v1, c);
		pen += (float)g.advance * scale;
	}
	return pen - x;
}

void gfx::videoUpload(const uint8_t* const planes[3], const int linesize[3], int width, int height, bool fullRange, int matrix, int transfer)
{
	if (width <= 0 || height <= 0 || !planes[0] || !planes[1] || !planes[2])
		return;
	// Lay the three planes out on one sheet: brightness across the top, the two half-size colour
	// planes side by side underneath.
	const int cw = (width + 1) / 2, ch = (height + 1) / 2;
	const int sheetW = ((width > cw * 2 ? width : cw * 2) + 3) & ~3, sheetH = height + ch;
	g_sheet.resize((size_t)sheetW * (size_t)sheetH);
	for (int y = 0; y < height; y++)
		memcpy(&g_sheet[(size_t)y * (size_t)sheetW], planes[0] + (ptrdiff_t)y * linesize[0], (size_t)width);
	for (int y = 0; y < ch; y++)
	{
		uint8_t* row = &g_sheet[(size_t)(height + y) * (size_t)sheetW];
		memcpy(row, planes[1] + (ptrdiff_t)y * linesize[1], (size_t)cw);
		memcpy(row + cw, planes[2] + (ptrdiff_t)y * linesize[2], (size_t)cw);
	}

	const int turn = (g_videoTurn + 1) % 3;
	glActiveTexture(GL_TEXTURE0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (!g_videoTex[turn])
	{
		glGenTextures(1, &g_videoTex[turn]);
		glBindTexture(GL_TEXTURE_2D, g_videoTex[turn]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_packed ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_packed ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glBindTexture(GL_TEXTURE_2D, g_videoTex[turn]);
	const int texW = g_packed ? sheetW / 4 : sheetW;
	const GLenum inside = g_packed ? GL_RGBA8 : GL_R8, layout = g_packed ? GL_RGBA : GL_RED;
	if (g_videoTexW[turn] != texW || g_videoTexH[turn] != sheetH)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)inside, texW, sheetH, 0, layout, GL_UNSIGNED_BYTE, g_sheet.data());
		g_videoTexW[turn] = texW;
		g_videoTexH[turn] = sheetH;
	}
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, sheetH, layout, GL_UNSIGNED_BYTE, g_sheet.data());
	glBindTexture(GL_TEXTURE_2D, g_atlasTex);
	g_videoTurn = turn;
	g_videoShown = turn;
	g_sheetW = sheetW;
	g_sheetH = sheetH;
	g_videoW = width;
	g_videoH = height;
	g_videoFull = fullRange;
	g_videoMatrix = matrix;
	g_videoTransfer = transfer;
	g_videoReady = true;
}

void gfx::videoClear()
{
	g_videoReady = false;
}

bool gfx::videoReady()
{
	return g_videoReady;
}

void gfx::videoDraw(float areaX, float areaY, float areaW, float areaH)
{
	if (!g_videoReady)
		return;
	flush();

	float w = areaW, h = areaW * (float)g_videoH / (float)g_videoW;
	if (h > areaH)
	{
		h = areaH;
		w = areaH * (float)g_videoW / (float)g_videoH;
	}
	const float x = areaX + (areaW - w) / 2, y = areaY + (areaH - h) / 2;
	const float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float p[6][4] = {
		{ x, y, 0, 0 }, { x + w, y, 1, 0 }, { x + w, y + h, 1, 1 },
		{ x, y, 0, 0 }, { x + w, y + h, 1, 1 }, { x, y + h, 0, 1 } };
	float verts[6 * kVertexFloats];
	memset(verts, 0, sizeof(verts));
	for (int i = 0; i < 6; i++)
	{
		memcpy(&verts[i * kVertexFloats], p[i], 4 * sizeof(float));
		memcpy(&verts[i * kVertexFloats + 4], c, 4 * sizeof(float));
	}

	glDisable(GL_BLEND);
	const VideoProgram& v = g_packed ? g_videoPacked : g_videoPlain;
	glUseProgram(v.program);
	glUniform2f(v.screen, kVirtualW, kVirtualH);
	if (g_videoFull)
		glUniform3f(v.yuv, 0.0f, 1.0f, 1.0f);
	else
		glUniform3f(v.yuv, 16.0f / 255.0f, 255.0f / 219.0f, 255.0f / 224.0f);
	if (g_videoMatrix == 2)
		glUniform4f(v.coef, 1.4746f, 0.1646f, 0.5714f, 1.8814f);    // BT.2020 (UHD)
	else if (g_videoMatrix == 1)
		glUniform4f(v.coef, 1.5748f, 0.1873f, 0.4681f, 1.8556f);    // BT.709 (HD)
	else
		glUniform4f(v.coef, 1.402f, 0.3441f, 0.7141f, 1.772f);      // BT.601 (SD)
	glUniform1f(v.hdr, (float)g_videoTransfer);
	{
		// how many picture dots land on one screen dot; above about one, the samples are spread
		const float drawnW = w * (float)g_w / kVirtualW, drawnH = h * (float)g_h / kVirtualH;
		const bool shrinking = drawnW > 1 && drawnH > 1 && (float)g_videoW / drawnW > 1.15f;
		glUniform2f(v.tap, shrinking ? 0.25f / drawnW : 0.0f, shrinking ? 0.25f / drawnH : 0.0f);
	}
	{
		// where each plane is on the sheet: left, top, right, bottom
		const float sw = (float)g_sheetW, sh = (float)g_sheetH;
		const float pw = (float)g_videoW, ph = (float)g_videoH;
		const float cw = (float)((g_videoW + 1) / 2), ch = (float)((g_videoH + 1) / 2);
		if (g_packed)
		{
			// in dots of the sheet: the shader does its own smoothing and stays inside each plane
			glUniform4f(v.region[0], 0, 0, pw, ph);
			glUniform4f(v.region[1], 0, ph, cw, ph + ch);
			glUniform4f(v.region[2], cw, ph, cw * 2, ph + ch);
		}
		else
		{
			// as fractions of the sheet, half a dot inside each plane's edges so neighbours do not bleed in
			glUniform4f(v.region[0], 0.5f / sw, 0.5f / sh, (pw - 0.5f) / sw, (ph - 0.5f) / sh);
			glUniform4f(v.region[1], 0.5f / sw, (ph + 0.5f) / sh, (cw - 0.5f) / sw, (ph + ch - 0.5f) / sh);
			glUniform4f(v.region[2], (cw + 0.5f) / sw, (ph + 0.5f) / sh, (cw * 2 - 0.5f) / sw, (ph + ch - 0.5f) / sh);
		}
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_videoTex[g_videoShown]);
	glUniform1i(v.sheet, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(g_vao);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	g_drawCalls++;
	glEnable(GL_BLEND);
}
