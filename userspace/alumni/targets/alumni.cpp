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

#include "../terminal_interpreter.hpp"
#include "../terminal.hpp"
#include "alumni.hpp"

CAlumni::CAlumni(std::unique_ptr<CTerminal> Terminal, std::unique_ptr<CTerminalInterpreter> Interpreter)
    : m_Interpreter(std::move(Interpreter)), m_Terminal(std::move(Terminal)), m_Alive(true)
{
    // Register inbuilt commands
    m_Interpreter->SetCommandResolver(std::bind(&CAlumni::CommandResolver, this, std::placeholders::_1, std::placeholders::_2));
    m_Interpreter->RegisterCommand("cd", "Change the working directory", std::bind(&CAlumni::ChangeDirectory, this, std::placeholders::_1));
    m_Interpreter->RegisterCommand("ls", "Lists the contents of the current working directory", std::bind(&CAlumni::ListDirectory, this, std::placeholders::_1));
    m_Interpreter->RegisterCommand("help", "Lists all registered terminal commands", std::bind(&CAlumni::Help, this, std::placeholders::_1));
    m_Interpreter->RegisterCommand("exit", "Quits the terminal", std::bind(&CAlumni::Exit, this, std::placeholders::_1));
}

bool CAlumni::Help(const std::vector<std::string>&)
{
    for (auto& Command : m_Interpreter->GetCommands()) {
        m_Terminal->Print("%s - %s\n", Command->GetCommandText().c_str(), Command->GetDescription().c_str());
    }
    return true;
}

bool CAlumni::Exit(const std::vector<std::string>&)
{
    m_Alive = false;
    return true;
}
