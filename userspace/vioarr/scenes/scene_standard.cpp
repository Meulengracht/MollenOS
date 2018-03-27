/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */

#include "../engine/veightengine.hpp"
#include "../engine/elements/sprite.hpp"
#include "../engine/elements/window.hpp"
#include "vioarr.hpp"

CEntity *VioarrCompositor::CreateStandardScene()
{
    // Create a new root instance
    CSprite *Background = new CSprite(sEngine.GetContext(), "$sys/themes/default/gfxbg.png", _Display->GetWidth(), _Display->GetHeight());

    // Create user interface

    // Spawn test window
    CWindow *Test = new CWindow(Background, sEngine.GetContext(), "Test", 450, 300);
    Test->Move(200, 200, 0);

    // Done
    return Background;
}
