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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

#define COLOR_BG     0xFF000000
#define COLOR_FG     0xFFFFFFFF
#define COLOR_BORDER 0xFFFFFFFF

static const char* g_bootConsoleTitle = "Startup Debug Console";

static void 
VideoDrawLine(
	_In_ unsigned int startX,
	_In_ unsigned int startY,
	_In_ unsigned int endX,
	_In_ unsigned int endY,
	_In_ unsigned int color)
{
	// Variables - clam some values
	int dx  = abs(endX - startX), sx = startX < endX ? 1 : -1;
	int dy  = abs(endY - startY), sy = startY < endY ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2, e2;

	// Draw the line by brute force
	for (;;) {
		VideoDrawPixel(startX, startY, color);
		if (startX == endX && startY == endY) break;
		e2 = err;
		if (e2 >-dx) { err -= dy; startX += sx; }
		if (e2 < dy) { err += dx; startY += sy; }
	}
}

static void
VideoDrawBootTerminal(
	_In_ unsigned int X,
	_In_ unsigned int Y,
	_In_ size_t       Width,
	_In_ size_t       Height)
{
	unsigned int TitleStartX = X + 8 + 32 + 8;
	unsigned int TitleStartY = Y + 18;
	int          i;

	// Instantiate a pointer to title
	char *TitlePtr = (char*)g_bootConsoleTitle;

	// Draw the header
	for (i = 0; i < 48; i++) {
		VideoDrawLine(X, Y + i, X + Width, Y + i, COLOR_BORDER);
	}
	
	// Draw remaining borders
	VideoDrawLine(X, Y, X, Y + Height, COLOR_BORDER);
	VideoDrawLine(X + Width, Y, X + Width, Y + Height, COLOR_BORDER);
	VideoDrawLine(X, Y + Height, X + Width, Y + Height, COLOR_BORDER);

	// Render title in middle of header
	while (*TitlePtr) {
		VideoDrawCharacter(TitleStartX, TitleStartY, *TitlePtr, COLOR_FG, COLOR_BG);
		TitleStartX += 10;
		TitlePtr++;
	}

	// Define some virtual borders to prettify just a little
	VideoGetTerminal()->CursorX = VideoGetTerminal()->CursorStartX = X + 11;
	VideoGetTerminal()->CursorLimitX = X + Width - 1;
	VideoGetTerminal()->CursorY = VideoGetTerminal()->CursorStartY = Y + 49;
	VideoGetTerminal()->CursorLimitY = Y + Height - 17;
    VideoGetTerminal()->BgColor = COLOR_BG;
    VideoGetTerminal()->FgColor = COLOR_FG;
}

OsStatus_t
VideoQuery(
	_Out_ VideoDescriptor_t *Descriptor)
{
	if (Descriptor == NULL || VideoGetTerminal() == NULL) {
		return OsError;
	}
	memcpy(Descriptor, &VideoGetTerminal()->Info, sizeof(VideoDescriptor_t));
	return OsSuccess;
}

/**
 * InitializeConsole
 * Initializes the output environment. This enables either visual representation
 * and debugging of the kernel and enables a serial debugger.
 */
OsStatus_t 
InitializeConsole(void)
{
    OsStatus_t osStatus;

    // Initialize visual representation by framebuffer
#ifdef __OSCONFIG_HAS_VIDEO
    osStatus = InitializeFramebufferOutput();
    if (osStatus == OsSuccess) {
        VideoClear(COLOR_BG);
#ifdef __OSCONFIG_DEBUGCONSOLE
		if (VideoGetTerminal()->AvailableOutputs & VIDEO_GRAPHICS) {
			VideoDrawBootTerminal((VideoGetTerminal()->CursorLimitX / 2) - 375, 0,
				MIN(750, VideoGetTerminal()->Info.Width), VideoGetTerminal()->Info.Height);
		}
#endif
    }
#endif

	// Only enable the log if we have any outputs
	if (VideoGetTerminal()->AvailableOutputs != 0) {
		LogSetRenderMode(1);	
	}
	return OsSuccess;
}
