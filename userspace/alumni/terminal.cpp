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
#include <cctype>
#include <cmath>
#include "surfaces/surface.hpp"
#include "terminal.hpp"
#include "terminal_font.hpp"
#include "terminal_renderer.hpp"

const int PRINTBUFFER_SIZE = 4096;

CTerminal::CTerminalLine::CTerminalLine(std::shared_ptr<CTerminalRenderer> Renderer, std::shared_ptr<CTerminalFont> Font, int Row, int Capacity)
    : m_Renderer(Renderer), m_Font(Font), m_Row(Row), m_Capacity(Capacity - 6)
{
    Reset();
}

void CTerminal::CTerminalLine::Reset()
{
    m_ShowCursor    = false;
    m_TextLength    = 0;
    m_InputOffset   = 0;
    m_Cursor        = 0;
    m_Dirty         = true;
    m_Text.clear();
}

bool CTerminal::CTerminalLine::AddCharacter(int Character)
{
    char Buf            = (char)Character & 0xFF;
    int CharacterLength = m_Renderer->GetLengthOfCharacter(m_Font, Buf);

    // Handle \r \t \n?
    if ((m_TextLength + CharacterLength) < m_Capacity) {
        if (m_Cursor == m_Text.length()) {
            m_Text.push_back(Character);
        }
        else {
            m_Text.insert(m_Cursor, &Buf, 1);
        }
        m_Dirty      = true;
        m_TextLength += CharacterLength;
        m_InputOffset++;
        m_Cursor++;
        return true;
    }
    return false;
}

bool CTerminal::CTerminalLine::AddInput(int Character)
{
    char Buf            = (char)Character & 0xFF;
    int CharacterLength = m_Renderer->GetLengthOfCharacter(m_Font, Buf);

    // Handle \r \t \n?
    if ((m_TextLength + CharacterLength) < m_Capacity) {
        if (m_Cursor == m_Text.length()) {
            m_Text.push_back(Character);
        }
        else {
            m_Text.insert(m_Cursor, &Buf, 1);
        }
        m_Dirty      = true;
        m_TextLength += CharacterLength;
        m_Cursor++;
        return true;
    }
    return false;
}

bool CTerminal::CTerminalLine::RemoveInput()
{
    int ModifiedCursor = m_Cursor - m_InputOffset;
    if (ModifiedCursor != 0) {
        m_Text.erase(m_Cursor - 1, 1);
        m_Dirty      = true;
        m_TextLength = m_Renderer->CalculateTextLength(m_Font, m_Text);
        m_Cursor--;
        return true;
    }
    return false;
}

void CTerminal::CTerminalLine::Update()
{
    if (m_Dirty) {
        m_Renderer->RenderClear(0, (m_Row * m_Font->GetFontHeight()) + 2, -1, m_Font->GetFontHeight());
        m_TextLength = m_Renderer->RenderText(3, (m_Row * m_Font->GetFontHeight()) + 2, m_Font, m_Text);
        if (m_ShowCursor) {
            uint32_t ExistingColor = m_Renderer->GetBackgroundColor();
            m_Renderer->SetBackgroundColor(255, 255, 255, 255);
            m_Renderer->RenderCharacter(m_TextLength, (m_Row * m_Font->GetFontHeight()) + 2, m_Font, '_');
            m_Renderer->SetBackgroundColor(ExistingColor);
        }
        m_Dirty = false;
    }
}

void CTerminal::CTerminalLine::SetText(const std::string& Text)
{
    Reset();
    m_Text   = Text;
    m_Cursor = Text.length();
}

std::string CTerminal::CTerminalLine::GetInput()
{
    if ((int)m_Text.length() > m_InputOffset) {
        return m_Text.substr(m_InputOffset);
    }
    return "";
}

void CTerminal::CTerminalLine::HideCursor()
{
    m_ShowCursor = false;
    m_Dirty      = true;
}

void CTerminal::CTerminalLine::ShowCursor()
{
    m_ShowCursor = true;
    m_Dirty      = true;
}

CTerminal::CTerminal(CSurfaceRect& Area, std::shared_ptr<CTerminalRenderer> Renderer, std::shared_ptr<CTerminalFont> Font)
    : m_Renderer(Renderer), m_Font(Font), m_Rows((Area.GetHeight() / Font->GetFontHeight()) - 1), 
      m_HistoryIndex(0), m_LineIndex(0)
{
    m_PrintBuffer = (char*)std::malloc(PRINTBUFFER_SIZE);
    for (int i = 0; i < m_Rows; i++) {
        m_Lines.push_back(std::make_unique<CTerminalLine>(Renderer, Font, i, Area.GetWidth()));
    }
}

CTerminal::~CTerminal() {
    std::free(m_PrintBuffer);
    m_History.clear();
    m_Lines.clear();
}

void CTerminal::AddInput(int Character)
{
    if (!m_Lines[m_LineIndex]->AddInput(Character)) {
        // uh todo, we should skip to next line
        return;
    }
    m_Lines[m_LineIndex]->Update();
}

void CTerminal::RemoveInput()
{
    if (!m_Lines[m_LineIndex]->RemoveInput()) {
        // uh todo, we should skip to prev line
        return;
    }
    m_Lines[m_LineIndex]->Update();
}

std::string CTerminal::ClearInput(bool Newline)
{
    std::string Input = m_Lines[m_LineIndex]->GetInput();
    if (Newline) {
        FinishCurrentLine();
    }
    else {
        m_Lines[m_LineIndex]->Reset();
        m_Lines[m_LineIndex]->Update();
    }
    return Input;
}

void CTerminal::FinishCurrentLine()
{
    // Only add to history if not an empty line
    if (m_Lines[m_LineIndex]->GetText().length() > 0) {
        m_History.push_back(m_Lines[m_LineIndex]->GetText());
        m_HistoryIndex = m_History.size();
    }

    // Are we at the end?
    if (m_LineIndex == m_Rows - 1) {
        ScrollToLine(true);
    }
    else {
        m_Lines[m_LineIndex]->HideCursor();
        m_Lines[m_LineIndex]->Update();
        m_LineIndex++;
        m_Lines[m_LineIndex]->ShowCursor();
        m_Lines[m_LineIndex]->Update();
    }
}

void CTerminal::ScrollToLine(bool ClearInput)
{
    int HistoryStart = m_HistoryIndex - m_LineIndex;
    for (int i = 0; i < m_Rows; i++) {
        if (i == m_LineIndex && !ClearInput) {
            break;
        }

        m_Lines[i]->Reset();
        if (HistoryStart < m_HistoryIndex) {
            m_Lines[i]->SetText(m_History[HistoryStart++]);
        }
        m_Lines[i]->Update();
    }
}

void CTerminal::HistoryNext()
{
    // History must be longer than the number of rows - 1
    if (m_History.size()        >= (size_t)m_Rows &&
        (size_t)m_HistoryIndex  < m_History.size()) {
        m_HistoryIndex++;
        ScrollToLine(false);
    }
}

void CTerminal::HistoryPrevious()
{
    // History must be longer than the number of rows - 1
    if (m_History.size()    >= (size_t)m_Rows &&
        m_HistoryIndex      >= m_Rows) {
        m_HistoryIndex--;
        ScrollToLine(false);
    }
}

void CTerminal::Print(const char *Format, ...)
{
    std::unique_lock<std::mutex> LockedSection(m_PrintLock);
    va_list Arguments;

    va_start(Arguments, Format);
    vsnprintf(m_PrintBuffer, PRINTBUFFER_SIZE, Format, Arguments);
    va_end(Arguments);

    for (size_t i = 0; i < PRINTBUFFER_SIZE && m_PrintBuffer[i]; i++) {
        if (m_PrintBuffer[i] == '\n') {
            FinishCurrentLine();
        }
        else {
            if (!m_Lines[m_LineIndex]->AddCharacter(m_PrintBuffer[i])) {
                FinishCurrentLine();
                i--;
            }
        }
    }
    m_Lines[m_LineIndex]->Update();
}

void CTerminal::Invalidate()
{
    m_Renderer->Invalidate();
}

void CTerminal::MoveCursorLeft()
{

}

void CTerminal::MoveCursorRight()
{

}
