#pragma once
#include <string>
#include <Windows.h>
#include <gl/GL.h>
#include <cstdint>

class FontRenderer {
public:
	FontRenderer();
	~FontRenderer();

	bool init(HDC hdc);
	void drawString(float x, float y, const std::string& text, uint32_t color, float scale = 0.5f);
	float getStringWidth(const std::string& text);
	float getHeight() const { return 16.0f; }
	bool isInitialized() const { return m_initialized; }

	float getCharWidth(char c) const;

private:
	bool m_initialized;
	GLuint m_texture;
	int m_bitmapWidth;
	int m_bitmapHeight;
	unsigned char* m_bitmap;
	int m_charWidths[256];
};
