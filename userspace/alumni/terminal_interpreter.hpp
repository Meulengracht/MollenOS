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
        int GetCommandDistance(const std::string& Command)
        {
            std::size_t n = m_Command.size(), m = Command.size();
            if (n == 0) { return m; }
            if (m == 0) { return n; }

            std::size_t* d = new std::size_t[n * m];
            memset(d, 0, sizeof(std::size_t) * n * m);

            for (size_t i = 1, im = 0; i < m; ++i, ++im) {
                for (size_t j = 1, jn = 0; j < n; ++j, ++jn) {
                    if (m_Command[jn] == Command[im]) {
                        d[(i * n) + j] = d[((i - 1) * n) + (j - 1)];
                    }
                    else {
                        d[(i * n) + j] = std::min(d[(i - 1) * n + j] + 1,   /* A deletion. */
                                         std::min(d[i * n + (j - 1)] + 1,   /* An insertion. */
                                         d[(i - 1) * n + (j - 1)] + 1));    /* A substitution. */
                    }
                }
            }
            int r = (int)(d[n * m - 1]);
            delete [] d;
            return r;
        }

    private:
        std::string m_Command;
        std::string m_Description;
        std::function<bool(const std::vector<std::string>&)> m_Function;
    };

public:
    CTerminalInterpreter(CTerminal& Terminal);
    ~CTerminalInterpreter() = default;

    void    RegisterCommand(const std::string& Command, const std::string& Description, std::function<bool(const std::vector<std::string>&)> Fn);
    int     Run();

private:
    bool Interpret(const std::string& String, std::string& ClosestMatch);
    bool Help(const std::vector<std::string>& Arguments);
    bool Exit(const std::vector<std::string>& Arguments);

private:
    CTerminal&                                      m_Terminal;
    std::list<std::unique_ptr<CTerminalCommand>>    m_Commands;
    bool                                            m_Alive;
};
