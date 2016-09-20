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

/* Constructor 
 * - We use this to instantiate a drawing surface */
Terminal::Terminal()
{
	/* Store params and setup initial values */
	m_bIsAlive = true;

	/* Initialize render surface */
	m_pSurface = new Surface();

	/* Initialize a new instance of freetype
	 * so we can use the font engine */
	if (FT_Init_FreeType(&m_pFreeType))
		m_bIsAlive = false;
}

/* Destructor 
 * - We use this to cleanup all resources allocated */
Terminal::~Terminal()
{
	/* Destroy render surface */
	delete m_pSurface;
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

}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetHistorySize(size_t NumCharacters)
{

}

/* Terminal Customization functions
 * Use this for setting font, colors
 * size etc */
bool Terminal::SetFont(const char *FontPath, int SizePixels)
{
	/* Load the font file into a buffer 
	 * and retrieve size of file */
	FILE *Fp = fopen(FontPath, "ra");
	size_t Size = ftell(Fp);
	void *DataBuffer = NULL;
	FT_Face Font;

	/* Sanitize the stuff, ftell won't
	 * crash on null, so we can wait till here */
	if (Fp == NULL || Size == 0)
		return false;

	/* Spool back */
	rewind(Fp);

	/* Instantiate a new font object 
	 * in the FT library */
	if (FT_New_Memory_Face(m_pFreeType,
		(FT_Byte*)DataBuffer, Size, 0, &Font)) {
		free(DataBuffer);
		return false;
	}
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
