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

#include "targets/alumni_vali.hpp"
#include "surfaces/surface_vali.hpp"
#include "terminal_interpreter.hpp"
#include "terminal_renderer.hpp"
#include "terminal_font.hpp"
#include "terminal.hpp"
#include <os/input.h>

int main(int argc, char **argv) {
    std::unique_ptr<CTerminalFreeType>          FreeType(new CTerminalFreeType());
    CSurfaceRect                                TerminalArea(450, 300);
    std::unique_ptr<CSurface>                   Surface(new CValiSurface(TerminalArea));
    std::shared_ptr<CTerminalRenderer>          Renderer(new CTerminalRenderer(std::move(Surface)));
    std::shared_ptr<CTerminalFont>              Font(new CTerminalFont(std::move(FreeType), "$sys/fonts/DejaVuSansMono.ttf", 12));
    std::unique_ptr<CTerminal>                  Terminal(new CTerminal(TerminalArea, Renderer, Font));
    std::unique_ptr<CTerminalInterpreter>       Interpreter(new CTerminalInterpreter());
    std::unique_ptr<CValiAlumni>                Alumni(new CValiAlumni(std::move(Terminal), std::move(Interpreter)));
    SystemKey_t                                 Key;

    Alumni->PrintCommandHeader();
	while (Alumni->IsAlive()) {
        if (ReadSystemKey(&Key) == OsSuccess) {
            Alumni->HandleKeyCode(Key.KeyCode, Key.Flags);
        }
    }
    return 0;
}
