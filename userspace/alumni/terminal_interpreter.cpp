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
#include <numeric>
#include "terminal.hpp"
#include "terminal_interpreter.hpp"

int CTerminalInterpreter::CTerminalCommand::GetCommandDistance(const std::string& Command)
{
	int s1len = m_Command.size();
	int s2len = Command.size();
	
	auto column_start   = (decltype(s1len))1;
	auto column         = new decltype(s1len)[s1len + 1];
	std::iota(column + column_start - 1, column + s1len + 1, column_start - 1);
	
	for (auto x = column_start; x <= s2len; x++) {
		column[0] = x;
		auto last_diagonal = x - column_start;
		for (auto y = column_start; y <= s1len; y++) {
			auto old_diagonal = column[y];
			auto possibilities = {
				column[y] + 1,
				column[y - 1] + 1,
				last_diagonal + (m_Command[y - 1] == Command[x - 1]? 0 : 1)
			};
			column[y] = std::min(possibilities);
			last_diagonal = old_diagonal;
		}
	}
	auto result = column[s1len];
	delete[] column;
	return result;
}

CTerminalInterpreter::CTerminalInterpreter()
    : m_ClosestMatch("")
{
}

void CTerminalInterpreter::RegisterCommand(const std::string& Command, const std::string& Description, std::function<bool(const std::vector<std::string>&)> Fn)
{
    m_Commands.push_back(std::make_unique<CTerminalCommand>(Command, Description, Fn));
}

std::vector<std::string> CTerminalInterpreter::SplitCommandString(const std::string& String)
{
    std::vector<std::string>    Tokens;
    std::string                 Token = "";
    for (size_t i = 0; i < String.size(); i++) {
        if (String[i] == ' ') {
            if (Token != "") {
                Tokens.push_back(Token);
            }
            Token = "";
            continue;
        }
        Token.push_back(String[i]);
    }
    if (Token != "") {
        Tokens.push_back(Token);
    }
    return Tokens;
}

bool CTerminalInterpreter::Interpret(const std::string& String)
{
    int ShortestDistance = String.size();
    m_ClosestMatch = "";
    if (String.size() == 0) {
        return false;
    }
    
    // Get command and remove from array
    std::vector<std::string> Tokens = SplitCommandString(String);
    std::string CommandString       = Tokens[0];
    Tokens.erase(Tokens.begin());

    for (auto& Command : m_Commands) {
        int Distance = Command->operator()(CommandString, Tokens);
        if (Distance == 0) {
            return true;
        }
        else if (Distance < ShortestDistance) {
            ShortestDistance    = Distance;
            m_ClosestMatch      = Command->GetCommandText();
        }
    }
    return m_Resolver(CommandString, Tokens);
}

void CTerminalInterpreter::SetCommandResolver(std::function<bool(const std::string&, const std::vector<std::string>&)> Resolver)
{
    m_Resolver = Resolver;
}
