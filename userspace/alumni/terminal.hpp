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

#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <list>

class CTerminalRenderer;
class CTerminalFont;
class CSurfaceRect;

class CTerminal
{    
private:
    class CTerminalLine {
    public:
        CTerminalLine(std::shared_ptr<CTerminalRenderer> Renderer, std::shared_ptr<CTerminalFont> Font, int Row, int Capacity);
        ~CTerminalLine() = default;

        void Reset();
        bool AddInput(int Character);
        bool RemoveInput();
        bool AddCharacter(int Character);
        void Update();

        void SetText(const std::string& Text);
        void HideCursor();
        void ShowCursor();

        std::string GetInput();
        const std::string& GetText() const { return m_Text; }

    private:
        std::shared_ptr<CTerminalRenderer>  m_Renderer;
        std::shared_ptr<CTerminalFont>      m_Font;
        
        std::string m_Text;
        int         m_TextLength;
        int         m_Row;
        int         m_Capacity;
        int         m_Cursor;
        int         m_InputOffset;
        bool        m_ShowCursor;
        bool        m_Dirty;
    };

public:
    CTerminal(CSurfaceRect& Area, std::shared_ptr<CTerminalRenderer> Renderer, std::shared_ptr<CTerminalFont> Font);
    ~CTerminal();

    void Print(const char *Format, ...);
    void Invalidate();

    // Input manipulation
    std::string ClearInput(bool Newline);
    void RemoveInput();
    void AddInput(int Character);
    
    // History manipulation
    void HistoryPrevious();
    void HistoryNext();

    // Cursor manipulation
    void MoveCursorLeft();
    void MoveCursorRight();

private:
    void FinishCurrentLine();
    void ScrollToLine(bool ClearInput);

private:
    std::shared_ptr<CTerminalRenderer>          m_Renderer;
    std::shared_ptr<CTerminalFont>              m_Font;
    int                                         m_Rows;
    std::vector<std::string>                    m_History;
    int                                         m_HistoryIndex;
    std::vector<std::unique_ptr<CTerminalLine>> m_Lines;
    int                                         m_LineIndex;
    char*                                       m_PrintBuffer;
    std::mutex                                  m_PrintLock;
};
