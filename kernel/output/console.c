/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Video Device
 */

#include <system/video.h>
#include <string.h>
#include <video.h>
#include <math.h>
#include <log.h>

/* Globals
 * Window title for boot console */
const char *GlbBootVideoWindowTitle = "Startup Debug Console";

/* VideoDrawLine
 * Draw's a line from (StartX, StartY) -> (EndX, EndY) 
 * with the given color */
void 
VideoDrawLine(
	_In_ unsigned StartX, 
	_In_ unsigned StartY,
	_In_ unsigned EndX, 
	_In_ unsigned EndY, 
	_In_ unsigned Color)
{
	// Variables - clam some values
	int dx = abs(EndX - StartX), sx = StartX < EndX ? 1 : -1;
	int dy = abs(EndY - StartY), sy = StartY < EndY ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2, e2;

	// Draw the line by brute force
	for (;;) {
		VideoDrawPixel(StartX, StartY, Color);
		if (StartX == EndX && StartY == EndY) break;
		e2 = err;
		if (e2 >-dx) { err -= dy; StartX += sx; }
		if (e2 < dy) { err += dx; StartY += sy; }
	}
}

/* VideoDrawBootTerminal
 * Draws the crude initial boot-terminal without any
 * fancy effects or anything */
void
VideoDrawBootTerminal(
	_In_ unsigned X, 
	_In_ unsigned Y,
	_In_ size_t Width, 
	_In_ size_t Height)
{
	// Variables
	unsigned TitleStartX = X + 8 + 32 + 8;
	unsigned TitleStartY = Y + 18;
	int i;

	// Instantiate a pointer to title
	char *TitlePtr = (char*)GlbBootVideoWindowTitle;

	// Draw the header
	for (i = 0; i < 48; i++) {
		VideoDrawLine(X, Y + i, X + Width, Y + i, 0x2980B9);
	}
	
	// Draw remaining borders
	VideoDrawLine(X, Y, X, Y + Height, 0x2980B9);
	VideoDrawLine(X + Width, Y, X + Width, Y + Height, 0x2980B9);
	VideoDrawLine(X, Y + Height, X + Width, Y + Height, 0x2980B9);

	// Render title in middle of header
	while (*TitlePtr) {
		VideoDrawCharacter(TitleStartX, TitleStartY, *TitlePtr, 0x2980B9, 0xFFFFFF);
		TitleStartX += 10;
		TitlePtr++;
	}

	// Define some virtual borders to prettify just a little
	VideoGetTerminal()->CursorX = VideoGetTerminal()->CursorStartX = X + 11;
	VideoGetTerminal()->CursorLimitX = X + Width - 1;
	VideoGetTerminal()->CursorY = VideoGetTerminal()->CursorStartY = Y + 49;
	VideoGetTerminal()->CursorLimitY = Y + Height - 17;
}

/* VideoQuery
 * Renders a character with default colors
 * at the current terminal position */
OsStatus_t
VideoQuery(
	_Out_ VideoDescriptor_t *Descriptor)
{
	// Sanitize
	if (Descriptor == NULL || VideoGetTerminal() == NULL) {
		return OsError;
	}
	memcpy(Descriptor, &VideoGetTerminal()->Info, sizeof(VideoDescriptor_t));
	return OsSuccess;
}

/* VideoInitialize
 * Initializes boot-video environment untill a more
 * complete driver can take-over the screen */
OsStatus_t 
VideoInitialize(void)
{
    // Clear video
    VideoClear();

	// Draw boot terminal if we have graphics
	if (VideoGetTerminal()->Type == VIDEO_GRAPHICS) {
#ifdef __OSCONFIG_DEBUGCONSOLE
		VideoDrawBootTerminal((VideoGetTerminal()->CursorLimitX / 2) - 325, 0, 
			650, VideoGetTerminal()->Info.Height);
#endif
	}
    LogSetRenderMode(1);
	return OsSuccess;
}

/* VideoDebugMode
 * Initializes boot-video environment untill a more
 * complete driver can take-over the screen */
OsStatus_t
VideoDebugMode(void)
{
    return OsError;
}
