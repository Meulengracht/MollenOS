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
typedef struct GlyphCache {
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
} c_glyph;

/* The structure used to hold internal 
 * font information */
struct TerminalFont {
	/* Freetype2 maintains all sorts of useful info itself */
	FT_Face face;

	/* We'll cache these ourselves */
	int height;
	int ascent;
	int descent;
	int lineskip;

	/* The font style */
	int face_style;
	int style;
	int outline;

	/* Whether kerning is desired */
	int kerning;

	/* Extra width in glyph bounds for text styles */
	int glyph_overhang;
	float glyph_italics;

	/* Information in the font for underlining */
	int underline_offset;
	int underline_height;

	/* Cache for style-transformed glyphs */
	c_glyph *current;
	c_glyph cache[257]; /* 257 is a prime */

	/* We are responsible for closing the font stream */
	int freesrc;
	FT_Open_Args args;

	/* For non-scalable formats, we must remember which font index size */
	int font_size_family;

	/* really just flags passed into FT_Load_Glyph */
	int hinting;
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
	bool SetHistorySize(size_t NumCharacters);
	bool SetFont(const char *FontPath, int SizePixels);
	bool SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);
	bool SetBackgroundColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);

	/* Get whether or not the terminal has been...
	 * - TERMINATED! */
	bool IsAlive() { return m_bIsAlive; }

private:
	/* Private - Functions */

	/* Private - Data */
	FT_Library m_pFreeType;
	Surface *m_pSurface;

	/* Private - State */
	bool m_bIsAlive;
};


#endif //!_ALUMNIOUS_TERMINAL_H_
