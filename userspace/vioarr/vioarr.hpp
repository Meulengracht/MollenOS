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
#pragma once

/* Includes
 * - Library */
#include <os/service.h>
#include <cstdlib>

/* Includes
 * - System */
#include "graphics/displays/display_framebuffer.hpp"
#include "input/input_handler.hpp"

class VioarrCompositor {
public:
	static VioarrCompositor& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static VioarrCompositor _Instance;
		return _Instance;
	}
private:
	VioarrCompositor() {}                     // Constructor? (the {} brackets) are needed here.
	VioarrCompositor(VioarrCompositor const&);// Don't Implement
	void operator=(VioarrCompositor const&);  // Don't implement

public:
	VioarrCompositor(VioarrCompositor const&) = delete;
	void operator=(VioarrCompositor const&) = delete;

    // Run
    // The main program loop
    int Run() {
        char *ArgumentBuffer    = NULL;
        bool IsRunning          = true;
        MRemoteCall_t Message;

        // Create the display
        _Display = new CDisplayOsMesa();
        if (!_Display->Initialize(-1, -1)) {
            delete _Display;
            return -2;
        }

        // Present the background image
        
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

private:
    CDisplay *_Display;
}


// Shorthand for the vioarr
#define sVioarr VioarrCompositor::GetInstance()
