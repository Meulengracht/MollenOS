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

#include "alumni.hpp"

class CWin32Alumni : public CAlumni {
public:
    CWin32Alumni(std::unique_ptr<CTerminal> Terminal, std::unique_ptr<CTerminalInterpreter> Interpreter);
    ~CWin32Alumni() = default;

    bool HandleKeyCode(unsigned int KeyCode, unsigned int Flags) override;
    void PrintCommandHeader() override;
    
protected:
    bool CommandResolver(const std::string&, const std::vector<std::string>&) override;
    bool ListDirectory(const std::vector<std::string>&) override;
    bool ChangeDirectory(const std::vector<std::string>&) override;
};
