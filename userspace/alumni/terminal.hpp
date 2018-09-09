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

#include <cstddef>
#include <cstdlib>
#include "terminal_font.hpp"

class CSurface;

class CTerminal
{
public:
    CTerminal(CSurface& Surface, uint8_t BgR, uint8_t BgG, uint8_t BgB);
    ~CTerminal();

    void Print(const char *Message, ...);

    bool SetHistorySize(int NumLines);
    void SetFont(const std::string& FontPath, std::size_t PixelSize);
    void SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a);

private:
    void AddTextBuffer(char *Message, ...); // Add text to buffer
    void AddText(char *Message, ...); // Renders the text
    void HistoryPrevious();
    void HistoryNext();
    void NewLine();
    void InsertChar(char Character);
    int RemoveChar(int Position);
    void ShowCursor();
    void HideCursor();
    void RenderText(int AtX, int AtY, const char *Text);
    void ScrollText(int Lines);
    void ClearFrom(int Column, int Row);
    void AddCharacter(char Character);

private:
    FT_Library      m_FreeType;
    CSurface&       m_Surface;
    CTerminalFont*  m_Font;

    char**  m_History;
    int     m_HistorySize;
    int     m_HistoryIndex;

    char**  m_Buffer;
    int     m_BufferSize;

    char*   m_CurrentLine;
    int     m_LineStartX;
    int     m_LineStartY;
    int     m_LinePos;

    char    m_CmdStart[3];
    int     m_Columns;
    int     m_Rows;
    int     m_CursorPositionX;
    int     m_CursorPositionY;
    
    uint8_t m_BgR, m_BgG, m_BgB;
};
