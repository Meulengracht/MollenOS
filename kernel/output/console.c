/**
 * Copyright 2016, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Debug Console Implementation
 *  - Provides an interface for creating and initializing the output system
 *    and the debug console
 */

#include <arch/output.h>
#include <string.h>
#include <console.h>
#include <math.h>
#include <log.h>

#define COLOR_BG 0xFF000000
#define COLOR_FG 0xFFFFFFFF

OsStatus_t
VideoQuery(
	_Out_ VideoDescriptor_t* videoDescriptor)
{
	if (videoDescriptor == NULL || VideoGetTerminal() == NULL) {
		return OsNotSupported;
	}

	memcpy(videoDescriptor, &VideoGetTerminal()->Info, sizeof(VideoDescriptor_t));
	return OsSuccess;
}

OsStatus_t 
ConsoleInitialize(void)
{
    OsStatus_t osStatus;

    // Initialize visual representation by framebuffer
#ifdef __OSCONFIG_HAS_VIDEO
    osStatus = InitializeFramebufferOutput();
    if (osStatus == OsSuccess) {
        VideoClear(COLOR_BG);
#ifdef __OSCONFIG_DEBUGCONSOLE
        // Define some virtual borders to prettify just a little
        VideoGetTerminal()->CursorX = VideoGetTerminal()->CursorStartX = 5;
        VideoGetTerminal()->CursorLimitX -= 10;
        VideoGetTerminal()->CursorY = VideoGetTerminal()->CursorStartY = 5;
        VideoGetTerminal()->CursorLimitY -= 10;

        // Set colors as requested
        VideoGetTerminal()->BgColor = COLOR_BG;
        VideoGetTerminal()->FgColor = COLOR_FG;
#endif
    }
#endif

	// Only enable the log if we have any outputs
	if (VideoGetTerminal()->AvailableOutputs != 0) {
		LogSetRenderMode(1);	
	}
	return OsSuccess;
}
