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

#include <chrono>
#include <thread>
#include <sstream>
#include "../terminal.hpp"
#include "terminal_interpreter_win32.hpp"
#include <windows.h>

namespace {
    int Vk2Ascii(unsigned int KeyCode, unsigned int ScanCode, int *Character)
    {
        unsigned char KybdState[256];

        if (!GetKeyboardState(&KybdState[0])) {
            return 0;
        }
        return (ToAscii(KeyCode, ScanCode, &KybdState[0], (LPWORD)Character, 0) > 0);
    }
}

CWin32TerminalInterpreter::CWin32TerminalInterpreter(CTerminal& Terminal)
    : CTerminalInterpreter(Terminal)
{
}

bool CWin32TerminalInterpreter::HandleKeyCode(unsigned int KeyCode, unsigned int Flags)
{
    int Character;
    if (KeyCode == VK_RETURN) {
        if (!Interpret(m_Terminal.ClearInput(true))) {
            if (GetClosestMatch().length() != 0) {
                m_Terminal.Print("Command did not exist, did you mean %s?\n", GetClosestMatch().c_str());
            }
        }
        PrintCommandHeader();
    }
    else if (KeyCode == VK_BACK) {
        m_Terminal.RemoveInput();
    }
    else if (KeyCode == VK_LEFT) {
        m_Terminal.MoveCursorLeft();
    }
    else if (KeyCode == VK_RIGHT) {
        m_Terminal.MoveCursorRight();
    }
    else if (Vk2Ascii(KeyCode, (Flags >> 16) & 0xFF, &Character) != 0) {
        m_Terminal.AddInput(Character);
    }
    return true;
}

void CWin32TerminalInterpreter::PrintCommandHeader()
{
    m_Terminal.Print("[%s | %s | 09/12/2018 - 13:00]\n", "Philip", "n/a");
    m_Terminal.Print("$ ");
}
