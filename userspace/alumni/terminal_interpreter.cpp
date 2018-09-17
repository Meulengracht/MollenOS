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
#include "terminal.hpp"
#include "terminal_interpreter.hpp"

CTerminalInterpreter::CTerminalInterpreter(CTerminal& Terminal)
    : m_Terminal(Terminal), m_Alive(true)
{
    // Register inbuilt commands
    RegisterCommand("help", "Lists all registered terminal commands", std::bind(&CTerminalInterpreter::Help, this, std::placeholders::_1));
    RegisterCommand("exit", "Quits the terminal", std::bind(&CTerminalInterpreter::Exit, this, std::placeholders::_1));
}

void CTerminalInterpreter::RegisterCommand(const std::string& Command, const std::string& Description, std::function<bool(const std::vector<std::string>&)> Fn)
{
    m_Commands.push_back(std::make_unique<CTerminalCommand>(Command, Description, Fn));
}

bool CTerminalInterpreter::Interpret(const std::string& String, std::string& ClosestMatch)
{
    std::stringstream                   Stream(String);
    std::istream_iterator<std::string>  Begin(Stream);
    std::istream_iterator<std::string>  End;
    std::vector<std::string>            Tokens(Begin, End);
    int ShortestDistance = String.size();

    // Get command and remove from array
    std::string CommandString = Tokens[0];
    Tokens.erase(Tokens.begin());

    // Clear out any empty arguments
    Tokens.erase(std::remove_if(Tokens.begin(), Tokens.end(),
        [](std::string const& Token) { return Token.size() == 0; }), Tokens.end());

    for (auto& Command : m_Commands) {
        int Distance = Command->operator()(CommandString, Tokens);
        if (Distance == 0) {
            return true;
        }
        else if (Distance < ShortestDistance) {
            ShortestDistance    = Distance;
            ClosestMatch        = Command->GetCommandText();
        }
    }
    return false;
}

int CTerminalInterpreter::Run()
{
    char* CurrentPath = (char*)std::malloc(_MAXPATH);
    std::string ClosestString = "";
    SystemKey_t Key;

    std::memset(CurrentPath, 0, _MAXPATH);
    GetWorkingDirectory(CurrentPath, _MAXPATH);

    while (m_Alive) {
        m_Terminal.Print("[%s | %s | 09/12/2018 - 13:00]\n", CurrentPath, "Philip");
        m_Terminal.Print("$ ");

        while (true) {
            if (ReadSystemKey(&Key) == OsSuccess) {
                if (Key.KeyCode == VK_BACK) {
                    m_Terminal.RemoveInput();
                }
                else if (Key.KeyCode == VK_ENTER) {
                    std::string Input = m_Terminal.ClearInput(true);
                    if (!Interpret(Input, ClosestString)) {
                        m_Terminal.Print("Command did not exist, did you mean %s?\n", ClosestString.c_str());
                    }
                    break;
                }
                else if (Key.KeyCode == VK_UP) {
                    m_Terminal.HistoryPrevious();
                }
                else if (Key.KeyCode == VK_DOWN) {
                    m_Terminal.HistoryNext();
                }
                else if (Key.KeyCode == VK_LEFT) {
                    m_Terminal.MoveCursorLeft();
                }
                else if (Key.KeyCode == VK_RIGHT) {
                    m_Terminal.MoveCursorRight();
                }
                else if (TranslateSystemKey(&Key) == OsSuccess) {
                    m_Terminal.AddInput(Key.KeyAscii);
                }
            }
        }
    }
    return 0;
}

bool CTerminalInterpreter::Help(const std::vector<std::string>& Arguments)
{
    for (auto& Command : m_Commands) {
        m_Terminal.Print("%s - %s\n", Command->GetCommandText().c_str(), Command->GetDescription().c_str());
    }
    return true;
}

bool CTerminalInterpreter::Exit(const std::vector<std::string>& Arguments)
{
    m_Alive = false;
    return true;
}
