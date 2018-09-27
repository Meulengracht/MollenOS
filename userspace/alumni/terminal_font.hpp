/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS Terminal Implementation (Alumnious)
 * - The terminal emulator implementation for Vali. Built on manual rendering and
 *   using freetype as the font renderer.
 */
#pragma once

#include <memory>
#include <string>
#include "terminal_freetype.hpp"

class CTerminalRenderer;

typedef struct FontGlyph {
    int             Stored;
    FT_UInt         Index;
    FT_Bitmap       Bitmap;
    FT_Bitmap       Pixmap;
    int             MinX;
    int             MaxX;
    int             MinY;
    int             MaxY;
    int             yOffset;
    int             Advance;
    unsigned long   Cached;
} FontGlyph_t;

typedef struct FontCharacter {
    uint8_t*    Bitmap;
    int         Pitch;

    int         Width;
    int         Height;

    int         IndentX;
    int         IndentY;
    int         Advance;
} FontCharacter_t;

class CTerminalFont
{
public:
    CTerminalFont(std::unique_ptr<CTerminalFreeType> FreeType, const std::string& FontPath, std::size_t InitialPixelSize);
    ~CTerminalFont();

    bool    SetSize(std::size_t PixelSize);
    int     GetFontHeight() const { return m_Height; }
    int     GetLineSkip() const { return m_LineSkip; }
    bool    GetCharacterBitmap(unsigned long Character, FontCharacter_t& Information);
    void    ResetPrevious();

private:
    FT_Error    LoadGlyph(unsigned long Character, FontGlyph_t* Cached, int Want);
    FT_Error    FindGlyph(unsigned long Character, int Want);
    void        FlushGlyph(FontGlyph_t* Glyph);
    void        FlushCache();

private:
    std::unique_ptr<CTerminalFreeType>  m_FreeType;
    FT_Face                             m_Face;
    
    FontGlyph_t* m_Current;
    FontGlyph_t  m_Cache[257]; /* 257 is a prime */

    int         m_Height;
    int         m_Ascent;
    int         m_Descent;
    int         m_LineSkip;
    std::size_t m_FontSizeFamily;

    int     m_FaceStyle;
    int     m_Style;
    int     m_Outline;
    int     m_Kerning;
    int     m_Hinting;
    FT_UInt m_PreviousIndex;

    int         m_GlyphOverhang;
    float       m_GlyphItalics;
    int         m_UnderlineOffset;
    int         m_UnderlineHeight;
};
