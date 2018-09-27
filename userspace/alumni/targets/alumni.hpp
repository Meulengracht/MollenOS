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
#include <thread>
#include <string>
#include <vector>

class CTerminalInterpreter;
class CTerminal;

class CAlumni {
public:
    CAlumni(std::unique_ptr<CTerminal> Terminal, std::unique_ptr<CTerminalInterpreter> Interpreter);
    virtual ~CAlumni() = default;

public:
    virtual bool HandleKeyCode(unsigned int KeyCode, unsigned int Flags) = 0;
    virtual void PrintCommandHeader() = 0;

public:
    bool IsAlive() const { return m_Alive; }

protected:
    virtual bool CommandResolver(const std::string&, const std::vector<std::string>&) = 0;
    virtual bool ListDirectory(const std::vector<std::string>&) = 0;
    virtual bool ChangeDirectory(const std::vector<std::string>&) = 0;
    bool Help(const std::vector<std::string>&);
    bool Exit(const std::vector<std::string>&);

protected:
    std::unique_ptr<CTerminalInterpreter>   m_Interpreter;
    std::unique_ptr<CTerminal>              m_Terminal;
    bool                                    m_Alive;
};
