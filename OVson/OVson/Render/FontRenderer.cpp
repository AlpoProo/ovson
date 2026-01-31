#include "FontRenderer.h"
#include <Windows.h>
#include <gl/GL.h>
#include <vector>

FontRenderer::FontRenderer()
	: m_initialized(false), m_texture(0), m_bitmapWidth(2048), m_bitmapHeight(2048), m_bitmap(nullptr)
{
	memset(m_charWidths, 0, sizeof(m_charWidths));
}

FontRenderer::~FontRenderer()
{
	if (m_texture) {
		glDeleteTextures(1, &m_texture);
	}
	if (m_bitmap) {
		delete[] m_bitmap;
	}
}

bool FontRenderer::init(HDC hdc)
{
	if (m_initialized) return true;
	if (!hdc) return false;

	HDC memDC = CreateCompatibleDC(hdc);
	if (!memDC) return false;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = m_bitmapWidth;
	bmi.bmiHeader.biHeight = -m_bitmapHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!hBitmap) {
		DeleteDC(memDC);
		OutputDebugStringA("[OVson] FontRenderer: CreateDIBSection failed\n");
		return false;
	}

	HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

	// font
	HFONT hFont = CreateFontA(
		-32,                    // Height
		0,                      // Width
		0,                      // Escapement
		0,                      // Orientation
		FW_SEMIBOLD,            // Weight
		FALSE,                  // Italic
		FALSE,                  // Underline
		FALSE,                  // StrikeOut
		ANSI_CHARSET,           // CharSet
		OUT_TT_PRECIS,          // OutPrecision
		CLIP_DEFAULT_PRECIS,    // ClipPrecision
		ANTIALIASED_QUALITY,    // Quality
		FF_SWISS | DEFAULT_PITCH, // PitchAndFamily
		"Segoe UI"              // FaceName
	);

	if (!hFont) {
		hFont = CreateFontA(-32, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
	}

	if (!hFont) {
		SelectObject(memDC, hOldBitmap);
		DeleteObject(hBitmap);
		DeleteDC(memDC);
		return false;
	}

	HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

	memset(bits, 0xFF, m_bitmapWidth * m_bitmapHeight * 4);

	SetBkMode(memDC, TRANSPARENT); 
	SetTextColor(memDC, RGB(0, 0, 0));
	SetTextAlign(memDC, TA_TOP | TA_LEFT);
	
	const int charSize = 64; 
	const int charsPerRow = m_bitmapWidth / charSize;
	
	for (int i = 32; i < 127; i++) {
		char ch[2] = { (char)i, 0 };
		
		SIZE sz;
		GetTextExtentPoint32A(memDC, ch, 1, &sz);
		m_charWidths[i] = sz.cx;
		
		int x = (((i - 32) % charsPerRow) * charSize) + 8;
		int y = (((i - 32) / charsPerRow) * charSize) + 8; 
		
		TextOutA(memDC, x, y, ch, 1);
	}

	GdiFlush();

	m_bitmap = new unsigned char[m_bitmapWidth * m_bitmapHeight * 4];
	
	uint8_t* src = (uint8_t*)bits;
	uint8_t* dst = m_bitmap;
	
	for (int i = 0; i < m_bitmapWidth * m_bitmapHeight; i++) {
		uint8_t b = src[i * 4 + 0];
		uint8_t alpha = 255 - b;
		
		dst[i * 4 + 0] = 255; 
		dst[i * 4 + 1] = 255; 
		dst[i * 4 + 2] = 255; 
		dst[i * 4 + 3] = alpha; 
	}

	glGenTextures(1, &m_texture);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_bitmapWidth, m_bitmapHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_bitmap);

	SelectObject(memDC, hOldFont);
	SelectObject(memDC, hOldBitmap);
	DeleteObject(hFont);
	DeleteObject(hBitmap);
	DeleteDC(memDC);

	m_initialized = true;
	return true;
}

float FontRenderer::getCharWidth(char c) const {
	if (c < 32 || c > 126) return 8.0f;
	return (float)m_charWidths[(unsigned char)c];
}

void FontRenderer::drawString(float x, float y, const std::string& text, uint32_t color, float scale)
{
	if (!m_initialized || text.empty()) return;

	float r = ((color >> 16) & 0xFF) / 255.0f;
	float g = ((color >> 8) & 0xFF) / 255.0f;
	float b = (color & 0xFF) / 255.0f;
	float a = ((color >> 24) & 0xFF) / 255.0f;
	if (a == 0.0f) a = 1.0f;

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glColor4f(r, g, b, a);
	
	const int charSize = 64; 
	const int charsPerRow = m_bitmapWidth / charSize;
	const float renderScale = scale;

	float currentX = x;

	glBegin(GL_QUADS);
	for (size_t k = 0; k < text.length(); k++) {
		unsigned char ch = (unsigned char)text[k];
        
        if (ch == 167 || ch == '&') { // 167 is §
            if (k + 1 < text.length()) {
                char code = tolower(text[k + 1]);
                uint32_t newColor = color;
                bool found = true;
                if (code == '0') newColor = 0xFF000000;
                else if (code == '1') newColor = 0xFF0000AA;
                else if (code == '2') newColor = 0xFF00AA00;
                else if (code == '3') newColor = 0xFF00AAAA;
                else if (code == '4') newColor = 0xFFAA0000;
                else if (code == '5') newColor = 0xFFAA00AA;
                else if (code == '6') newColor = 0xFFFFAA00;
                else if (code == '7') newColor = 0xFFAAAAAA;
                else if (code == '8') newColor = 0xFF555555;
                else if (code == '9') newColor = 0xFF5555FF;
                else if (code == 'a') newColor = 0xFF55FF55;
                else if (code == 'b') newColor = 0xFF55FFFF;
                else if (code == 'c') newColor = 0xFFFF5555;
                else if (code == 'd') newColor = 0xFFFF55FF;
                else if (code == 'e') newColor = 0xFFFFFF55;
                else if (code == 'f') newColor = 0xFFFFFFFF;
                else if (code == 'r') newColor = color; // Reset
                else found = false;

                if (found) {
                    float nr = ((newColor >> 16) & 0xFF) / 255.0f;
                    float ng = ((newColor >> 8) & 0xFF) / 255.0f;
                    float nb = (newColor & 0xFF) / 255.0f;
                    float na = ((newColor >> 24) & 0xFF) / 255.0f;
                    if (na == 0.0f) na = a;
                    glColor4f(nr, ng, nb, na);
                    k++;
                    continue;
                }
            }
        }

		if (ch < 32 || ch > 126) continue;

		int i = ch - 32;
		const int charSize = 64;
		const int charsPerRow = m_bitmapWidth / charSize;
		int col = i % charsPerRow;
		int row = i / charsPerRow;

		float realWidth = getCharWidth((char)ch);
		float renderWidth = (realWidth + 16.0f) * renderScale;
		float renderHeight = (float)charSize * renderScale; 

		float u0 = (float)(col * charSize) / (float)m_bitmapWidth;
		float v0 = (float)(row * charSize) / (float)m_bitmapHeight;
		float u1 = u0 + ((realWidth + 16.0f) / (float)m_bitmapWidth);
		float v1 = v0 + ((float)charSize / (float)m_bitmapHeight);

		float x0 = currentX - (8.0f * renderScale);
		float y0 = y - (8.0f * renderScale);
		float x1 = x0 + renderWidth;
		float y1 = y0 + renderHeight;

		glTexCoord2f(u0, v0); glVertex2f(x0, y0);
		glTexCoord2f(u1, v0); glVertex2f(x1, y0);
		glTexCoord2f(u1, v1); glVertex2f(x1, y1);
		glTexCoord2f(u0, v1); glVertex2f(x0, y1);

		currentX += (realWidth + 1.0f) * renderScale; 
	}
	glEnd();

    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

float FontRenderer::getStringWidth(const std::string& text)
{
	float w = 0.0f;
	const float renderScale = 0.5f;
	for (size_t i = 0; i < text.length(); i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == 167 || ch == '&') {
            if (i + 1 < text.length()) {
                i++;
                continue;
            }
        }
        if (ch < 32 || ch > 126) continue;
		w += (getCharWidth(ch) * renderScale) + (1.0f * renderScale);
	}
	return w;
}
