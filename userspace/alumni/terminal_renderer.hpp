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

class CSurface;

class CTerminalRenderer {
public:
    CTerminalRenderer(CSurface& Surface);
    ~CTerminalRenderer() = default;

    void SetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A);
    void RenderBitmap(int X, int Y, int Columns, int Rows, uint8_t* Bitmap, std::size_t Pitch);

private:
    CSurface&   m_Surface;
    uint32_t    m_BackgroundColor;
    uint32_t    m_ForegroundColor;
};
