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
* MollenOS Terminal Implementation
* - Project Alumnious
*/

#include "surfaces/surface_vali.hpp"
#include "terminal_interpreter.hpp"
#include "terminal_renderer.hpp"
#include "terminal_font.hpp"
#include "terminal.hpp"

int main(int argc, char **argv) {
    CTerminalFreeType       FreeType;
    CSurfaceRect            TerminalArea(450, 300);
    CValiSurface            Surface(TerminalArea);
    CTerminalRenderer       Renderer(Surface);
    CTerminalFont           Font(FreeType, Renderer, "$sys/Fonts/DejaVuSansMono.ttf", 12);
    CTerminal               Terminal(Font);
    CTerminalInterpreter    Interpreter(Terminal);

    Interpreter.RegisterCommand("cd", "Change the working directory", [](const std::vector<std::string>&) { return true; });
    Interpreter.RegisterCommand("ls", "Lists the contents of the current working directory", [](const std::vector<std::string>&) { return true; });

	Terminal.Print("MollenOS System Terminal %s\n", "V0.01-dev");
    
	return Interpreter.Run();
}
