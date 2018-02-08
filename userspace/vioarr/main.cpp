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

/* Includes
 * - Library */
#include <os/service.h>
#include <os/window.h>
#include "vioarr.hpp"

void MessageHandler() {
    char *ArgumentBuffer    = NULL;
    bool IsRunning          = true;
    MRemoteCall_t Message;

    // Open pipe
    ArgumentBuffer = (char*)::malloc(IPC_MAX_MESSAGELENGTH);
    PipeOpen(PIPE_RPCOUT);

    // Listen for messages
    while (IsRunning) {
        if (RPCListen(&Message, ArgumentBuffer) == OsSuccess) {
            if (Message.Function == __WINDOWMANAGER_NEWINPUT) {
                
            }
        }
    }

    // Done
    PipeClose(PIPE_RPCOUT);
}

// Spawn the message handler for compositor
void VioarrCompositor::SpawnMessageHandler() {
    _MessageThread = new std::thread(MessageHandler);
}

int main(int argc, char **argv) {
    if (RegisterService(__WINDOWMANAGER_TARGET) != OsSuccess) {
        // Only once instance at the time
        return -1;
    }
    return sVioarr.Run();
}
