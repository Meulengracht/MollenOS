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

/* We read the multiboot header for video 
 * information and setup the terminal accordingly */
void VideoInit(Multiboot_t *BootInfo)
{
	/* Cast */
	BootTerminal_t *vDevice = (BootTerminal_t*)&__GlbVideoTerminal;

	/* Do we have VESA or is this text mode? */
	switch (BootInfo->VbeMode)
	{
		case 0:
		{
			/* This means 80x25 text mode */
			vDevice->Info.Width = 80;
			vDevice->Info.Height = 25;
			vDevice->Info.Depth = 16;
			vDevice->Info.BytesPerScanline = 2 * 80;
			vDevice->Info.FrameBufferAddress = STD_VIDEO_MEMORY;

			/* Set */
			vDevice->Type = VideoTypeText;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = 80;
			vDevice->CursorLimitY = 25;
			vDevice->FgColor = (0 << 4) | (15 & 0x0F);
			vDevice->BgColor = 0;

		} break;
		case 1:
		{
			/* This means 80x50 text mode */
			vDevice->Info.Width = 80;
			vDevice->Info.Height = 50;
			vDevice->Info.Depth = 16;
			vDevice->Info.BytesPerScanline = 2 * 80;
			vDevice->Info.FrameBufferAddress = STD_VIDEO_MEMORY;

			/* Set */
			vDevice->Type = VideoTypeText;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = 80;
			vDevice->CursorLimitY = 50;
			vDevice->FgColor = (0 << 4) | (15 & 0x0F);
			vDevice->BgColor = 0;

			/* Set functions */
			vDevice->DrawPixel = NULL;
			vDevice->DrawCharacter = NULL;
			vDevice->Put = _VideoPutCharText;

		} break;
		case 2:
		{
			/* This means VGA Mode */
			//N/A

			/* Set */
			vDevice->Type = VideoTypeVGA;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = vDevice->Info.Width / (MCoreFontWidth + 2);
			vDevice->CursorLimitY = vDevice->Info.Height / MCoreFontHeight;
			vDevice->FgColor = (0 << 4) | (15 & 0x0F);
			vDevice->BgColor = 0;
		} break;

		default:
		{
			/* Assume VESA otherwise */

			/* Get info struct */
			VbeMode_t *vbe = (VbeMode_t*)mboot->VbeModeInfo;

			/* Fill our structure */
			vDevice->Info.FrameBufferAddress = vbe->ModeInfo_PhysBasePtr;
			vDevice->Info.Width = vbe->ModeInfo_XResolution;
			vDevice->Info.Height = vbe->ModeInfo_YResolution;
			vDevice->Info.Depth = vbe->ModeInfo_BitsPerPixel;
			vDevice->Info.BytesPerScanline = vbe->ModeInfo_BytesPerScanLine;
			vDevice->Info.RedPosition = vbe->ModeInfo_RedMaskPos;
			vDevice->Info.BluePosition = vbe->ModeInfo_BlueMaskPos;
			vDevice->Info.GreenPosition = vbe->ModeInfo_GreenMaskPos;
			vDevice->Info.ReservedPosition = vbe->ModeInfo_ReservedMaskPos;
			vDevice->Info.RedMask = vbe->ModeInfo_RedMaskSize;
			vDevice->Info.BlueMask = vbe->ModeInfo_BlueMaskSize;
			vDevice->Info.GreenMask = vbe->ModeInfo_GreenMaskSize;
			vDevice->Info.ReservedMask = vbe->ModeInfo_ReservedMaskSize;

			/* Set */
			vDevice->Type = VideoTypeLFB;

			/* Clear background */
			memset((void*)vDevice->Info.FrameBufferAddress, 0xFF,
				(vDevice->Info.BytesPerScanline * vDevice->Info.Height));

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = vDevice->Info.Width;
			vDevice->CursorLimitY = vDevice->Info.Height;
			vDevice->FgColor = 0;
			vDevice->BgColor = 0xFFFFFFFF;

		} break;
	}
}

/* VesaDrawPixel
 * Uses the vesa-interface to plot a single pixel */
OsStatus_t VesaDrawPixel(
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
	return OsNoError;
}

/* VesaDrawCharacter
 * Renders a ASCII/UTF16 character at the given pixel-position
 * on the screen */
OsStatus_t VesaDrawCharacter(
	_In_ int Character, 
	_In_ unsigned CursorY,
	_In_ unsigned CursorX, 
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
	return OsNoError;
}

/* VesaPutCharacter
 * Uses the vesa-interface to print a new character
 * at the current terminal position */
int VesaPutCharacter(int Character)
{
	/* Get spinlock */
	SpinlockAcquire(&__GlbVideoTerminal.Lock);

	/* Handle Special Characters */
	switch (Character)
	{
		/* New line */
	case '\n':
	{
		vDevice->CursorX = vDevice->CursorStartX;
		vDevice->CursorY += MCoreFontHeight;
	} break;

	/* Carriage Return */
	case '\r':
	{
		vDevice->CursorX = vDevice->CursorStartX;
	} break;

	/* Default */
	default:
	{
		/* Print */
		vDevice->DrawCharacter(Character,
			vDevice->CursorY, vDevice->CursorX, vDevice->FgColor, vDevice->BgColor);

		/* Increase position */
		vDevice->CursorX += (MCoreFontWidth + 2);
	} break;
	}

	/* Newline check */
	if ((vDevice->CursorX + (MCoreFontWidth + 2)) >= vDevice->CursorLimitX)
	{
		vDevice->CursorX = vDevice->CursorStartX;
		vDevice->CursorY += MCoreFontHeight;
	}

	/* Do scroll check here */
	if ((vDevice->CursorY + MCoreFontHeight) >= vDevice->CursorLimitY)
	{
		/* Calculate video offset */
		uint8_t *VideoPtr;
		uint32_t BytesToCopy;
		int i = 0;
		int Lines = (vDevice->CursorLimitY - vDevice->CursorStartY);
		VideoPtr = (uint8_t*)(vDevice->Info.FrameBufferAddress +
			((vDevice->CursorStartY * vDevice->Info.BytesPerScanline)
				+ (vDevice->CursorStartX * (vDevice->Info.Depth / 8))));
		BytesToCopy = ((vDevice->CursorLimitX - vDevice->CursorStartX) * (vDevice->Info.Depth / 8));

		/* Do a scroll */
		for (i = 0; i < Lines; i++)
		{
			/* Copy a line up */
			memcpy(VideoPtr, VideoPtr +
				(vDevice->Info.BytesPerScanline * MCoreFontHeight), BytesToCopy);

			/* Increament */
			VideoPtr += vDevice->Info.BytesPerScanline;
		}

		/* Clear
		* Get X0, Y0 */
		VideoPtr = (uint8_t*)(vDevice->Info.FrameBufferAddress +
			((vDevice->CursorStartX * (vDevice->Info.Depth / 8))));

		/* Add up to Y(MAX-Height) */
		VideoPtr += (vDevice->Info.BytesPerScanline * (vDevice->CursorLimitY - MCoreFontHeight));

		for (i = 0; i < (int)MCoreFontHeight; i++)
		{
			/* Clear low line */
			memset(VideoPtr, 0xFF, BytesToCopy);

			/* Increament */
			VideoPtr += vDevice->Info.BytesPerScanline;
		}

		//We scrolled, set it back one line.
		vDevice->CursorY -= MCoreFontHeight;
	}

	/* Release spinlock */
	SpinlockRelease(&vDevice->Lock);

	return Character;
}

/* Write a character in TEXT mode */
int TextPutCharacter(int Character)
{
	uint16_t Attrib = (uint16_t)(__GlbVideoTerminal.FgColor << 8);
	uint16_t CursorLoc = 0;

	/* Get spinlock */
	SpinlockAcquire(&vDevice->Lock);

	/* Check special characters */
	if (Character == 0x08 && vDevice->CursorX)		//Backspace
		vDevice->CursorX--;
	else if (Character == 0x09)					//Tab
		vDevice->CursorX = (uint32_t)((vDevice->CursorX + 8) & ~(8 - 1));
	else if (Character == '\r')					//Carriage return
		vDevice->CursorX = 0;				//New line
	else if (Character == '\n') {
		vDevice->CursorX = 0;
		vDevice->CursorY++;
	}
	//Printable characters
	else if (Character >= ' ')
	{
		uint16_t* VidLoc = (uint16_t*)vDevice->Info.FrameBufferAddress +
			(vDevice->CursorY * vDevice->Info.Width + vDevice->CursorX);
		*VidLoc = (uint16_t)(Character | Attrib);
		vDevice->CursorX++;
	}

	//Go to new line?
	if (vDevice->CursorX >= vDevice->Info.Width) {

		vDevice->CursorX = 0;
		vDevice->CursorY++;
	}

	//Scroll if at last line
	if (vDevice->CursorY >= vDevice->Info.Height)
	{
		uint16_t CharAttrib = (uint16_t)(vDevice->FgColor << 8);
		uint16_t *VidLoc = (uint16_t*)vDevice->Info.FrameBufferAddress;
		uint16_t i;

		//Move display one line up
		for (i = (uint16_t)(0 * vDevice->Info.Width); i < (uint16_t)(vDevice->Info.Height - 1) * vDevice->Info.Width; i++)
			VidLoc[i] = VidLoc[i + vDevice->Info.Width];

		//Clear last line
		for (i = (uint16_t)((vDevice->Info.Height - 1) * vDevice->Info.Width);
			i < (vDevice->Info.Height * vDevice->Info.Width); i++)
			VidLoc[i] = (uint16_t)(CharAttrib | ' ');

		vDevice->CursorY = (vDevice->Info.Height - 1);
	}

	//Update HW Cursor
	CursorLoc = (uint16_t)((vDevice->CursorY * vDevice->Info.Width) + vDevice->CursorX);

	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(CursorLoc >> 8)); // Send the high byte.
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)CursorLoc);      // Send the low byte.

										  /* Release spinlock */
	SpinlockRelease(&vDevice->Lock);

	return Character;
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
		return OsError;

		// VBE
	case VIDEO_GRAPHICS:
		return VesaDrawCharacter(Character, Y, X, Fg, Bg);

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
}
