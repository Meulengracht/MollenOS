/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Video Includes */
#include <MollenOS.h>
#include <Arch.h>
#include <Devices\Video.h>
#include <Log.h>

/* CLib */
#include <string.h>
#include <math.h>
#include <stddef.h>

/* Video */
const char *GlbBootVideoWindowTitle = "MollenOS Boot Console";
MCoreVideoDevice_t *GlbVideoPtr = NULL;

/* Draw Line */
void VideoDrawLine(MCoreVideoDevice_t *VideoDevice, 
	uint32_t StartX, uint32_t StartY, uint32_t EndX, uint32_t EndY, uint32_t Color)
{
	int dx = abs(EndX - StartX), sx = StartX < EndX ? 1 : -1;
	int dy = abs(EndY - StartY), sy = StartY < EndY ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2, e2;

	for (;;) {
		VideoDevice->DrawPixel(VideoDevice, StartX, StartY, Color);
		if (StartX == EndX && StartY == EndY) break;
		e2 = err;
		if (e2 >-dx) { err -= dy; StartX += sx; }
		if (e2 < dy) { err += dx; StartY += sy; }
	}
}

/* Draw Window */
void VideoDrawBootTerminal(MCoreVideoDevice_t *VideoDevice, 
	uint32_t X, uint32_t Y, uint32_t Width, uint32_t Height)
{
	/* Title pointer */
	uint32_t TitleStartX = X + 8 + 32 + 8;
	uint32_t TitleStartY = Y + 18;
	int i;
	char *TitlePtr = (char*)GlbBootVideoWindowTitle;

	/* Draw Borders */
	for (i = 0; i < 48; i++)
		VideoDrawLine(VideoDevice, X, Y + i, X + Width, Y + i, 0x2980B9);
	VideoDrawLine(VideoDevice, X, Y, X, Y + Height, 0x2980B9);
	VideoDrawLine(VideoDevice, X + Width, Y, X + Width, Y + Height, 0x2980B9);
	VideoDrawLine(VideoDevice, X, Y + Height, X + Width, Y + Height, 0x2980B9);

	/* Draw Title */
	while (*TitlePtr)
	{
		char Char = *TitlePtr;
		VideoDevice->DrawCharacter(VideoDevice, 
			(int)Char, TitleStartY, TitleStartX, 0xFFFFFF, 0x2980B9);
		TitleStartX += 10;

		TitlePtr++;
	}

	/* Define Virtual Borders */
	VideoDevice->CursorX = VideoDevice->CursorStartX = X + 11;
	VideoDevice->CursorLimitX = X + Width - 1;
	VideoDevice->CursorY = VideoDevice->CursorStartY = Y + 49;
	VideoDevice->CursorLimitY = Y + Height - 17;
}

/* CPU Prototypes */
OsStatus_t VideoBootInit(MCoreVideoDevice_t *BootVideo)
{
	/* Clear */
	GlbVideoPtr = BootVideo;

	/* Draw boot terminal */
	if (BootVideo->Type == VideoTypeLFB)
		VideoDrawBootTerminal(BootVideo, (BootVideo->CursorLimitX / 2) - 325,
		(BootVideo->CursorLimitY / 2) - 260, 650, 520);

	/* Reset lock */
	SpinlockReset(&BootVideo->Lock);

	/* Now we have a terminal, 
	 * Redirect the log */
	LogRedirect(LogConsole);

	/* Done */
	return OsNoError;
}

/* PutChar Wrapper */
int VideoPutChar(int Character)
{
	/* Sanity */
	if (GlbVideoPtr == NULL)
		return Character;

	/* Do the deed */
	GlbVideoPtr->Put(GlbVideoPtr, Character);

	/* Done */
	return Character;
}