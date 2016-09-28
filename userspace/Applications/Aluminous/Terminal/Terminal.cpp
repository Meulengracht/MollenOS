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

/* C-Library */
#include <os/VirtualKeyCodes.h>
#include <ds/mstring.h>
#include <cmath>
#include <cctype>

/* FIXME: Right now we assume the gray-scale renderer Freetype is using
supports 256 shades of gray, but we should instead key off of num_grays
in the result FT_Bitmap after the FT_Render_Glyph() call. */
#define NUM_GRAYS       256

/* Handy routines for converting from fixed point */
#define FT_FLOOR(x)				((x & -64) / 64)
#define FT_CEIL(x)				(((x + 63) & -64) / 64)

/* Set and retrieve the font style */
#define TTF_STYLE_NORMAL        0x00
#define TTF_STYLE_BOLD          0x01
#define TTF_STYLE_ITALIC        0x02
#define TTF_STYLE_UNDERLINE     0x04
#define TTF_STYLE_STRIKETHROUGH 0x08

/* Handle a style only if the font does not already handle it */
#define TTF_HANDLE_STYLE_BOLD(font) (((font)->Style & TTF_STYLE_BOLD) && \
                                    !((font)->FaceStyle & TTF_STYLE_BOLD))
#define TTF_HANDLE_STYLE_ITALIC(font) (((font)->Style & TTF_STYLE_ITALIC) && \
                                      !((font)->FaceStyle & TTF_STYLE_ITALIC))
#define TTF_HANDLE_STYLE_UNDERLINE(font) ((font)->Style & TTF_STYLE_UNDERLINE)
#define TTF_HANDLE_STYLE_STRIKETHROUGH(font) ((font)->Style & TTF_STYLE_STRIKETHROUGH)

#define CACHED_METRICS  0x10
#define CACHED_BITMAP   0x01
#define CACHED_PIXMAP   0x02

/* ZERO WIDTH NO-BREAKSPACE (Unicode byte order mark) */
#define UNICODE_BOM_NATIVE  0xFEFF
#define UNICODE_BOM_SWAPPED 0xFFFE

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

	/* Fill out the cmd */
	m_szCmdStart[0] = '$';
	m_szCmdStart[1] = ' ';
	m_szCmdStart[2] = '\0';

	/* Initialize render surface */
	m_pSurface = new Surface();

	/* Initialize a new instance of freetype
	 * so we can use the font engine */
	if (FT_Init_FreeType(&m_pFreeType)) {
		m_bIsAlive = false;
		m_pFreeType = NULL;
	}

	/* Initialize lines */
	m_pCurrentLine = (char*)malloc(1024 * sizeof(char));
	m_pCurrentLine[0] = '\0';
	m_iLineStartX = 0;
	m_iLineStartY = 0;
	m_iLinePos = 0;

	/* History allocation */
	m_iHistorySize = 25;
	m_iHistoryIndex = m_iHistorySize - 1;
	m_pHistory = (char**)malloc(m_iHistorySize * sizeof(char*));

	/* Reset history */
	for (int i = 0; i < m_iHistorySize; i++)
		m_pHistory[i] = NULL;

	/* Text buffer Allocation */
	m_iBufferSize = 250;
	m_pBuffer = (char**)malloc(m_iBufferSize * sizeof(char*));

	/* Reset buffer */
	for (int i = 0; i < m_iBufferSize; i++)
		m_pBuffer[i] = NULL;
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
	/* Keep track */
	bool Reading = true;

	/* Print a new command */
	while (Reading) {

		/* Read a new character from input */
		VKey Character = (VKey)getchar();

		/* Switch */ 
		if (Character == VK_BACK) {
			if ((m_iLinePos > 0) && (m_pCurrentLine[m_iLinePos - 1] == '\t')) {
				if (!RemoveChar(m_iLinePos - 1)) {
					HideCursor();
					m_iCursorPositionX -= 4;

					/* Sanity check boundary on y */
					if (m_iCursorPositionX < 0) {
						m_iCursorPositionX += m_iColumns;
						m_iCursorPositionY--;
					}

					/* If we were in the middle of the line, 
					 * we have to render the rest of the line */
					if (m_iLinePos < strlen(m_pCurrentLine)) 
					{
						/* Save current cursor */
						int SavedX = m_iCursorPositionX, SavedY = m_iCursorPositionY;

						for (int i = m_iLinePos; i<strlen(m_pCurrentLine); i++)
							AddCharacter(m_pCurrentLine[i]); //AddChar
						for (int i = 0; i < 4; i++)
							AddCharacter(' '); //AddChar

						/* Restore position */
						m_iCursorPositionX = SavedX;
						m_iCursorPositionY = SavedY;
					}

					/* Render Cursor */
					ShowCursor();
				}
			}
			else if (RemoveChar(m_iLinePos - 1) == 0) {
				
				HideCursor();
				
				if (m_iCursorPositionX > 0)
					m_iCursorPositionX--;
				else {
					m_iCursorPositionX = m_iColumns - 1;
					m_iCursorPositionY--;
				}

				/* If we were in the middle of the line, we have to render the ret of the line */
				if (m_iLinePos < strlen(m_pCurrentLine)) {
					
					/* Save current cursor */
					int SavedX = m_iCursorPositionX, SavedY = m_iCursorPositionY;

					/* Add the characters */
					for (int i = m_iLinePos; i<strlen(m_pCurrentLine); i++)
						AddCharacter(m_pCurrentLine[i]);
					AddCharacter(' ');
					
					/* Restore position */
					m_iCursorPositionX = SavedX;
					m_iCursorPositionY = SavedY;
				}
				
				/* Render Cursor */
				ShowCursor();
			}
		}
		else if (Character == VK_ENTER) {
			HideCursor();
			NewLine();
			break;
		}
		else if (Character == VK_UP) {
			HistoryPrevious();
		}
		else if (Character == VK_DOWN) {
			HistoryNext();
		}
		else if (Character == VK_LEFT) {
			if (m_iLinePos > 0) {
				HideCursor();
				m_iLinePos--;
				int dx = 1;
				if (m_pCurrentLine[m_iLinePos] == '\t')
					dx = 4;
				m_iCursorPositionX -= dx;
				if (m_iCursorPositionX < 0) {
					m_iCursorPositionX += m_iColumns;
					m_iCursorPositionY--;
				}
				
				/* Render Cursor */
				ShowCursor();
			}
		}
		else if (Character == VK_RIGHT) {
			if (m_iLinePos < strlen(m_pCurrentLine)) {
				HideCursor();
				int dx = +1;
				if ((m_iLinePos < strlen(m_pCurrentLine)) && m_pCurrentLine[m_iLinePos] == '\t')
					dx = 4;
				m_iCursorPositionX += dx;
				if (m_iCursorPositionX >= m_iColumns) {
					m_iCursorPositionX -= m_iColumns;
					m_iCursorPositionY++;
				}
				m_iLinePos++;
				
				/* Render Cursor */
				ShowCursor();
			}
		}
		else
		{
			/* ASCII ? */
			if (isalpha(Character)) {
				AddCharacter((char)Character);
			}
		}
	}
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

	/* Done! */
	return true;
}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetHistorySize(int NumLines)
{
	return true;
}

/* Yank previous history line */
void Terminal::HistoryPrevious()
{
	/* Sanitize if enough history available */
	if ((m_iHistoryIndex <= 0) || (m_pHistory[m_iHistoryIndex - 1] == 0))
		return;

	/* Save current entry within history */
	if (m_iHistoryIndex == m_iHistorySize - 1) {
		strcpy(m_pHistory[m_iHistorySize - 1], m_pCurrentLine);
	}

	/* Copy history to entry */
	m_iHistoryIndex--;
	strcpy(m_pCurrentLine, m_pHistory[m_iHistoryIndex]);
	m_iLinePos = strlen(m_pCurrentLine);

	/* Refresh terminal */
	ClearFrom(m_iLineStartX, m_iLineStartY);
	m_iCursorPositionX = m_iLineStartX;
	m_iCursorPositionY = m_iLineStartY;
	AddText(m_pCurrentLine);
}

/* Yank next history line */
void Terminal::HistoryNext()
{
	/* Make sure we have enough room for next */
	if (m_iHistoryIndex >= m_iHistorySize - 1)
		return;

	/* Copy history to entry */
	m_iHistoryIndex++;
	strcpy(m_pCurrentLine, m_pHistory[m_iHistoryIndex]);
	m_iLinePos = strlen(m_pCurrentLine);

	/* Refresh terminal */
	ClearFrom(m_iLineStartX, m_iLineStartY);
	m_iCursorPositionX = m_iLineStartX;
	m_iCursorPositionY = m_iLineStartY;
	AddText(m_pCurrentLine);
}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetFont(const char *FontPath, int SizePt)
{
	/* Load the font file into a buffer 
	 * and retrieve size of file */
	FILE *Fp = fopen(FontPath, "rab");
	size_t Size = ftell(Fp);
	TerminalFont *Font = NULL;
	void *DataBuffer = NULL;
	FT_CharMap CmFound = 0;

	/* Sanitize the stuff, ftell won't
	 * crash on null, so we can wait till here */
	if (Fp == NULL || Size == 0) {
		return false;
	}

	/* Spool back */
	rewind(Fp);

	/* Read the font file */
	MollenOSSystemLog("Read %u bytes out of %u",
		fread(DataBuffer, 1, Size, Fp), Size);
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
		MollenOSSystemLog("Failed to create a new FT_Memory_Face");
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

	/* Update stats */
	m_iFontHeight = Font->Height;
	FindGlyph(m_pActiveFont, 'A', CACHED_METRICS);
	m_iFontWidth = Font->Current->Advance;

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
	/* Update color */
	m_cFgR = r;
	m_cFgG = g;
	m_cFgB = b;
	m_cFgA = a;

	/* Done! */
	return true;
}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetBackgroundColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a)
{
	/* Update color */
	m_cBgR = r;
	m_cBgG = g;
	m_cBgB = b;
	m_cBgA = a;

	/* Done! */
	return true;
}

/* Print raw messages to the terminal,
 * this could be a header or a warning message */
void Terminal::PrintLine(const char *Message, ...)
{
	/* VA */
	va_list Args;
	char *Line = (char*)malloc(1024 * sizeof(char));

	/* Combine into buffer */
	va_start(Args, Message);
	vsnprintf(Line, 1024 - 1, Message, Args);
	va_end(Args);

	/* Now actually add the text */
	AddTextBuffer(Line);
	AddText(Line);

	/* Update line start coords */
	m_iLineStartX = m_iCursorPositionX;
	m_iLineStartY = m_iCursorPositionY;

	/* Cleanup line */
	free(Line);
}

/* This actually renders the text in the end
 * by processng it, and scrolls if necessary */
void Terminal::AddText(char *Message, ...)
{
	/* VA */
	static char Line[1024];
	va_list Args;
	int i = 0;

	/* Sanitize params */
	if (Message == NULL)
		return;

	/* Combine */
	va_start(Args, Message);
	vsnprintf(Line, 1024 - 1, Message, Args);
	va_end(Args);

	/* Iterate characters */
	while (Line[i] != 0) 
	{
		/* Extract character */
		int c = Line[i++];
		int cc = c;
		int dx = 1;

		/* Newline? */
		if (c == '\n') {
			m_iCursorPositionX = 0;
			m_iCursorPositionY++;

			/* Boundary check, maybe scroll */
			if (m_iCursorPositionY >= m_iRows) {
				ScrollText(1);
				m_iCursorPositionY--;
			}

			/* Done, go to next */
			continue;
		}

		/* Taburator, override in that case
		 * since \t is not printable */
		if (c == '\t') {
			cc = ' ';
			dx = 4;
		}

		/* Iterate how many chars we need to output */
		for (int j = 0; j < dx; j++)  {
			AddCharacter(cc);
		}
	}
}

/* Shorthand for the above function, instead of 
 * adding an entire message, just add a single character */
void Terminal::AddCharacter(char Character)
{
	/* Buffer */
	char cBuffer[] = " ";

	/* Update */
	cBuffer[0] = Character;

	/* Render the character */
	RenderText(m_iCursorPositionX * m_iFontWidth,
		m_iCursorPositionY * m_iFontHeight, cBuffer);
	m_iCursorPositionX++;

	/* Boundary check on width */
	if (m_iCursorPositionX >= m_iColumns) {
		m_iCursorPositionX -= m_iColumns;
		m_iCursorPositionY++;

		/* Boundary check, maybe scroll */
		if (m_iCursorPositionY >= m_iRows) {
			ScrollText(1);
			m_iCursorPositionY--;
		}
	}
}

/* Add text to the buffer, this ensures
 * we can transfer it to history afterwards */
void Terminal::AddTextBuffer(char *Message, ...)
{
	/* VA */
	va_list Args;

	/* Sanitize params */
	if (Message == NULL)
		return;

	/* Sanitize the buffer first index, 
	 * we need the space */
	if (m_pBuffer[0] != NULL)
		free(m_pBuffer[0]);

	/* Iterate and move the buffers */
	for (int i = 1; i < m_iBufferSize; i++)
		m_pBuffer[i - 1] = m_pBuffer[i];

	/* Allocate a new buffer */
	m_pBuffer[m_iBufferSize - 1] = (char*)malloc(1024 * sizeof(char));
	if (m_pBuffer[m_iBufferSize - 1] == NULL) {
		/* Out of memory probably */
		return;
	}

	/* Combine it into buffer */
	va_start(Args, Message);
	vsnprintf(m_pBuffer[m_iBufferSize - 1], 1024 - 1, Message, Args);
	va_end(Args);

	/* Null terminate the new string */
	m_pBuffer[m_iBufferSize - 1][1024 - 1] = '\0';
}

/* Process a new line by updating history */
void Terminal::NewLine()
{
	/* Store a copy within history
	do not store blank lines
	do not store line if it is just before in history
	*/
	if ((strcmp(m_pCurrentLine, "") != 0) &&
		((m_pHistory[m_iHistorySize - 2] == 0) ||
		(strcmp(m_pCurrentLine, m_pHistory[m_iHistorySize - 2]) != 0)))
	{
		/* Variables */
		int i;

		/* Sanitize the first member of history */
		if (m_pHistory[0])
			free(m_pHistory[0]);

		/* Allocate a new history entry if 
		 * it isn't already allocated */
		if (!m_pHistory[m_iHistorySize - 1]) {
			m_pHistory[m_iHistorySize - 1] = (char*)malloc(1024 * sizeof(char));
			if (m_pHistory[m_iHistorySize - 1] == NULL) {
				/* Out of memory */
				return;
			}
		}

		/* Move history up */
		strcpy(m_pHistory[m_iHistorySize - 1], m_pCurrentLine);
		for (i = 1; i< m_iHistorySize; i++)
			m_pHistory[i - 1] = m_pHistory[i];

		/* Allocate a history entry */
		m_pHistory[m_iHistorySize - 1] = (char*)malloc(1024 * sizeof(char));
		if (m_pHistory[m_iHistorySize - 1] == NULL) {
			/* Out of memory */
			return;
		}

		/* Null terminate history */
		m_pHistory[0] = '\0';
		m_iHistoryIndex = m_iHistorySize - 1;
	}


	/* Update cursor position */
	m_iCursorPositionX = 0;
	m_iCursorPositionY++;

	/* Boundary Check */
	if (m_iCursorPositionY >= m_iRows) {
		ScrollText(1);
		m_iCursorPositionY--;
	}

	/* Add trailing '\n' to go to next line */
	InsertChar('\n');

	/* Update text buffer (with '\n') */
	AddTextBuffer(m_pCurrentLine);

	/* Misc. updates */
	m_pCurrentLine[0] = '\0';
	m_iLinePos = 0;
	m_iLineStartX = m_iCursorPositionX;
	m_iLineStartY = m_iCursorPositionY;
}

/* 'Render' the cursor at our current position 
 * in it's normal colors, this will effectively hide it */
void Terminal::HideCursor()
{
	/* Variables */
	char cBuffer[] = " ";
	char c = ' ';

	/* Determine where to put the cursor in case
	 * it's to far */
	if (m_iLinePos < strlen(m_pCurrentLine)) {
		c = m_pCurrentLine[m_iLinePos];
		if (c == '\t')
			c = ' ';
	}

	/* Update buffer */
	cBuffer[0] = c;

	/* 'Render' the cursor */
	RenderText(m_iCursorPositionX * m_iFontWidth,
		m_iCursorPositionY * m_iFontHeight, cBuffer);
}

/* Render the cursor in reverse colors, this will give the
 * effect of a big fat block that acts as cursor */
void Terminal::ShowCursor()
{
	/* Get foreground and bg colors */
	char cBuffer[] = " ";
	char c = ' ';
	int r = 0;

	/* Store colors */
	uint8_t BgR = m_cBgR; uint8_t BgG = m_cBgG;
	uint8_t BgB = m_cBgB; uint8_t BgA = m_cBgA;

	uint8_t FgR = m_cFgR; uint8_t FgG = m_cFgG;
	uint8_t FgB = m_cFgB; uint8_t FgA = m_cFgA;

	/* Setup cursor colors, by swapping fg/bg */
	m_cFgR = BgR; m_cFgG = BgG; m_cFgB = BgB; m_cFgA = BgA;
	m_cBgR = FgR; m_cBgG = FgG; m_cBgB = FgB; m_cBgA = FgA;

	/* Where should we render it? */
	if (m_iLinePos < strlen(m_pCurrentLine)) {
		c = m_pCurrentLine[m_iLinePos];
		if (c == '\t')
			c = ' ';
	}

	/* Update buffer */
	cBuffer[0] = c;

	/* Render the cursor */
	RenderText(m_iCursorPositionX * m_iFontWidth,
		m_iCursorPositionY * m_iFontHeight, cBuffer);

	/* Restore colors */
	m_cFgR = FgR; m_cFgG = FgG; m_cFgB = FgB; m_cFgA = FgA;
	m_cBgR = BgR; m_cBgG = BgG; m_cBgB = BgB; m_cBgA = BgA;
}

/* Add given character to current line */
void Terminal::InsertChar(char Character)
{
	/* Sanitize length */
	if (strlen(m_pCurrentLine) >= (1024 - 1))
		return;

	/* Move the characters one space */
	memmove(&m_pCurrentLine[m_iLinePos + 1],
		&m_pCurrentLine[m_iLinePos],
		strlen(m_pCurrentLine) - m_iLinePos + 1);

	/* Insert the new character */
	m_pCurrentLine[m_iLinePos++] = Character;
}

/* Delete character of current line at current pos */
int Terminal::RemoveChar(int Position)
{
	/* Sanitize some params and the current line */
	if (Position < 0 
		|| strlen(m_pCurrentLine) == 0)
		return -1;

	/* Move the line one space and simoultanously override the
	 * character spot we want to delete */
	memmove(&m_pCurrentLine[Position], &m_pCurrentLine[Position + 1],
		strlen(m_pCurrentLine) - Position + 1);

	/* Reduce line position */
	m_iLinePos--;

	/* Done! */
	return 0;
}

/* Clear out lines from the given col/row 
 * so it is ready for new data */
void Terminal::ClearFrom(int Column, int Row)
{
	/* Make sure columns are valid */
	if ((Column <= (m_iColumns - 1)) && (Row <= (m_iRows - 1))) 
	{
		/* Create dest area */
		Rect_t Destination = {
			Column * m_iFontWidth,
			Row * m_iFontHeight,
			(m_iRows - Row) * m_iFontHeight,
			(m_iColumns - Column) * m_iFontWidth
		};

		/* Clear */
		m_pSurface->Clear(m_pSurface->GetColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA), &Destination);
	}

	/* Make sure row is valid */
	if (Row < (m_iRows - 1)) 
	{
		/* Create dest area */
		Rect_t Destination = { 
			0, 
			(Row + 1) * m_iFontHeight,
			(m_iRows - Row - 1) * m_iFontHeight,
			m_iColumns * m_iFontWidth
		};

		/* Clear */
		m_pSurface->Clear(m_pSurface->GetColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA), &Destination);
	}
}

/* Scroll the terminal by a number of lines
 * and clear below the scrolled lines */
void Terminal::ScrollText(int Lines)
{
	/* Sanitize limits */
	if (Lines >= m_iRows)
		Lines = m_iRows;

	/* We do a memcpy */
	if (Lines < m_iRows) 
	{
		/* Calculate source pointer and how much to copy */
		uint8_t *SourcePtr = (uint8_t*)m_pSurface->DataPtr(0, Lines * m_iFontHeight);
		size_t BytesToCopy = m_pSurface->GetDimensions()->w * 
			(m_pSurface->GetDimensions()->h - Lines * m_iFontHeight) * 4;

		/* Calculate destination pointer */
		uint8_t *DestPtr = (uint8_t*)m_pSurface->DataPtr(0, 0);

		/* Copy */
		memcpy(DestPtr, SourcePtr, BytesToCopy);
	}

	/* Clear remaining lines */
	ClearFrom(0, m_iRows - Lines);
}

/* Text Rendering 
 * This is our primary render function, 
 * it renders text at a specific position on the buffer 
 * TODO ERASE */
void Terminal::RenderText(int AtX, int AtY, const char *Text)
{
	/* Variables for state tracking, 
	 * and formatting, and rendering */
	bool First;
	int xStart;
	int Width;
	//int Height;
	uint8_t *Source;
	uint8_t *Destination;
	uint8_t *DestCheck;
	int Row, Col;
	FontGlyph_t *Glyph;

	/* Iterating Text */
	MString_t *mText;
	size_t ItrLength = 0;
	char *mItr = NULL;

	/* FT Variables and stuff for text */
	FT_Bitmap *Current;
	FT_Error Error;
	FT_Long UseKerning;
	FT_UInt PreviousIndex = 0;

	/* Sanity */
	if (m_pActiveFont == NULL) {
		return;
	}

	/* Adding bound checking to avoid all kinds of memory corruption errors
	 * that may occur. */
	DestCheck = (uint8_t*)m_pSurface->DataPtr() + 4 * m_pSurface->GetDimensions()->h;

	/* check kerning */
	UseKerning = FT_HAS_KERNING(m_pActiveFont->Face) && m_pActiveFont->Kerning;

	/* Initialise for the loop */
	mText = MStringCreate((void*)Text, Latin1);
	First = true;
	xStart = 0;

	/* Load and render each character */
	while (true) 
	{
		/* Get next character of text-string */
		uint16_t Character = (uint16_t)MStringIterate(mText, &mItr, &ItrLength);
		if (Character == UNICODE_BOM_NATIVE 
			|| Character == UNICODE_BOM_SWAPPED) {
			continue;
		}

		/* End of string? */
		if (Character == MSTRING_EOS)
			break;

		/* Lookup glyph for the character, if we have none, 
		 * we bail! */
		Error = FindGlyph(m_pActiveFont, Character, CACHED_METRICS | CACHED_BITMAP);
		if (Error) {
			return;
		}

		/* Shorthand some stuff */
		Glyph = m_pActiveFont->Current;
		Current = &Glyph->Bitmap;

		/* Ensure the width of the pixmap is correct. On some cases,
		 * freetype may report a larger pixmap than possible.*/
		Width = Current->width;
		if (m_pActiveFont->Outline <= 0 && Width > Glyph->MaxX - Glyph->MinX) {
			Width = Glyph->MaxX - Glyph->MinX;
		}

		/* do kerning, if possible AC-Patch */
		if (UseKerning && PreviousIndex && Glyph->Index) {
			FT_Vector Delta;
			FT_Get_Kerning(m_pActiveFont->Face, PreviousIndex, Glyph->Index, FT_KERNING_DEFAULT, &Delta);
			xStart += Delta.x >> 6;
		}

		/* Compensate for wrap around bug with negative minx's */
		if (First && (Glyph->MinX < 0)) {
			xStart -= Glyph->MinX;
		}

		/* No longer the first! */
		First = false;

		/* Iterate over rows and actually draw it */
		for (Row = 0; Row < Current->rows; ++Row) {
			/* Make sure we don't go either over, or under the
			 * limit */
			if (Row + Glyph->yOffset < 0
				|| Row + Glyph->yOffset >= m_pSurface->GetDimensions()->h) {
				continue;
			}
			
			/* Calculate destination */
			Destination = (uint8_t*)m_pSurface->DataPtr(AtX, AtY) +
				((Row + Glyph->yOffset) * 4) + xStart + Glyph->MinX;
			Source = Current->buffer + Row * Current->pitch;

			/* Loop ! */
			for (Col = Width; Col > 0 && Destination < DestCheck; --Col) {
				*Destination++ |= *Source++;
			}
		}

		/* Advance, and handle bold style if neccassary */
		xStart += Glyph->Advance;
		if (TTF_HANDLE_STYLE_BOLD(m_pActiveFont)) {
			xStart += m_pActiveFont->GlyphOverhang;
		}

		/* Set new previous index */
		PreviousIndex = Glyph->Index;
	}

	/* Handle the underline style 
	if (TTF_HANDLE_STYLE_UNDERLINE(font)) {
		row = TTF_underline_top_row(font);
		TTF_drawLine_Solid(font, textbuf, row);
	} */

	/* Handle the strikethrough style
	if (TTF_HANDLE_STYLE_STRIKETHROUGH(font)) {
		row = TTF_strikethrough_top_row(font);
		TTF_drawLine_Solid(font, textbuf, row);
	}  */

	/* Cleanup */
	MStringDestroy(mText);
}

/* This cleans up a stored glyph and 
 * releases all associated resources */
void Terminal::FlushGlyph(FontGlyph_t* Glyph)
{
	/* Clear out data */
	Glyph->Stored = 0;
	Glyph->Index = 0;
	Glyph->Cached = 0;

	/* Free buffer if necessary */
	if (Glyph->Bitmap.buffer) {
		free(Glyph->Bitmap.buffer);
		Glyph->Bitmap.buffer = 0;
	}

	/* Free the pixmap if necessary */
	if (Glyph->Pixmap.buffer) {
		free(Glyph->Pixmap.buffer);
		Glyph->Pixmap.buffer = 0;
	}
}

/* This does the same as flush glyph, 
 * except we flush the entire cache of glyphs */
void Terminal::FlushCache(TerminalFont* Font)
{
	/* Calculate size */
	int Size = sizeof(Font->Cache) / sizeof(Font->Cache[0]);

	/* Iterate and call FlushGlyph */
	for (int i = 0; i < Size; ++i) {
		if (Font->Cache[i].Cached) {
			FlushGlyph(&Font->Cache[i]);
		}
	}
}

/* This function cleans up a font object 
 * and flushes all resources associated */
void Terminal::CleanupFont(TerminalFont *Font)
{
	/* Sanity */
	if (Font == NULL) {
		return;
	}

	/* Flush Cache */
	FlushCache(Font);

	/* Flush Source */
	if (Font->CleanupSource) {
		free(Font->Source);
	}

	/* Cleanup struct */
	free(Font);
}

/* Loads a glyph into the cache of the font */
FT_Error Terminal::LoadGlyph(TerminalFont* Font,
	uint16_t Character, FontGlyph_t* Cached, int Want)
{
	/* A looot of FT variables thanks! */
	FT_Face Face;
	FT_Error Error;
	FT_GlyphSlot Glyph;
	FT_Glyph_Metrics* Metrics;
	FT_Outline* Outline;

	/* Do some immediate sanity checks */
	if (!Font || !Font->Face) {
		return FT_Err_Invalid_Handle;
	}

	/* Short hand */
	Face = Font->Face;

	/* Start out by getting the character index
	 * if we haven't */
	if (!Cached->Index) {
		Cached->Index = FT_Get_Char_Index(Face, Character);
	}

	/* Load the glyph */
	Error = FT_Load_Glyph(Face, Cached->Index, FT_LOAD_DEFAULT | Font->Hinting);
	
	/* If anything wrong happened, return */
	if (Error) {
		return Error;
	}

	/* Get our glyph shortcuts */
	Glyph = Face->glyph;
	Metrics = &Glyph->metrics;
	Outline = &Glyph->outline;

	/* Get the glyph metrics if desired */
	if ((Want & CACHED_METRICS) && !(Cached->Stored & CACHED_METRICS)) {
		if (FT_IS_SCALABLE(Face)) {
			/* Get the bounding box */
			Cached->MinX = FT_FLOOR(Metrics->horiBearingX);
			Cached->MaxX = FT_CEIL(Metrics->horiBearingX + Metrics->width);
			Cached->MaxY = FT_FLOOR(Metrics->horiBearingY);
			Cached->MinY = Cached->MaxY - FT_CEIL(Metrics->height);
			Cached->yOffset = Font->Ascent - Cached->MaxY;
			Cached->Advance = FT_CEIL(Metrics->horiAdvance);
		}
		else {
			/* Get the bounding box for non-scalable format.
			* Again, freetype2 fills in many of the font metrics
			* with the value of 0, so some of the values we
			* need must be calculated differently with certain
			* assumptions about non-scalable formats.
			* */
			Cached->MinX = FT_FLOOR(Metrics->horiBearingX);
			Cached->MaxX = FT_CEIL(Metrics->horiBearingX + Metrics->width);
			Cached->MaxY = FT_FLOOR(Metrics->horiBearingY);
			Cached->MinY = Cached->MaxY - FT_CEIL(Face->available_sizes[Font->FontSizeFamily].height);
			Cached->yOffset = 0;
			Cached->Advance = FT_CEIL(Metrics->horiAdvance);
		}

		/* Adjust for bold and italic text */
		if (TTF_HANDLE_STYLE_BOLD(Font)) {
			Cached->MaxX += Font->GlyphOverhang;
		}
		if (TTF_HANDLE_STYLE_ITALIC(Font)) {
			Cached->MaxX += (int)ceilf(Font->GlyphItalics);
		}

		/* Set stored metrics */
		Cached->Stored |= CACHED_METRICS;
	}
	
	/* Do we have the glyph cached as bitmap/pixmap? */
	if (((Want & CACHED_BITMAP) && !(Cached->Stored & CACHED_BITMAP)) ||
		((Want & CACHED_PIXMAP) && !(Cached->Stored & CACHED_PIXMAP))) 
	{
		/* This means we have to render the glyph 
		 * so we need some extra vars */
		int Mono = (Want & CACHED_BITMAP);
		FT_Bitmap *Source;
		FT_Bitmap *Destination;
		FT_Glyph BitmapGlyph = NULL;

		/* Handle the italic style */
		if (TTF_HANDLE_STYLE_ITALIC(Font)) 
		{
			/* Create a new matrix */
			FT_Matrix Shear;

			/* Set up shearing */
			Shear.xx = 1 << 16;
			Shear.xy = (int)(Font->GlyphItalics * (1 << 16)) / Font->Height;
			Shear.yx = 0;
			Shear.yy = 1 << 16;

			/* Apply the transform */
			FT_Outline_Transform(Outline, &Shear);
		}

		/* Render as outline */
		if ((Font->Outline > 0) && Glyph->format != FT_GLYPH_FORMAT_BITMAP) 
		{
			FT_Stroker Stroker;

			/* Extract the glyph bitmap so we can apply the stroker
			 * and create a new stroker */
			FT_Get_Glyph(Glyph, &BitmapGlyph);
			Error = FT_Stroker_New(m_pFreeType, &Stroker);
			if (Error) {
				return Error;
			}

			/* Stroke the glyph, and clenaup the stroker */
			FT_Stroker_Set(Stroker, Font->Outline * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
			FT_Glyph_Stroke(&BitmapGlyph, Stroker, 1 /* delete the original glyph */);
			FT_Stroker_Done(Stroker);
			
			/* Render the glyph */
			Error = FT_Glyph_To_Bitmap(&BitmapGlyph, Mono ? ft_render_mode_mono : ft_render_mode_normal, 0, 1);
			
			/* Sanitize the result, if fail
			 * Cleanup glyph and return error */
			if (Error) {
				FT_Done_Glyph(BitmapGlyph);
				return Error;
			}

			/* Shorthand the bitmap */
			Source = &((FT_BitmapGlyph)BitmapGlyph)->bitmap;
		}
		else {
			/* Render the glyph */
			Error = FT_Render_Glyph(Glyph, Mono ? ft_render_mode_mono : ft_render_mode_normal);
			if (Error) {
				return Error;
			}

			/* Shorthand the bitmap */
			Source = &Glyph->bitmap;
		}
		
		/* Copy over information to cache */
		if (Mono) {
			Destination = &Cached->Bitmap;
		}
		else {
			Destination = &Cached->Pixmap;
		}
		memcpy(Destination, Source, sizeof(*Destination));

		/* FT_Render_Glyph() and .fon fonts always generate a
		* two-color (black and white) glyphslot surface, even
		* when rendered in ft_render_mode_normal. */
		/* FT_IS_SCALABLE() means that the font is in outline format,
		* but does not imply that outline is rendered as 8-bit
		* grayscale, because embedded bitmap/graymap is preferred
		* (see FT_LOAD_DEFAULT section of FreeType2 API Reference).
		* FT_Render_Glyph() canreturn two-color bitmap or 4/16/256-
		* color graymap according to the format of embedded bitmap/
		* graymap. */
		if (Source->pixel_mode == FT_PIXEL_MODE_MONO) {
			Destination->pitch *= 8;
		}
		else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY2) {
			Destination->pitch *= 4;
		}
		else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY4) {
			Destination->pitch *= 2;
		}

		/* Adjust for bold and italic text */
		if (TTF_HANDLE_STYLE_BOLD(Font)) {
			int bump = Font->GlyphOverhang;
			Destination->pitch += bump;
			Destination->width += bump;
		}
		if (TTF_HANDLE_STYLE_ITALIC(Font)) {
			int bump = (int)ceilf(Font->GlyphItalics);
			Destination->pitch += bump;
			Destination->width += bump;
		}

		if (Destination->rows != 0) {
			Destination->buffer = (unsigned char *)malloc(Destination->pitch * Destination->rows);
			if (!Destination->buffer) {
				return FT_Err_Out_Of_Memory;
			}
			memset(Destination->buffer, 0, Destination->pitch * Destination->rows);

			for (int i = 0; i < Source->rows; i++) {
				int soffset = i * Source->pitch;
				int doffset = i * Destination->pitch;
				if (Mono) {
					unsigned char *srcp = Source->buffer + soffset;
					unsigned char *dstp = Destination->buffer + doffset;
					int j;
					if (Source->pixel_mode == FT_PIXEL_MODE_MONO) {
						for (j = 0; j < Source->width; j += 8) {
							unsigned char c = *srcp++;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
							c <<= 1;
							*dstp++ = (c & 0x80) >> 7;
						}
					}
					else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY2) {
						for (j = 0; j < Source->width; j += 4) {
							unsigned char c = *srcp++;
							*dstp++ = (((c & 0xA0) >> 6) >= 0x2) ? 1 : 0;
							c <<= 2;
							*dstp++ = (((c & 0xA0) >> 6) >= 0x2) ? 1 : 0;
							c <<= 2;
							*dstp++ = (((c & 0xA0) >> 6) >= 0x2) ? 1 : 0;
							c <<= 2;
							*dstp++ = (((c & 0xA0) >> 6) >= 0x2) ? 1 : 0;
						}
					}
					else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY4) {
						for (j = 0; j < Source->width; j += 2) {
							unsigned char c = *srcp++;
							*dstp++ = (((c & 0xF0) >> 4) >= 0x8) ? 1 : 0;
							c <<= 4;
							*dstp++ = (((c & 0xF0) >> 4) >= 0x8) ? 1 : 0;
						}
					}
					else {
						for (j = 0; j < Source->width; j++) {
							unsigned char c = *srcp++;
							*dstp++ = (c >= 0x80) ? 1 : 0;
						}
					}
				}
				else if (Source->pixel_mode == FT_PIXEL_MODE_MONO) {
					/* This special case wouldn't
					* be here if the FT_Render_Glyph()
					* function wasn't buggy when it tried
					* to render a .fon font with 256
					* shades of gray.  Instead, it
					* returns a black and white surface
					* and we have to translate it back
					* to a 256 gray shaded surface.
					* */
					unsigned char *srcp = Source->buffer + soffset;
					unsigned char *dstp = Destination->buffer + doffset;
					unsigned char c;
					int j, k;
					for (j = 0; j < Source->width; j += 8) {
						c = *srcp++;
						for (k = 0; k < 8; ++k) {
							if ((c & 0x80) >> 7) {
								*dstp++ = NUM_GRAYS - 1;
							}
							else {
								*dstp++ = 0x00;
							}
							c <<= 1;
						}
					}
				}
				else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY2) {
					unsigned char *srcp = Source->buffer + soffset;
					unsigned char *dstp = Destination->buffer + doffset;
					unsigned char c;
					int j, k;
					for (j = 0; j < Source->width; j += 4) {
						c = *srcp++;
						for (k = 0; k < 4; ++k) {
							if ((c & 0xA0) >> 6) {
								*dstp++ = NUM_GRAYS * ((c & 0xA0) >> 6) / 3 - 1;
							}
							else {
								*dstp++ = 0x00;
							}
							c <<= 2;
						}
					}
				}
				else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY4) {
					unsigned char *srcp = Source->buffer + soffset;
					unsigned char *dstp = Destination->buffer + doffset;
					unsigned char c;
					int j, k;
					for (j = 0; j < Source->width; j += 2) {
						c = *srcp++;
						for (k = 0; k < 2; ++k) {
							if ((c & 0xF0) >> 4) {
								*dstp++ = NUM_GRAYS * ((c & 0xF0) >> 4) / 15 - 1;
							}
							else {
								*dstp++ = 0x00;
							}
							c <<= 4;
						}
					}
				}
				else {
					memcpy(Destination->buffer + doffset, Source->buffer + soffset, Source->pitch);
				}
			}
		}

		/* Handle the bold style */
		if (TTF_HANDLE_STYLE_BOLD(Font)) 
		{
			/* Variables for tracking */
			int Row;
			int Col;
			int Offset;
			int Pixel;
			uint8_t* Pixmap;

			/* The pixmap is a little hard, we have to add and clamp */
			for (Row = Destination->rows - 1; Row >= 0; --Row) {
				Pixmap = (uint8_t*)Destination->buffer + Row * Destination->pitch;
				for (Offset = 1; Offset <= Font->GlyphOverhang; ++Offset) {
					for (Col = Destination->width - 1; Col > 0; --Col) {
						if (Mono) {
							Pixmap[Col] |= Pixmap[Col - 1];
						}
						else {
							Pixel = (Pixmap[Col] + Pixmap[Col - 1]);
							if (Pixel > NUM_GRAYS - 1) {
								Pixel = NUM_GRAYS - 1;
							}
							Pixmap[Col] = (uint8_t)Pixel;
						}
					}
				}
			}
		}

		/* Mark that we rendered this format */
		if (Mono) {
			Cached->Stored |= CACHED_BITMAP;
		}
		else {
			Cached->Stored |= CACHED_PIXMAP;
		}

		/* Free outlined glyph */
		if (BitmapGlyph) {
			FT_Done_Glyph(BitmapGlyph);
		}
	}

	/* We're done, mark this glyph cached */
	Cached->Cached = Character;

	/* Done! */
	return 0;
}

/* Looks up a glyph for the given character, if it 
 * doesn't exist and we do want it, we load it */
FT_Error Terminal::FindGlyph(TerminalFont* Font, uint16_t Character, int Want)
{
	/* Variables */
	int RetVal = 0;
	int Size = sizeof(Font->Cache) / sizeof(Font->Cache[0]);
	int h = Character % Size;

	Font->Current = &Font->Cache[h];

	/* If the currently stored glyph is not 
	 * the one we need, flush it */
	if (Font->Current->Cached != Character)
		FlushGlyph(Font->Current);

	/* Check if we need to load it */
	if ((Font->Current->Stored & Want) != Want) {
		RetVal = LoadGlyph(Font, Character, Font->Current, Want);
	}

	/* Done! */
	return RetVal;
}
