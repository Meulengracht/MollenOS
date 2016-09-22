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
* - Project Alumnious (First C++ Project)
*/

#ifndef _ALUMNIOUS_TERMINAL_H_
#define _ALUMNIOUS_TERMINAL_H_

/* Includes */
#include <cstddef>
#include <cstdlib>
#include <cstdio>

/* System Includes */
#ifdef MOLLENOS
#include "MollenOS/Surface.h"
#endif

/* LibFreeType
* We use that for font-rendering */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_GLYPH_H
#include FT_TRUETYPE_IDS_H

/* Cached glyph information */
typedef struct FontGlyph {
	int stored;
	FT_UInt index;
	FT_Bitmap bitmap;
	FT_Bitmap pixmap;
	int minx;
	int maxx;
	int miny;
	int maxy;
	int yoffset;
	int advance;
	uint16_t cached;

} FontGlyph_t;

/* The structure used to hold internal 
 * font information */
struct TerminalFont {
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

/* Class */
class Terminal
{
public:
	Terminal();
	~Terminal();

	/* New Command - Prints the command
	 * character and preps for new input */
	void NewCommand();

	/* Terminal Customization functions 
	 * Use this for setting font, colors
	 * size etc */
	bool SetSize(int Columns, int Rows);
	bool SetHistorySize(int NumLines);
	bool SetFont(const char *FontPath, int SizePt);
	bool SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);
	bool SetBackgroundColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);

	/* Get whether or not the terminal has been...
	 * - TERMINATED! */
	bool IsAlive() { return m_bIsAlive; }

private:
	/* Private - Functions */
	void CleanupFont(TerminalFont *Font);
	void FlushGlyph(FontGlyph_t* Glyph);
	void FlushCache(TerminalFont* Font);
	FT_Error LoadGlyph(TerminalFont* Font,
		uint16_t Character, FontGlyph_t* Cached, int Want);
	FT_Error FindGlyph(TerminalFont* Font, uint16_t Character, int Want);

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

	/* Private - State */
	bool m_bIsAlive;
	int m_iColumns;
	int m_iRows;
};


#endif //!_ALUMNIOUS_TERMINAL_H_
