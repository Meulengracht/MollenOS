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

#include "surfaces/surface.hpp"
#include "terminal_renderer.hpp"
#include "terminal_font.hpp"

namespace {
    unsigned int AlphaBlend(unsigned int ColorA, unsigned int ColorB, unsigned int Alpha)
    {
        unsigned int Rb1 = ((0x100 - Alpha) * (ColorA & 0xFF00FF)) >> 8;
        unsigned int Rb2 = (Alpha * (ColorB & 0xFF00FF)) >> 8;
        unsigned int G1  = ((0x100 - Alpha) * (ColorA & 0x00FF00)) >> 8;
        unsigned int G2  = (Alpha * (ColorB & 0x00FF00)) >> 8;
        return ((Rb1 | Rb2) & 0xFF00FF) + ((G1 | G2) & 0x00FF00) | 0xFF000000;
    }
}

CTerminalRenderer::CTerminalRenderer(std::unique_ptr<CSurface> Surface)
    : m_Surface(std::move(Surface)),
      m_BackgroundColor(m_Surface->GetColor(0, 0, 0, 255)),
      m_ForegroundColor(m_Surface->GetColor(255, 255, 255, 255))
{
    m_Surface->Clear(m_BackgroundColor, m_Surface->GetDimensions(), true);
}

void CTerminalRenderer::RenderClear(int X, int Y, int Width, int Height)
{
    CSurfaceRect Area(X, Y, 
        (Width == -1) ? m_Surface->GetDimensions().GetWidth() : Width, 
        (Height == -1) ? m_Surface->GetDimensions().GetHeight() : Height);
    m_Surface->Clear(m_BackgroundColor, Area, false);
}

int CTerminalRenderer::RenderText(int X, int Y, const std::shared_ptr<CTerminalFont>& Font, const std::string& Text)
{
    int Advance = X;
    Font->ResetPrevious();
    for (size_t i = 0; i < Text.length(); i++) {
        char Character = Text[i];
        Advance += RenderCharacter(Advance, Y, Font, Character);
    }
    return Advance;
}

int CTerminalRenderer::CalculateTextLength(const std::shared_ptr<CTerminalFont>& Font, const std::string& Text)
{
    int Advance = 0;
    if (Text.length() != 0) {
        Font->ResetPrevious();
        for (size_t i = 0; i < Text.length(); i++) {
            char Character = Text[i];
            Advance += GetLengthOfCharacter(Font, Character);
        }
    }
    return Advance;
}

int CTerminalRenderer::GetLengthOfCharacter(const std::shared_ptr<CTerminalFont>& Font, char Character)
{
    FontCharacter_t CharInfo;
    if (Font->GetCharacterBitmap(Character, CharInfo)) {
        return CharInfo.IndentX + CharInfo.Advance;
    }
    return 0;
}

int CTerminalRenderer::RenderCharacter(int X, int Y, const std::shared_ptr<CTerminalFont>& Font, char Character)
{
    FontCharacter_t CharInfo;

    if (Font->GetCharacterBitmap(Character, CharInfo)) {
        uint32_t* Pointer   = (uint32_t*)m_Surface->GetDataPointer(X + CharInfo.IndentX, Y + CharInfo.IndentY);
        uint8_t* Source     = CharInfo.Bitmap;
        for (int Row = 0; Row < CharInfo.Height; Row++) {
            for (int Column = 0; Column < CharInfo.Width; Column++) { // @todo might need to be reverse
                uint8_t Alpha = Source[Column];
                if (Alpha == 0) {
                    Pointer[Column] = m_BackgroundColor;
                }
                else if (Alpha == 255) {
                    Pointer[Column] = m_ForegroundColor;
                }
                else {
                    // Pointer[Column] = m_FgColor; if CACHED_BITMAP
                    Pointer[Column] = AlphaBlend(m_BackgroundColor, m_ForegroundColor, Alpha);
                }
            }
            Pointer = (uint32_t*)((uint8_t*)Pointer + m_Surface->GetStride());
            Source  += CharInfo.Pitch;
        }
        return CharInfo.IndentX + CharInfo.Advance;
    }
    return 0;
}

void CTerminalRenderer::Invalidate()
{
    m_Surface->Invalidate();
}

void CTerminalRenderer::SetForegroundColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
    m_ForegroundColor = m_Surface->GetColor(R, G, B, A);
}

void CTerminalRenderer::SetForegroundColor(uint32_t Color)
{
    m_ForegroundColor = Color;
}

void CTerminalRenderer::SetBackgroundColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
    m_BackgroundColor = m_Surface->GetColor(R, G, B, A);
}

void CTerminalRenderer::SetBackgroundColor(uint32_t Color)
{
    m_BackgroundColor = Color;
}
