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

#include <sstream>
#include <os/input.h>
#include <os/mollenos.h>
#include "../terminal.hpp"
#include "terminal_interpreter_vali.hpp"

CValiTerminalInterpreter::CValiTerminalInterpreter(CTerminal& Terminal)
    : CTerminalInterpreter(Terminal), m_Profile("Philip"), m_CurrentDirectory("n/a")
{
    UpdateWorkingDirectory();
}

void CValiTerminalInterpreter::UpdateWorkingDirectory()
{
    char* CurrentPath = (char*)std::malloc(_MAXPATH);
    std::memset(CurrentPath, 0, _MAXPATH);
    if (GetWorkingDirectory(CurrentPath, _MAXPATH) == OsSuccess) {
        m_CurrentDirectory = CurrentPath;
    }
    else {
        m_CurrentDirectory = "n/a";
    }
    std::free(CurrentPath);
}

bool CValiTerminalInterpreter::HandleKeyCode(unsigned int KeyCode, unsigned int Flags)
{
    SystemKey_t Key;

    // Don't respond to released events
    if (Flags & KEY_MODIFIER_RELEASED) {
        return false;
    }
    Key.KeyCode = KeyCode;
    Key.Flags   = Flags;

    if (KeyCode == VK_BACK) {
        m_Terminal.RemoveInput();
    }
    else if (KeyCode == VK_ENTER) {
        std::string Input = m_Terminal.ClearInput(true);
        if (!Interpret(Input)) {
            if (GetClosestMatch().length() != 0) {
                m_Terminal.Print("Command did not exist, did you mean %s?\n", GetClosestMatch().c_str());
            }
        }
        PrintCommandHeader();
    }
    else if (KeyCode == VK_UP) {
        m_Terminal.HistoryPrevious();
    }
    else if (KeyCode == VK_DOWN) {
        m_Terminal.HistoryNext();
    }
    else if (KeyCode == VK_LEFT) {
        m_Terminal.MoveCursorLeft();
    }
    else if (KeyCode == VK_RIGHT) {
        m_Terminal.MoveCursorRight();
    }
    else if (TranslateSystemKey(&Key) == OsSuccess) {
        m_Terminal.AddInput(Key.KeyAscii);
    }
    return true;
}

void CValiTerminalInterpreter::PrintCommandHeader()
{
    m_Terminal.Print("[%s | %s | 09/12/2018 - 13:00]\n", m_Profile.c_str(), m_CurrentDirectory.c_str());
    m_Terminal.Print("$ ");
}
