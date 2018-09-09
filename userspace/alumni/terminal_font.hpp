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

#include <string>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_GLYPH_H
#include FT_TRUETYPE_IDS_H

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

class CTerminalFont 
{
public:
    CTerminalFont(FT_Library FTLibrary, const std::string& FontPath, std::size_t InitialPixelSize);
    ~CTerminalFont();

    void    SetColors(uint32_t TextColor, uint32_t BackgroundColor);
    bool    SetSize(std::size_t PixelSize);
    int     RenderCharacter(unsigned long Character, uint32_t* AtPointer, std::size_t Stride);

private:
    FT_Error    LoadGlyph(unsigned long Character, FontGlyph_t* Cached, int Want);
    FT_Error    FindGlyph(unsigned long Character, int Want);
    void        FlushGlyph(FontGlyph_t* Glyph);
    void        FlushCache();

private:
    FT_Library      m_FreeType;
    FT_Face         m_Face;
    FontGlyph_t*    m_Current;
    FontGlyph_t     m_Cache[257]; /* 257 is a prime */
    uint32_t        m_FgColor;
    uint32_t        m_BgColor;

    int         m_FontHeight;
    int         m_FontWidth;
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
    void*       m_Source;
    std::size_t m_SourceLength;
};
