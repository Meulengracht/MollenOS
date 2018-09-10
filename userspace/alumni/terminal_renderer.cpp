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

#include "terminal_renderer.hpp"
#include "surfaces/surface.hpp"

namespace {
    unsigned int AlphaBlend(unsigned int ColorA, unsigned int ColorB, unsigned int Alpha)
    {
        unsigned int Rb1 = ((0x100 - Alpha) * (ColorA & 0xFF00FF)) >> 8;
        unsigned int Rb2 = (Alpha * (ColorB & 0xFF00FF)) >> 8;
        unsigned int G1  = ((0x100 - Alpha) * (ColorA & 0x00FF00)) >> 8;
        unsigned int G2  = (Alpha * (ColorB & 0x00FF00)) >> 8;
        return ((Rb1 | Rb2) & 0xFF00FF) + ((G1 | G2) & 0x00FF00);
    }
}

CTerminalRenderer::CTerminalRenderer(CSurface& Surface)
    : m_Surface(Surface),
      m_BackgroundColor(Surface.GetColor(0, 0, 0, 255)),
      m_ForegroundColor(Surface.GetColor(255, 255, 255, 255))
{
}

void CTerminalRenderer::SetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
    m_ForegroundColor = Surface.GetColor(R, G, B, A);
}

void CTerminalRenderer::RenderBitmap(int X, int Y, int Columns, int Rows, uint8_t* Bitmap, std::size_t Pitch)
{
    uint32_t* Pointer   = (uint32_t*)m_Surface.GetDataPointer(X, Y);
    uint8_t* Source     = Bitmap;
    for (int Row = 0; Row < Rows; Row++) {
        for (int Column = 0; Column < Columns; Column++) { // @todo might need to be reverse
            uint8_t Alpha = Source[Column];
            if (Alpha == 0) {
                Pointer[Column] = m_BackgroundColor;
            }
            else {
                // Pointer[Column] = m_FgColor; if CACHED_BITMAP
                Pointer[Column] = AlphaBlend(m_BackgroundColor, m_ForegroundColor, Alpha);
            }
        }
        Pointer = (uint8_t*)Pointer + Surface.GetStride();
        Source  += Pitch;
    }
}
