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
 * MollenOS x86-32 Boot-Video Component
 */

/* Includes 
 * - System */
#include <system/video.h>
#include <multiboot.h>
#include <vbe.h>

/* Includes
 * - Library */
#include <string.h>

/* Globals 
 * We need to keep track of boot-video in this system */
static BootTerminal_t __GlbVideoTerminal;

/* Externs
 * We want to import the standard font from
 * MCore system */
__EXTERN const uint8_t MCoreFontBitmaps[];
__EXTERN const uint32_t MCoreFontNumChars;
__EXTERN const uint32_t MCoreFontHeight;
__EXTERN const uint32_t MCoreFontWidth;

#ifdef UNICODE
__EXTERN const uint16_t MCoreFontIndex[];
#endif

/* VbeInitialize
 * Initializes the X86 video sub-system and provides
 * boot-video interface for the entire OS */
void 
VbeInitialize(
	_In_ Multiboot_t *BootInfo)
{
	// Zero out structure
	memset(&__GlbVideoTerminal, 0, sizeof(__GlbVideoTerminal));

	// Initialize lock
	SpinlockReset(&__GlbVideoTerminal.Lock);

	// Which kind of mode has been enabled for us
	switch (BootInfo->VbeMode) {

		// Text-Mode (80x25)
		case 0: {
			__GlbVideoTerminal.Type = VIDEO_TEXT;
			__GlbVideoTerminal.Info.Width = 80;
			__GlbVideoTerminal.Info.Height = 25;
			__GlbVideoTerminal.Info.Depth = 16;
			__GlbVideoTerminal.Info.BytesPerScanline = 2 * 80;
			__GlbVideoTerminal.Info.FrameBufferAddress = STD_VIDEO_MEMORY;

			__GlbVideoTerminal.CursorLimitX = 80;
			__GlbVideoTerminal.CursorLimitY = 25;
			__GlbVideoTerminal.FgColor = (0 << 4) | (15 & 0x0F);
			__GlbVideoTerminal.BgColor = 0;
		} break;

		// Text-Mode (80x50)
		case 1: {
			__GlbVideoTerminal.Type = VIDEO_TEXT;
			__GlbVideoTerminal.Info.Width = 80;
			__GlbVideoTerminal.Info.Height = 50;
			__GlbVideoTerminal.Info.Depth = 16;
			__GlbVideoTerminal.Info.BytesPerScanline = 2 * 80;
			__GlbVideoTerminal.Info.FrameBufferAddress = STD_VIDEO_MEMORY;

			__GlbVideoTerminal.CursorLimitX = 80;
			__GlbVideoTerminal.CursorLimitY = 50;
			__GlbVideoTerminal.FgColor = (0 << 4) | (15 & 0x0F);
			__GlbVideoTerminal.BgColor = 0;
		} break;

		// VGA-Mode (Graphics)
		case 2:
		{
			__GlbVideoTerminal.Type = VIDEO_GRAPHICS;

			__GlbVideoTerminal.CursorLimitX = __GlbVideoTerminal.Info.Width / (MCoreFontWidth + 1);
			__GlbVideoTerminal.CursorLimitY = __GlbVideoTerminal.Info.Height / MCoreFontHeight;
			__GlbVideoTerminal.FgColor = (0 << 4) | (15 & 0x0F);
			__GlbVideoTerminal.BgColor = 0;
		} break;

		// VBE-Mode (Graphics)
		default:
		{
			// Get active VBE information structure
			VbeMode_t *vbe = (VbeMode_t*)BootInfo->VbeModeInfo;

			// Copy information over
			__GlbVideoTerminal.Type = VIDEO_GRAPHICS;
			__GlbVideoTerminal.Info.FrameBufferAddress = vbe->PhysBasePtr;
			__GlbVideoTerminal.Info.Width = vbe->XResolution;
			__GlbVideoTerminal.Info.Height = vbe->YResolution;
			__GlbVideoTerminal.Info.Depth = vbe->BitsPerPixel;
			__GlbVideoTerminal.Info.BytesPerScanline = vbe->BytesPerScanLine;
			__GlbVideoTerminal.Info.RedPosition = vbe->RedMaskPos;
			__GlbVideoTerminal.Info.BluePosition = vbe->BlueMaskPos;
			__GlbVideoTerminal.Info.GreenPosition = vbe->GreenMaskPos;
			__GlbVideoTerminal.Info.ReservedPosition = vbe->ReservedMaskPos;
			__GlbVideoTerminal.Info.RedMask = vbe->RedMaskSize;
			__GlbVideoTerminal.Info.BlueMask = vbe->BlueMaskSize;
			__GlbVideoTerminal.Info.GreenMask = vbe->GreenMaskSize;
			__GlbVideoTerminal.Info.ReservedMask = vbe->ReservedMaskSize;

			// Clear out background (to white)
			memset((void*)__GlbVideoTerminal.Info.FrameBufferAddress, 0xFF,
				(__GlbVideoTerminal.Info.BytesPerScanline * __GlbVideoTerminal.Info.Height));

			__GlbVideoTerminal.CursorLimitX = __GlbVideoTerminal.Info.Width;
			__GlbVideoTerminal.CursorLimitY = __GlbVideoTerminal.Info.Height;
			__GlbVideoTerminal.FgColor = 0;
			__GlbVideoTerminal.BgColor = 0xFFFFFFFF;

		} break;
	}
}

/* VesaDrawPixel
 * Uses the vesa-interface to plot a single pixel */
OsStatus_t 
VesaDrawPixel(
	_In_ unsigned X, 
	_In_ unsigned Y, 
	_In_ uint32_t Color)
{
	// Variables
	uint32_t *VideoPtr = NULL;

	// Calculate the video-offset
	VideoPtr = (uint32_t*)
		(__GlbVideoTerminal.Info.FrameBufferAddress 
			+ ((Y * __GlbVideoTerminal.Info.BytesPerScanline)
		+ (X * (__GlbVideoTerminal.Info.Depth / 8))));

	// Set the pixel
	(*VideoPtr) = (0xFF000000 | Color);

	// No error
	return OsSuccess;
}

/* VesaDrawCharacter
 * Renders a ASCII/UTF16 character at the given pixel-position
 * on the screen */
OsStatus_t 
VesaDrawCharacter(
	_In_ unsigned CursorX,
	_In_ unsigned CursorY,
	_In_ int Character,
	_In_ uint32_t FgColor, 
	_In_ uint32_t BgColor)
{
	// Variables
	uint32_t *vPtr = NULL;
	uint8_t *ChPtr = NULL;
	unsigned Row, i = (unsigned)Character;

	// Calculate the video-offset
	vPtr = (uint32_t*)(__GlbVideoTerminal.Info.FrameBufferAddress 
		+ ((CursorY * __GlbVideoTerminal.Info.BytesPerScanline)
		+ (CursorX * (__GlbVideoTerminal.Info.Depth / 8))));

	// If it's unicode lookup index
#ifdef UNICODE
	for (i = 0; i < MCoreFontNumChars; i++) {
		if (MCoreFontIndex[i] == (uint16_t)Character)
			break;
    }
    if (i == MCoreFontNumChars) {
        // Not found
        return OsError;
    }
#endif

	// Lookup bitmap
	ChPtr = (uint8_t*)&MCoreFontBitmaps[i * MCoreFontHeight];

	// Iterate bitmap rows
	for (Row = 0; Row < MCoreFontHeight; Row++) {
		uint8_t BmpData = ChPtr[Row];
		uint32_t _;

		// Render data in row
		for (i = 0; i < 8; i++) {
			vPtr[i] = (BmpData >> (7 - i)) & 0x1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);
		}

		// Increase the memory pointer by row
		_ = (uint32_t)vPtr;
		_ += __GlbVideoTerminal.Info.BytesPerScanline;
		vPtr = (uint32_t*)_;
	}

	// Done - no errors
	return OsSuccess;
}

/* VesaScroll
 * Scrolls the terminal <n> lines up by using the
 * vesa-interface */
OsStatus_t 
VesaScroll(
	_In_ int ByLines)
{
	// Variables
	uint8_t *VideoPtr = NULL;
	size_t BytesToCopy = 0;
	int Lines = 0;
	int i = 0, j = 0;

	// How many lines do we need to modify?
	Lines = (__GlbVideoTerminal.CursorLimitY - __GlbVideoTerminal.CursorStartY);

	// Calculate the initial screen position
	VideoPtr = (uint8_t*)(__GlbVideoTerminal.Info.FrameBufferAddress +
		((__GlbVideoTerminal.CursorStartY * __GlbVideoTerminal.Info.BytesPerScanline)
			+ (__GlbVideoTerminal.CursorStartX * (__GlbVideoTerminal.Info.Depth / 8))));

	// Calculate num of bytes
	BytesToCopy = ((__GlbVideoTerminal.CursorLimitX - __GlbVideoTerminal.CursorStartX)
		* (__GlbVideoTerminal.Info.Depth / 8));

	// Do the actual scroll
	for (i = 0; i < ByLines; i++) {
		for (j = 0; j < Lines; j++) {
			memcpy(VideoPtr, VideoPtr +
				(__GlbVideoTerminal.Info.BytesPerScanline * MCoreFontHeight), BytesToCopy);
			VideoPtr += __GlbVideoTerminal.Info.BytesPerScanline;
		}
	}

	// Clear out the lines that was scrolled
	VideoPtr = (uint8_t*)(__GlbVideoTerminal.Info.FrameBufferAddress +
		((__GlbVideoTerminal.CursorStartX * (__GlbVideoTerminal.Info.Depth / 8))));

	// Scroll pointer down to bottom - n lines
	VideoPtr += (__GlbVideoTerminal.Info.BytesPerScanline 
		* (__GlbVideoTerminal.CursorLimitY - (MCoreFontHeight * ByLines)));

	// Clear out lines
	for (i = 0; i < ((int)MCoreFontHeight * ByLines); i++) {
		memset(VideoPtr, 0xFF, BytesToCopy);
		VideoPtr += __GlbVideoTerminal.Info.BytesPerScanline;
	}

	// We did the scroll, modify cursor
	__GlbVideoTerminal.CursorY -= (MCoreFontHeight * ByLines);

	// No errors
	return OsSuccess;
}

/* VesaPutCharacter
 * Uses the vesa-interface to print a new character
 * at the current terminal position */
OsStatus_t 
VesaPutCharacter(
	_In_ int Character)
{
	// Acquire terminal lock
	SpinlockAcquire(&__GlbVideoTerminal.Lock);

	// The first step is to handle special
	// case characters that we shouldn't print out
	switch (Character) 
	{
		// New-Line
		// Reset X and increase Y
	case '\n': {
		__GlbVideoTerminal.CursorX = __GlbVideoTerminal.CursorStartX;
		__GlbVideoTerminal.CursorY += MCoreFontHeight;
	} break;

	// Carriage Return
	// Reset X don't increase Y
	case '\r': {
		__GlbVideoTerminal.CursorX = __GlbVideoTerminal.CursorStartX;
	} break;

	// Default
	// Printable character
	default: {
		// Call print with the current location
		// and use the current colors
		VesaDrawCharacter(__GlbVideoTerminal.CursorX, __GlbVideoTerminal.CursorY,
			Character, __GlbVideoTerminal.FgColor, __GlbVideoTerminal.BgColor);
		__GlbVideoTerminal.CursorX += (MCoreFontWidth + 1);
	} break;
	}

	// Next step is to do some post-print
	// checks, including new-line and scroll-checks

	// Are we at last X position? - New-line
	if ((__GlbVideoTerminal.CursorX + (MCoreFontWidth + 1)) >= __GlbVideoTerminal.CursorLimitX) {
		__GlbVideoTerminal.CursorX = __GlbVideoTerminal.CursorStartX;
		__GlbVideoTerminal.CursorY += MCoreFontHeight;
	}

	// Do we need to scroll the terminal?
	if ((__GlbVideoTerminal.CursorY + MCoreFontHeight) >= __GlbVideoTerminal.CursorLimitY) {
		VesaScroll(1);
	}

	// Release lock and return OK
	SpinlockRelease(&__GlbVideoTerminal.Lock);
	return OsSuccess;
}

/* TextDrawCharacter
 * Renders an ASCII character at the given text-position
 * on the screen by the given color combination */
OsStatus_t 
TextDrawCharacter(
	_In_ int Character,
	_In_ unsigned CursorY,
	_In_ unsigned CursorX,
	_In_ uint8_t Color)
{
	// Variables
	uint16_t *Video = NULL;
	uint16_t Data = ((uint16_t)Color << 8) | (uint8_t)(Character & 0xFF);

	// Calculate video position
	Video = (uint16_t*)__GlbVideoTerminal.Info.FrameBufferAddress +
		(CursorY * __GlbVideoTerminal.Info.Width + CursorX);

	// Plot it on the screen
	*Video = Data;

	// Done - no errors
	return OsSuccess;
}

/* TextScroll
 * Scrolls the terminal <n> lines up by using the
 * text-interface */
OsStatus_t 
TextScroll(
	_In_ int ByLines)
{
	// Variables
	uint16_t *Video = (uint16_t*)__GlbVideoTerminal.Info.FrameBufferAddress;
	uint16_t Color = (uint16_t)(__GlbVideoTerminal.FgColor << 8);
	unsigned i;
	int j;

	// Move display n lines up
	for (j = 0; j < ByLines; j++) {
		for (i = 0; i < (__GlbVideoTerminal.Info.Height - 1) * __GlbVideoTerminal.Info.Width;
			i++) {
			Video[i] = Video[i + __GlbVideoTerminal.Info.Width];
		}

		// Clear last line
		for (i = ((__GlbVideoTerminal.Info.Height - 1) * __GlbVideoTerminal.Info.Width);
			i < (__GlbVideoTerminal.Info.Height * __GlbVideoTerminal.Info.Width);
			i++) {
			Video[i] = (uint16_t)(Color | ' ');
		}
	}

	// Update new Y cursor position
	__GlbVideoTerminal.CursorY = (__GlbVideoTerminal.Info.Height - ByLines);

	// Done - no errors
	return OsSuccess;
}

/* TextPutCharacter
 * Uses the text-interface to print a new character
 * at the current terminal position */
OsStatus_t 
TextPutCharacter(
	_In_ int Character)
{
	// Variables
	uint16_t CursorLoc = 0;

	// Acquire terminal lock
	SpinlockAcquire(&__GlbVideoTerminal.Lock);

	// Special case characters
	// Backspace
	if (Character == 0x08 && __GlbVideoTerminal.CursorX)
		__GlbVideoTerminal.CursorX--;

	// Tab
	else if (Character == 0x09)
		__GlbVideoTerminal.CursorX = ((__GlbVideoTerminal.CursorX + 8) & ~(8 - 1));

	// Carriage Return
	else if (Character == '\r')
		__GlbVideoTerminal.CursorX = 0;

	// New Line
	else if (Character == '\n') {
		__GlbVideoTerminal.CursorX = 0;
		__GlbVideoTerminal.CursorY++;
	}
	
	// Printable characters
	else if (Character >= ' ') {
		TextDrawCharacter(Character, __GlbVideoTerminal.CursorY, 
			__GlbVideoTerminal.CursorX, LOBYTE(LOWORD(__GlbVideoTerminal.FgColor)));
		__GlbVideoTerminal.CursorX++;
	}

	// Go to new line?
	if (__GlbVideoTerminal.CursorX >= __GlbVideoTerminal.Info.Width) {
		__GlbVideoTerminal.CursorX = 0;
		__GlbVideoTerminal.CursorY++;
	}

	// Scroll if at last line
	if (__GlbVideoTerminal.CursorY >= __GlbVideoTerminal.Info.Height) {
		TextScroll(1);
	}

	// Update HW Cursor
	CursorLoc = (uint16_t)((__GlbVideoTerminal.CursorY * __GlbVideoTerminal.Info.Width) 
		+ __GlbVideoTerminal.CursorX);

	// Send the high byte.
	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(CursorLoc >> 8));

	// Send the low byte.
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)CursorLoc);

	// Release lock and return
	SpinlockRelease(&__GlbVideoTerminal.Lock);
	return OsSuccess;
}

/* VideoGetTerminal
 * Retrieves the current terminal information */
BootTerminal_t*
VideoGetTerminal(void)
{
	// Simply return the static
	return &__GlbVideoTerminal;
}

/* VideoDrawPixel
 * Draws a pixel of the given color at the specified
 * pixel-position */
OsStatus_t
VideoDrawPixel(
	_In_ unsigned X,
	_In_ unsigned Y,
	_In_ uint32_t Color)
{
	// Start out by determining the kind
	// of draw we want to do
	switch (VideoGetTerminal()->Type)
	{
		// Text-Mode
	case VIDEO_TEXT:
		return OsError;

		// VBE
	case VIDEO_GRAPHICS:
		return VesaDrawPixel(X, Y, Color);

		// No video?
	case VIDEO_NONE:
		return OsError;
	}

	// Uh?
	return OsError;
}

/* VideoDrawCharacter
 * Renders a character of the given color(s) 
 * at the specified pixel-position */
OsStatus_t
VideoDrawCharacter(
	_In_ unsigned X,
	_In_ unsigned Y,
	_In_ int Character,
	_In_ uint32_t Bg,
	_In_ uint32_t Fg)
{
	// Start out by determining the kind
	// of draw we want to do
	switch (VideoGetTerminal()->Type)
	{
		// Text-Mode
	case VIDEO_TEXT:
		return TextDrawCharacter(Character, Y, X, LOBYTE(LOWORD(__GlbVideoTerminal.FgColor)));

		// VBE
	case VIDEO_GRAPHICS:
		return VesaDrawCharacter(X, Y, Character, Fg, Bg);

		// No video?
	case VIDEO_NONE:
		return OsError;
	}

	// Uh?
	return OsError;
}

/* VideoPutCharacter
 * Renders a character with default colors
 * at the current terminal position */
OsStatus_t
VideoPutCharacter(
	_In_ int Character)
{
	// Start out by determining the kind
	// of draw we want to do
	switch (VideoGetTerminal()->Type)
	{
		// Text-Mode
	case VIDEO_TEXT:
		return TextPutCharacter(Character);

		// VBE
	case VIDEO_GRAPHICS:
		return VesaPutCharacter(Character);

		// No video?
	case VIDEO_NONE:
		return OsError;
	}

	// Uh?
	return OsError;
}
