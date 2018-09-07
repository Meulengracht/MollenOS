/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS Terminal Implementation
* - Project Alumnious
*/
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_GLYPH_H
#include FT_TRUETYPE_IDS_H
#include "surfaces/surface.hpp"

typedef struct FontGlyph 
{
	int Stored;
	FT_UInt Index;
	FT_Bitmap Bitmap;
	FT_Bitmap Pixmap;
	int MinX;
	int MaxX;
	int MinY;
	int MaxY;
	int yOffset;
	int Advance;
	uint16_t Cached;

} FontGlyph_t;

struct TerminalFont 
{
	/* Freetype2 maintains all sorts of useful info itself */
	FT_Face Face;

	/* We'll cache these ourselves */
	int Height;
	int Ascent;
	int Descent;
	int LineSkip;

	/* The font style */
	int FaceStyle;
	int Style;
	int Outline;

	/* Whether kerning is desired */
	int Kerning;

	/* Extra width in glyph bounds for text styles */
	int GlyphOverhang;
	float GlyphItalics;

	/* Information in the font for underlining */
	int UnderlineOffset;
	int UnderlineHeight;

	/* Cache for style-transformed glyphs */
	FontGlyph_t *Current;
	FontGlyph_t Cache[257]; /* 257 is a prime */

	/* We are responsible for closing the font stream */
	int CleanupSource;
	void *Source;

	/* For non-scalable formats, we must remember which font index size */
	int FontSizeFamily;

	/* Really just flags passed into FT_Load_Glyph */
	int Hinting;
};

class Terminal
{
public:
	Terminal(CSurface* Surface);
	~Terminal();

	void NewCommand();
	void PrintLine(const char *Message, ...);

	bool SetSize(int Columns, int Rows);
	bool SetHistorySize(int NumLines);
	bool SetFont(const char *FontPath, int SizePt);
	bool SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);
	bool SetBackgroundColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);

	bool IsAlive() { return m_bIsAlive; }

private:
	/* Private - Functions */
	void CleanupFont(TerminalFont *Font);
	void FlushGlyph(FontGlyph_t* Glyph);
	void FlushCache(TerminalFont* Font);
	FT_Error LoadGlyph(TerminalFont* Font,
		uint16_t Character, FontGlyph_t* Cached, int Want);
	FT_Error FindGlyph(TerminalFont* Font, uint16_t Character, int Want);

	/* Add text to the buffer, this ensures
	 * we can transfer it to history afterwards */ 
	void AddTextBuffer(char *Message, ...);

	/* This actually renders the text in the end
	 * by processng it, and scrolls if necessary */
	void AddText(char *Message, ...);

	/* History functions, for previous / next */
	void HistoryPrevious();
	void HistoryNext();

	/* Process a new line by updating history */
	void NewLine();

	/* Add given character to current line */
	void InsertChar(char Character);

	/* Delete character of current line at current pos */
	int RemoveChar(int Position);

	/* Render the cursor in reverse colors, this will give the
	 * effect of a big fat block that acts as cursor */
	void ShowCursor();

	/* 'Render' the cursor at our current position
	 * in it's normal colors, this will effectively hide it */
	void HideCursor();

	/* Text Rendering 
	 * This is our primary render function, 
	 * it renders text at a specific position on the buffer */
	void RenderText(int AtX, int AtY, const char *Text);

	/* Scroll the terminal by a number of lines
	 * and clear below the scrolled lines */
	void ScrollText(int Lines);

	/* Clear out lines from the given col/row 
	 * so it is ready for new data */
	void ClearFrom(int Column, int Row);

	/* Text Functions */
	void AddCharacter(char Character);

	/* Private - Data */
	TerminalFont *m_pActiveFont;
	FT_Library m_pFreeType;
	Surface *m_pSurface;

	/* History buffer, newest command at last index */
	char **m_pHistory;
	int	m_iHistorySize;
	int	m_iHistoryIndex;

	/* Text buffer, newest line at last index */
	char **m_pBuffer;
	int m_iBufferSize;

	/* Current edit line */
	char *m_pCurrentLine;
	int m_iLineStartX;
	int m_iLineStartY;
	int m_iLinePos;

	/* Private - State */
	char m_szCmdStart[3];
	bool m_bIsAlive;
	int m_iColumns;
	int m_iRows;

	/* Position tracking */
	int m_iCursorPositionX;
	int m_iCursorPositionY;

	/* Advancing */
	int m_iFontHeight;
	int m_iFontWidth;

	/* Colors */
	uint8_t m_cBgR, m_cBgG, m_cBgB, m_cBgA;
	uint8_t m_cFgR, m_cFgG, m_cFgB, m_cFgA;
};
