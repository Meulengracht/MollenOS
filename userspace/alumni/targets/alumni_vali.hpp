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

#include <os/osdefs.h>
#include <thread>
#include "alumni.hpp"

class CValiAlumni : public CAlumni {
public:
    CValiAlumni(std::unique_ptr<CTerminal> Terminal, std::unique_ptr<CTerminalInterpreter> Interpreter);
    ~CValiAlumni();

public:
    bool HandleKeyCode(unsigned int KeyCode, unsigned int Flags) override;
    void PrintCommandHeader() override;

protected:
    bool CommandResolver(const std::string&, const std::vector<std::string>&) override;
    bool ListDirectory(const std::vector<std::string>&) override;
    bool ChangeDirectory(const std::vector<std::string>&) override;

private:
    std::vector<std::string> GetDirectoryContents(const std::string& Path);
    bool ExecuteProgram(const std::string&, const std::vector<std::string>&);
    bool IsProgramPathValid(const std::string&);
    void UpdateWorkingDirectory();
    void WaitForProcess();
    
    void StdoutListener();
    void StderrListener();

private:
    std::string m_Profile;
    std::string m_CurrentDirectory;

    int     m_Stdout;
    int     m_Stderr;
    UUId_t  m_Application;

    std::thread     m_StdoutThread;
    std::thread     m_StderrThread;
};
