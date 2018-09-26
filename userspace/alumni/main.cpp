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

#include "surfaces/surface_vali.hpp"
#include "interpreters/terminal_interpreter_vali.hpp"
#include "terminal_renderer.hpp"
#include "terminal_font.hpp"
#include "terminal.hpp"
#include <os/input.h>

int main(int argc, char **argv) {
    CTerminalFreeType           FreeType;
    CSurfaceRect                TerminalArea(450, 300);
    CValiSurface                Surface(TerminalArea);
    CTerminalRenderer           Renderer(Surface);
    CTerminalFont               Font(FreeType, "$sys/fonts/DejaVuSansMono.ttf", 12);
    CTerminal                   Terminal(TerminalArea, Renderer, Font);
    CValiTerminalInterpreter    Interpreter(Terminal);
    SystemKey_t                 Key;

    Interpreter.RegisterCommand("cd", "Change the working directory", [](const std::vector<std::string>&) { return true; });
    Interpreter.RegisterCommand("ls", "Lists the contents of the current working directory", [](const std::vector<std::string>&) { return true; });
    Interpreter.PrintCommandHeader();
    
	while (Interpreter.IsAlive()) {
        if (ReadSystemKey(&Key) == OsSuccess) {
            Interpreter.HandleKeyCode(Key.KeyCode, Key.Flags);
        }
    }
    return 0;
}
