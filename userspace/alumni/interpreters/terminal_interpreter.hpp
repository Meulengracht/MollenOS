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

#include <functional>
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>
#include <memory>
#include <list>

class CTerminal;

class CTerminalInterpreter {
    class CTerminalCommand {
    public:
        CTerminalCommand(const std::string& Command, const std::string& Description, std::function<bool(const std::vector<std::string>&)> Fn)
            : m_Command(Command), m_Description(Description), m_Function(Fn) { }

        int operator()(const std::string& Command, const std::vector<std::string>& Arguments) {
            int Result = GetCommandDistance(Command);
            if (!Result) {
                m_Function(Arguments);
            }
            return Result;
        }

        const std::string& GetCommandText() const { return m_Command; }
        const std::string& GetDescription() const { return m_Description; }

    private:
        int GetCommandDistance(const std::string& Command);

    private:
        std::string m_Command;
        std::string m_Description;
        std::function<bool(const std::vector<std::string>&)> m_Function;
    };

public:
    CTerminalInterpreter(CTerminal& Terminal);
    ~CTerminalInterpreter() = default;

    void                RegisterCommand(const std::string& Command, const std::string& Description, std::function<bool(const std::vector<std::string>&)> Fn);
    virtual bool        HandleKeyCode(unsigned int KeyCode, unsigned int Flags) = 0;
    virtual void        PrintCommandHeader() = 0;

    const std::string&  GetClosestMatch() const { return m_ClosestMatch; }
    bool                IsAlive() const { return m_Alive; }

protected:
    std::vector<std::string> SplitCommandString(const std::string& String);
    
    bool Interpret(const std::string& String);
    bool Help(const std::vector<std::string>& Arguments);
    bool Exit(const std::vector<std::string>& Arguments);

protected:
    CTerminal&                                      m_Terminal;
    std::list<std::unique_ptr<CTerminalCommand>>    m_Commands;
    bool                                            m_Alive;
    std::string                                     m_ClosestMatch;
};
