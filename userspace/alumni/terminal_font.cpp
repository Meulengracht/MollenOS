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

#include <cassert>
#include <cstdio>
#include <cctype>
#include <cmath>
#include "terminal_font.hpp"

/* FIXME: Right now we assume the gray-scale renderer Freetype is using
          supports 256 shades of gray, but we should instead key off of num_grays
          in the result FT_Bitmap after the FT_Render_Glyph() call. */
#define NUM_GRAYS       256

/* Handy routines for converting from fixed point */
#define FT_FLOOR(x)                ((x & -64) / 64)
#define FT_CEIL(x)                (((x + 63) & -64) / 64)

/* Set and retrieve the font style */
#define TTF_STYLE_NORMAL        0x00
#define TTF_STYLE_BOLD          0x01
#define TTF_STYLE_ITALIC        0x02
#define TTF_STYLE_UNDERLINE     0x04
#define TTF_STYLE_STRIKETHROUGH 0x08

/* Handle a style only if the font does not already handle it */
#define TTF_HANDLE_STYLE_BOLD(font) (((font)->m_Style & TTF_STYLE_BOLD) && \
                                    !((font)->m_FaceStyle & TTF_STYLE_BOLD))
#define TTF_HANDLE_STYLE_ITALIC(font) (((font)->m_Style & TTF_STYLE_ITALIC) && \
                                      !((font)->m_FaceStyle & TTF_STYLE_ITALIC))
#define TTF_HANDLE_STYLE_UNDERLINE(font) ((font)->m_Style & TTF_STYLE_UNDERLINE)
#define TTF_HANDLE_STYLE_STRIKETHROUGH(font) ((font)->m_Style & TTF_STYLE_STRIKETHROUGH)

#define CACHED_METRICS  0x10
#define CACHED_BITMAP   0x01
#define CACHED_PIXMAP   0x02

namespace {
    static bool LoadFile(const std::string& Path, void** Buffer, std::size_t* Length)
    {
        FILE *file = fopen(FontPath.c_str(), "rab");
        if (file != nullptr) {
            *Length = ftell(file);
            if (*Length != 0) {
                rewind(file);
                *Buffer = malloc(*Length);
                if (*Buffer != nullptr) {
                    std::size_t BytesRead = fread(*Buffer, 1, *Length, file);
                    if (BytesRead == *Length) {
                        fclose(file);
                        return true;
                    }
                    free(*Buffer);
                }
            }
            fclose(file);
        }
        return false;
    }
}

CTerminalFont::CTerminalFont(CTerminalFreeType& FreeType, CTerminalRenderer& Renderer, const std::string& FontPath, std::size_t InitialPixelSize)
    : m_FreeType(FreeType), m_Renderer(Renderer)
{
    FT_CharMap CmFound  = 0;
    bool Status         = LoadFile(FontPath, &m_Source, &m_SourceLength);
    assert(Status);

    Status = FT_New_Memory_Face(m_FreeType.GetLibrary(), (FT_Byte*)DataBuffer, m_SourceLength, 0, &m_Face) == 0;
    assert(Status);

    // Build the character map
    for (int i = 0; i < m_Face->num_charmaps; i++) {
        FT_CharMap Cm = m_Face->charmaps[i];
        if ((Cm->platform_id == 3 && Cm->encoding_id == 1) ||   // Windows Unicode
            (Cm->platform_id == 3 && Cm->encoding_id == 0) ||   // Windows Symbol
            (Cm->platform_id == 2 && Cm->encoding_id == 1) ||   // ISO Unicode
            (Cm->platform_id == 0)) {                           // Apple Unicode
            CmFound = Cm;
            break;
        }
    }

    // Keep using default character map in case we don't find it
    if (CmFound) {
        FT_Set_Charmap(m_Face, CmFound);
    }
    
    Status = SetSize(InitialPixelSize);
    assert(Status);
}

CTerminalFont::~CTerminalFont()
{
    FlushCache();
    free(m_Source);
}

void CTerminalFont::SetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
    m_BgR = R; m_BgG = G; m_BgB = B; m_BgA = A;
}

bool CTerminalFont::SetSize(std::size_t PixelSize)
{
    // Make sure that our font face is scalable (global metrics)
    if (FT_IS_SCALABLE(m_Face)) {
        // Set the character size and use default DPI (72)
        if (FT_Set_Char_Size(m_Face, 0, PixelSize * 64, 0, 0)) {
            return false;
        }

        // Get the scalable font metrics for this font
        FT_Fixed Scale      = m_Face->size->metrics.y_scale;
        m_Ascent            = FT_CEIL(FT_MulFix(m_Face->ascender, Scale));
        m_Descent           = FT_CEIL(FT_MulFix(m_Face->descender, Scale));
        m_Height            = m_Ascent - m_Descent + /* baseline */ 1;
        m_LineSkip          = FT_CEIL(FT_MulFix(m_Face->height, Scale));
        m_UnderlineOffset   = FT_FLOOR(FT_MulFix(m_Face->underline_position, Scale));
        m_UnderlineHeight   = FT_FLOOR(FT_MulFix(m_Face->underline_thickness, Scale));
    }
    else {
        // Non-scalable font case.  ptsize determines which family
        // or series of fonts to grab from the non-scalable format.
        // It is not the point size of the font.
        if (PixelSize >= m_Face->num_fixed_sizes) {
            PixelSize = m_Face->num_fixed_sizes - 1;
        }

        if (FT_Set_Pixel_Sizes(m_Face, m_Face->available_sizes[PixelSize].width, 
            m_Face->available_sizes[PixelSize].height)) {
            return false;
        }
        m_FontSizeFamily = PixelSize;

        // With non-scalale fonts, Freetype2 likes to fill many of the
        // font metrics with the value of 0.  The size of the
        // non-scalable fonts must be determined differently
        // or sometimes cannot be determined.
        m_Ascent            = m_Face->available_sizes[PixelSize].height;
        m_Descent           = 0;
        m_Height            = m_Face->available_sizes[PixelSize].height;
        m_LineSkip          = FT_CEIL(m_Ascent);
        m_UnderlineOffset   = FT_FLOOR(m_Face->underline_position);
        m_UnderlineHeight   = FT_FLOOR(m_Face->underline_thickness);
    }

    if (UnderlineHeight < 1) {
        UnderlineHeight = 1;
    }

    // Initialize the font face style
    FaceStyle = TTF_STYLE_NORMAL;
    if (m_Face->style_flags & FT_STYLE_FLAG_BOLD) {
        FaceStyle |= TTF_STYLE_BOLD;
    }
    if (m_Face->style_flags & FT_STYLE_FLAG_ITALIC) {
        FaceStyle |= TTF_STYLE_ITALIC;
    }

    // Update stored settings
    m_Style         = FaceStyle;
    m_Outline       = 0;
    m_Kerning       = 1;
    m_GlyphOverhang = m_Face->size->metrics.y_ppem / 10;

    // x offset = cos(((90.0-12)/360)*2*M_PI), or 12 degree angle
    m_GlyphItalics  = 0.207f;
    m_GlyphItalics  *= m_Face->height;
    m_FontHeight    = Height;
    return true;
}

int CTerminalFont::RenderCharacter(int X, int Y, unsigned long Character)
{
    FT_Long UseKerning      = FT_HAS_KERNING(m_Face) && m_Kerning;
    FT_Bitmap* Current;
    FontGlyph_t* Glyph;
    FT_Error Status;
    int IndentX             = 0;
    int Width;

    Status = FindGlyph(Character, CACHED_METRICS | CACHED_PIXMAP);
    if (Status) {
        return 0;
    }
    Glyph   = m_Current;
    Current = &Glyph->Pixmap; // Use Bitmap if CACHED_BITMAP is set

    // Ensure the width of the pixmap is correct. On some cases,
    // freetype may report a larger pixmap than possible.
    Width = Current->width;
    if (m_Outline <= 0 && Width > Glyph->MaxX - Glyph->MinX) {
        Width = Glyph->MaxX - Glyph->MinX;
    }

    // do kerning, if possible AC-Patch
    if (UseKerning && m_PreviousIndex && Glyph->Index) {
        FT_Vector Delta;
        FT_Get_Kerning(m_Face, m_PreviousIndex, Glyph->Index, FT_KERNING_DEFAULT, &Delta);
        IndentX += Delta.x >> 6;
    }

    // Compensate for wrap around bug with negative minx's
    if (Glyph->MinX < 0) {
        IndentX -= Glyph->MinX;
    }

    // Render to the pointer
    m_Renderer.SetColor(m_BgR, m_BgG, m_BgB, m_BgA);
    m_Renderer.RenderBitmap(X + IndentX, Y, Width, Current->rows, Current->buffer, Current->pitch);

    // Advance, and handle bold style if neccassary
    IndentX += Glyph->Advance;
    if (TTF_HANDLE_STYLE_BOLD(this)) {
        IndentX += m_GlyphOverhang;
    }
    m_PreviousIndex = Glyph->Index;
    return IndentX;

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
}

void CTerminalFont::FlushGlyph(FontGlyph_t* Glyph)
{
    Glyph->Stored   = 0;
    Glyph->Index    = 0;
    Glyph->Cached   = 0;

    if (Glyph->Bitmap.buffer) {
        free(Glyph->Bitmap.buffer);
        Glyph->Bitmap.buffer = 0;
    }
    if (Glyph->Pixmap.buffer) {
        free(Glyph->Pixmap.buffer);
        Glyph->Pixmap.buffer = 0;
    }
}

void CTerminalFont::FlushCache()
{
    int ElementCount = sizeof(m_Cache) / sizeof(m_Cache[0]);
    for (int i = 0; i < ElementCount; ++i) {
        if (m_Cache[i].Cached) {
            FlushGlyph(&m_Cache[i]);
        }
    }
}

FT_Error CTerminalFont::LoadGlyph(unsigned long Character, FontGlyph_t* Cached, int Want)
{
    FT_Error Status     = 0;
    FT_GlyphSlot Glyph;
    FT_Glyph_Metrics* Metrics;
    FT_Outline* Outline;

    if (!m_Face) {
        return FT_Err_Invalid_Handle;
    }

    // Look up the character index, we will need it later
    if (!Cached->Index) {
        Cached->Index = FT_Get_Char_Index(Face, Character);
    }

    Status = FT_Load_Glyph(Face, Cached->Index, FT_LOAD_DEFAULT | Hinting);
    if (Status) {
        return Status;
    }

    Glyph   = m_Face->glyph;
    Metrics = &Glyph->metrics;
    Outline = &Glyph->outline;

    // Get the glyph metrics if desired
    if ((Want & CACHED_METRICS) && !(Cached->Stored & CACHED_METRICS)) {
        if (FT_IS_SCALABLE(m_Face)) {
            // Get the bounding box
            Cached->MinX    = FT_FLOOR(Metrics->horiBearingX);
            Cached->MaxX    = FT_CEIL(Metrics->horiBearingX + Metrics->width);
            Cached->MaxY    = FT_FLOOR(Metrics->horiBearingY);
            Cached->MinY    = Cached->MaxY - FT_CEIL(Metrics->height);
            Cached->yOffset = Ascent - Cached->MaxY;
            Cached->Advance = FT_CEIL(Metrics->horiAdvance);
        }
        else {
            // Get the bounding box for non-scalable format.
            // Again, freetype2 fills in many of the font metrics
            // with the value of 0, so some of the values we
            // need must be calculated differently with certain
            // assumptions about non-scalable formats.
            Cached->MinX    = FT_FLOOR(Metrics->horiBearingX);
            Cached->MaxX    = FT_CEIL(Metrics->horiBearingX + Metrics->width);
            Cached->MaxY    = FT_FLOOR(Metrics->horiBearingY);
            Cached->MinY    = Cached->MaxY - FT_CEIL(m_Face->available_sizes[FontSizeFamily].height);
            Cached->yOffset = 0;
            Cached->Advance = FT_CEIL(Metrics->horiAdvance);
        }

        // Adjust for bold and italic text
        if (TTF_HANDLE_STYLE_BOLD(Font)) {
            Cached->MaxX += GlyphOverhang;
        }
        if (TTF_HANDLE_STYLE_ITALIC(Font)) {
            Cached->MaxX += (int)ceilf(GlyphItalics);
        }
        Cached->Stored |= CACHED_METRICS;
    }
    
    // Do we have the glyph cached as bitmap/pixmap?
    if (((Want & CACHED_BITMAP) && !(Cached->Stored & CACHED_BITMAP)) ||
        ((Want & CACHED_PIXMAP) && !(Cached->Stored & CACHED_PIXMAP))) 
    {
        int Mono                = (Want & CACHED_BITMAP);
        FT_Glyph BitmapGlyph    = nullptr;
        FT_Bitmap *Source;
        FT_Bitmap *Destination;

        // Handle the italic style
        if (TTF_HANDLE_STYLE_ITALIC(Font))  {
            FT_Matrix Shear;

            // Initialize shearing for glyph
            Shear.xx = 1 << 16;
            Shear.xy = (int)(GlyphItalics * (1 << 16)) / Height;
            Shear.yx = 0;
            Shear.yy = 1 << 16;
            FT_Outline_Transform(Outline, &Shear);
        }

        // Handle outline rendering
        if ((Outline > 0) && Glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            FT_Stroker Stroker;
            FT_Get_Glyph(Glyph, &BitmapGlyph);
            Status = FT_Stroker_New(m_pFreeType, &Stroker);
            if (Status) {
                return Status;
            }

            // Stroke the glyph, and clenaup the stroker
            FT_Stroker_Set(Stroker, Outline * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
            FT_Glyph_Stroke(&BitmapGlyph, Stroker, 1 /* delete the original glyph */);
            FT_Stroker_Done(Stroker);
            
            // Render the glyph to a bitmap for easier re-rendering
            Status = FT_Glyph_To_Bitmap(&BitmapGlyph, Mono ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL, 0, 1);
            if (Status) {
                FT_Done_Glyph(BitmapGlyph);
                return Status;
            }
            Source = &((FT_BitmapGlyph)BitmapGlyph)->bitmap;
        }
        else {
            Status = FT_Render_Glyph(Glyph, Mono ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL);
            if (Status) {
                return Status;
            }
            Source = &Glyph->bitmap;
        }
        
        // Copy over information to cache
        if (Mono) {
            Destination = &Cached->Bitmap;
        }
        else {
            Destination = &Cached->Pixmap;
        }
        memcpy(Destination, Source, sizeof(*Destination));

        // FT_Render_Glyph() and .fon fonts always generate a
        // two-color (black and white) glyphslot surface, even
        // when rendered in ft_render_mode_normal.
        // FT_IS_SCALABLE() means that the font is in outline format,
        // but does not imply that outline is rendered as 8-bit
        // grayscale, because embedded bitmap/graymap is preferred
        // (see FT_LOAD_DEFAULT section of FreeType2 API Reference).
        // FT_Render_Glyph() canreturn two-color bitmap or 4/16/256-
        // color graymap according to the format of embedded bitmap/
        // graymap.
        if (Source->pixel_mode == FT_PIXEL_MODE_MONO) {
            Destination->pitch *= 8;
        }
        else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY2) {
            Destination->pitch *= 4;
        }
        else if (Source->pixel_mode == FT_PIXEL_MODE_GRAY4) {
            Destination->pitch *= 2;
        }

        // Adjust for bold and italic text
        int Bump = 0;
        if (TTF_HANDLE_STYLE_BOLD(Font)) {
            Bump += GlyphOverhang;
        }
        if (TTF_HANDLE_STYLE_ITALIC(Font)) {
            Bump += (int)ceilf(GlyphItalics);
        }
        Destination->pitch += bump;
        Destination->width += bump;

        if (Destination->rows != 0) {
            Destination->buffer = (unsigned char*)malloc(Destination->pitch * Destination->rows);
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
                    // This special case wouldn't be here if the FT_Render_Glyph()
                    // function wasn't buggy when it tried to render a .fon font with 256
                    // shades of gray.  Instead, it returns a black and white surface
                    // and we have to translate it back to a 256 gray shaded surface.
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

        // Handle the bold style
        if (TTF_HANDLE_STYLE_BOLD(Font))  {
            uint8_t* Pixmap;
            int Row;
            int Col;
            int Offset;
            int Pixel;

            // The pixmap is a little hard, we have to add and clamp
            for (Row = Destination->rows - 1; Row >= 0; --Row) {
                Pixmap = (uint8_t*)Destination->buffer + Row * Destination->pitch;
                for (Offset = 1; Offset <= GlyphOverhang; ++Offset) {
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

        // Mark that we rendered this format
        if (Mono) {
            Cached->Stored |= CACHED_BITMAP;
        }
        else {
            Cached->Stored |= CACHED_PIXMAP;
        }

        // Free outlined glyph
        if (BitmapGlyph) {
            FT_Done_Glyph(BitmapGlyph);
        }
    }

    Cached->Cached = Character;
    return 0;
}

FT_Error CTerminalFont::FindGlyph(unsigned long Character, int Want)
{
    FT_Error Status     = 0;
    int ElementCount    = sizeof(m_Cache) / sizeof(m_Cache[0]);
    int h               = Character % ElementCount;

    // Get the appropriate index
    m_Current = &m_Cache[h];
    if (m_Current->Cached != Character) {
        FlushGlyph(m_Current);
    }

    // check if it should be stored
    if ((m_Current->Stored & Want) != Want) {
        Status = LoadGlyph(Character, m_Current, Want);
    }
    return Status;
}
