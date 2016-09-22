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

/* Includes */
#include "Terminal.h"

/* Handy routines for converting from fixed point */
#define FT_FLOOR(x)				((x & -64) / 64)
#define FT_CEIL(x)				(((x + 63) & -64) / 64)

/* Set and retrieve the font style */
#define TTF_STYLE_NORMAL        0x00
#define TTF_STYLE_BOLD          0x01
#define TTF_STYLE_ITALIC        0x02
#define TTF_STYLE_UNDERLINE     0x04
#define TTF_STYLE_STRIKETHROUGH 0x08

/* Constructor 
 * - We use this to instantiate a drawing surface */
Terminal::Terminal()
{
	/* Store params and setup initial values */
	m_bIsAlive = true;
	m_pActiveFont = NULL;

	/* Set default size of 80/25 */
	m_iColumns = 80;
	m_iRows = 25;

	/* Initialize render surface */
	m_pSurface = new Surface();

	/* Initialize a new instance of freetype
	 * so we can use the font engine */
	if (FT_Init_FreeType(&m_pFreeType)) {
		m_bIsAlive = false;
		m_pFreeType = NULL;
	}
}

/* Destructor 
 * - We use this to cleanup all resources allocated */
Terminal::~Terminal()
{
	/* Destroy render surface */
	delete m_pSurface;

	/* Cleanup Font */
	if (m_pActiveFont != NULL)
		CleanupFont(m_pActiveFont);

	/* Cleanup freetype */
	if (m_pFreeType != NULL)
		FT_Done_FreeType(m_pFreeType);
}

/* New Command - Prints the command
 * character and preps for new input */
void Terminal::NewCommand()
{

}

/* Terminal Customization functions
* Use this for setting font, colors
* size etc */
bool Terminal::SetSize(int Columns, int Rows)
{
	/* If we have no font, then we can't adjust accordingly
	 * so lets avoid a resize */
	m_iColumns = Columns;
	m_iRows = Rows;

	/* Sanitize */
	if (m_pActiveFont != NULL)
		m_pSurface->Resize(Columns * (m_pActiveFont->Face->size->metrics.max_advance >> 6),
						   Rows * m_pActiveFont->Height);
}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetHistorySize(int NumLines)
{

}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetFont(const char *FontPath, int SizePt)
{
	/* Load the font file into a buffer 
	 * and retrieve size of file */
	FILE *Fp = fopen(FontPath, "ra");
	size_t Size = ftell(Fp);
	TerminalFont *Font = NULL;
	void *DataBuffer = NULL;
	FT_CharMap CmFound = 0;

	/* Sanitize the stuff, ftell won't
	 * crash on null, so we can wait till here */
	if (Fp == NULL || Size == 0)
		return false;

	/* Spool back */
	rewind(Fp);

	/* Read the font file */
	fread(DataBuffer, 1, Size, Fp);
	fclose(Fp);

	/* If there was an old font, clean it up */
	if (m_pActiveFont != NULL) {
		CleanupFont(m_pActiveFont);
		m_pActiveFont = NULL;
	}

	/* Instantiate a new font object */
	Font = (TerminalFont*)malloc(sizeof(TerminalFont));
	memset(Font, 0, sizeof(TerminalFont));

	/* Set up the information about stream, 
	 * we need to free it */
	Font->CleanupSource = 1;
	Font->Source = DataBuffer;

	/* Instantiate a new font object 
	 * in the FT library */
	if (FT_New_Memory_Face(m_pFreeType,
		(FT_Byte*)DataBuffer, Size, 0, &Font->Face)) {
		CleanupFont(Font);
		return false;
	}

	/* Set charmap for loaded font */
	for (int i = 0; i < Font->Face->num_charmaps; i++) {
		FT_CharMap Cm = Font->Face->charmaps[i];
		if ((Cm->platform_id == 3 && Cm->encoding_id == 1) /* Windows Unicode */
			|| (Cm->platform_id == 3 && Cm->encoding_id == 0) /* Windows Symbol */
			|| (Cm->platform_id == 2 && Cm->encoding_id == 1) /* ISO Unicode */
			|| (Cm->platform_id == 0)) { /* Apple Unicode */
			CmFound = Cm;
			break;
		}
	}
	if (CmFound) {
		/* If this fails, continue using the default charmap */
		FT_Set_Charmap(Font->Face, CmFound);
	}

	/* Make sure that our font face is scalable (global metrics) */
	if (FT_IS_SCALABLE(Font->Face)) {
		/* Set the character size and use default DPI (72) */
		if (FT_Set_Char_Size(Font->Face, 0, SizePt * 64, 0, 0)) {
			CleanupFont(Font);
			return false;
		}

		/* Get the scalable font metrics for this font */
		FT_Fixed Scale = Font->Face->size->metrics.y_scale;
		Font->Ascent = FT_CEIL(FT_MulFix(Font->Face->ascender, Scale));
		Font->Descent = FT_CEIL(FT_MulFix(Font->Face->descender, Scale));
		Font->Height = Font->Ascent - Font->Descent + /* baseline */ 1;
		Font->LineSkip = FT_CEIL(FT_MulFix(Font->Face->height, Scale));
		Font->UnderlineOffset = FT_FLOOR(FT_MulFix(Font->Face->underline_position, Scale));
		Font->UnderlineHeight = FT_FLOOR(FT_MulFix(Font->Face->underline_thickness, Scale));
	}
	else {
		/* Non-scalable font case.  ptsize determines which family
		* or series of fonts to grab from the non-scalable format.
		* It is not the point size of the font.
		* */
		if (SizePt >= Font->Face->num_fixed_sizes)
			SizePt = Font->Face->num_fixed_sizes - 1;
		Font->FontSizeFamily = SizePt;
		int error = FT_Set_Pixel_Sizes(Font->Face,
			Font->Face->available_sizes[SizePt].width,
			Font->Face->available_sizes[SizePt].height);

		/* With non-scalale fonts, Freetype2 likes to fill many of the
		* font metrics with the value of 0.  The size of the
		* non-scalable fonts must be determined differently
		* or sometimes cannot be determined.
		* */
		Font->Ascent = Font->Face->available_sizes[SizePt].height;
		Font->Descent = 0;
		Font->Height = Font->Face->available_sizes[SizePt].height;
		Font->LineSkip = FT_CEIL(Font->Ascent);
		Font->UnderlineOffset = FT_FLOOR(Font->Face->underline_position);
		Font->UnderlineHeight = FT_FLOOR(Font->Face->underline_thickness);
	}

	if (Font->UnderlineHeight < 1) {
		Font->UnderlineHeight = 1;
	}

	/* Initialize the font face style */
	Font->FaceStyle = TTF_STYLE_NORMAL;
	if (Font->Face->style_flags & FT_STYLE_FLAG_BOLD) {
		Font->FaceStyle |= TTF_STYLE_BOLD;
	}
	if (Font->Face->style_flags & FT_STYLE_FLAG_ITALIC) {
		Font->FaceStyle |= TTF_STYLE_ITALIC;
	}

	/* Set the default font style */
	Font->Style = Font->FaceStyle;
	Font->Outline = 0;
	Font->Kerning = 1;
	Font->GlyphOverhang = Font->Face->size->metrics.y_ppem / 10;
	/* x offset = cos(((90.0-12)/360)*2*M_PI), or 12 degree angle */
	Font->GlyphItalics = 0.207f;
	Font->GlyphItalics *= Font->Face->height;

	/* Update active font */
	m_pActiveFont = Font;

	/* Done! 
	 * But the last thing to do here is to update the 
	 * size of the terminal */
	return SetSize(m_iColumns, m_iRows);
}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a)
{

}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetBackgroundColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a)
{

}
