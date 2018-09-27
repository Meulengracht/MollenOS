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

class CSurface;
class CTerminalFont;

class CTerminalRenderer {
public:
    CTerminalRenderer(std::unique_ptr<CSurface> Surface);
    ~CTerminalRenderer() = default;

    int CalculateTextLength(const std::shared_ptr<CTerminalFont>& Font, const std::string& Text);
    int GetLengthOfCharacter(const std::shared_ptr<CTerminalFont>& Font, char Character);

    void RenderClear(int X, int Y, int Width, int Height);
    int RenderText(int X, int Y, const std::shared_ptr<CTerminalFont>& Font, const std::string& Text);
    int RenderCharacter(int X, int Y, const std::shared_ptr<CTerminalFont>&Font, char Character);
    void Invalidate();

    void SetForegroundColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A);
    void SetForegroundColor(uint32_t Color);
    void SetBackgroundColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A);
    void SetBackgroundColor(uint32_t Color);
    
    uint32_t GetForegroundColor() const { return m_ForegroundColor; }
    uint32_t GetBackgroundColor() const { return m_BackgroundColor; }

private:
    std::unique_ptr<CSurface>   m_Surface;
    uint32_t                    m_BackgroundColor;
    uint32_t                    m_ForegroundColor;
};
