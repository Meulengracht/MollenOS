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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */

#include <os/process.h>
#include <os/input.h>
#include <string.h>
#include "vioarr.hpp"

void SpawnApplication(const char* Path, const char* Arguments)
{
    ProcessStartupInformation_t StartupInformation;
    InitializeStartupInformation(&StartupInformation);

    // Set arguments
    if (Arguments != NULL) {
        StartupInformation.ArgumentPointer = Arguments;
        StartupInformation.ArgumentLength = strlen(Arguments);
    }
    ProcessSpawnEx(Path, &StartupInformation, 1);
}

bool HandleShortcut(SystemKey_t* Key)
{

}

bool HandleFunctionKeys(SystemKey_t* Key)
{
    if (Key.KeyCode == VK_F1) {
        // Spawn the test application
        SpawnApplication("$bin/wintest.app", NULL);
        return true;
    }
    else if (Key.KeyCode == VK_F2) {
        // Spawn the terminal application
        SpawnApplication("$bin/terminal.app", NULL);
        return true;
    }
    return false;
}

void InputHandler()
{
    bool IsRunning = true;
    SystemKey_t Key;

    while (IsRunning) {
        if (ReadSystemKey(&Key) == OsSuccess) {
            bool Handled = false;
            if (Key.Flags & (KEY_MODIFIER_LALT | KEY_MODIFIER_RALT)) {
                Handled = HandleShortcut(&Key);
            }
            else {
                Handled = HandleFunctionKeys(&Key);
            }

            // Redirect if not handled
            if (!Handled) {
                //CEntity* Window = sEngine.GetActiveWindow();
                //if (CEntity != nullptr) {
                    // SendPipe(Window->GetOwner(), Window->GetStdInput(), &Key, sizeof(SystemKey_t));
                //}
            }
        }
    }
}
