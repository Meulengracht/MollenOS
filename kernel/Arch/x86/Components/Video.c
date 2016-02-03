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
* MollenOS x86-32 Boot-Video Component
*/

/* Includes */
#include <MollenOS.h>
#include <DeviceManager.h>
#include <Devices/Video.h>
#include "../Arch.h"
#include <Video.h>
#include <Multiboot.h>
#include <string.h>
#include <stddef.h>

/* Globals */
MCoreDevice_t GlbBootVideoDevice = { 0 };
MCoreVideoDevice_t GlbBootVideo = { 0 };
const char *GlbBootDriverName = "VESA Video Device";

/* Externs (Import from MCore) */
extern const uint8_t MCoreFontBitmaps[];
extern const uint32_t MCoreFontNumChars;
extern const uint32_t MCoreFontHeight;
extern const uint32_t MCoreFontWidth;

#ifdef UNICODE
extern const uint16_t MCoreFontIndex[];
#endif

/* Draw Pixel */
void _VideoDrawPixelVesa(void *VideoData, uint32_t X, uint32_t Y, uint32_t Color)
{
	/* Vars */
	MCoreVideoDevice_t *vDevice = (MCoreVideoDevice_t*)VideoData;
	uint32_t *VideoPtr;

	/* Calculate video offset */
	VideoPtr = (uint32_t*)(vDevice->Info.FrameBufferAddr + ((Y * vDevice->Info.BytesPerScanline)
		+ (X * (vDevice->Info.Depth / 8))));

	/* Set */
	(*VideoPtr) = (0xFF000000 | Color);
}

/* Base write */
void _VideoPutCharAtLocationVesa(void *VideoData, 
	int Character, uint32_t CursorY, uint32_t CursorX, uint32_t FgColor, uint32_t BgColor)
{
	/* Decls */
	uint32_t *vPtr;
	uint8_t *ChPtr;
	uint32_t Row, i;

	/* Cast */
	MCoreVideoDevice_t *vDevice = (MCoreVideoDevice_t*)VideoData;

	/* Calculate video offset */
	vPtr = (uint32_t*)(vDevice->Info.FrameBufferAddr + ((CursorY * vDevice->Info.BytesPerScanline)
		+ (CursorX * (vDevice->Info.Depth / 8))));

	/* Cast */
	i = (uint32_t)Character;

#ifdef UNICODE
	/* Lookup Encoding */
	for (i = 0; i < MCoreFontNumChars; i++)
	{
		if (MCoreFontIndex[i] == (uint16_t)Character)
			break;
	}
#endif
	
	/* Get bitmap */
	ChPtr = (uint8_t*)&MCoreFontBitmaps[i * MCoreFontHeight];

	/* Iterate */
	for (Row = 0; Row < MCoreFontHeight; Row++)
	{
		/* Get bitmap data */
		uint8_t BmpData = ChPtr[Row];
		
		/* Used for offsetting vPtr */
		uint32_t _;

		/* Put data */
		for (i = 0; i < 8; i++)
			vPtr[i] = (BmpData >> (7 - i)) & 0x1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);

		/* Increase vPtr */
		_ = (uint32_t)vPtr;
		_ += vDevice->Info.BytesPerScanline;
		vPtr = (uint32_t*)_;
	}
}

/* Write a character in VESA mode */
int _VideoPutCharVesa(void *VideoData, int Character)
{
	/* Cast */
	MCoreVideoDevice_t *vDevice = (MCoreVideoDevice_t*)VideoData;

	/* Get spinlock */
	SpinlockAcquire(&vDevice->Lock);

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
		vDevice->DrawCharacter(VideoData, 
			Character, vDevice->CursorY, vDevice->CursorX, vDevice->FgColor, vDevice->BgColor);

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
		VideoPtr = (uint8_t*)(vDevice->Info.FrameBufferAddr + 
			((vDevice->CursorStartY * vDevice->Info.BytesPerScanline)
			+ (vDevice->CursorStartX * (vDevice->Info.Depth / 8))));
		BytesToCopy = ((vDevice->CursorLimitX - vDevice->CursorStartX) * (vDevice->Info.Depth / 8));

		/* Do a scroll */
		for (i = 0; i < Lines; i++)
		{
			/* Copy a line up */
			memcpy(VideoPtr, VideoPtr + (vDevice->Info.BytesPerScanline * MCoreFontHeight), BytesToCopy);

			/* Increament */
			VideoPtr += vDevice->Info.BytesPerScanline;
		}

		/* Clear */
		VideoPtr = (uint8_t*)(vDevice->Info.FrameBufferAddr + 
			((vDevice->CursorStartY * vDevice->Info.BytesPerScanline)
			+ (vDevice->CursorStartX * (vDevice->Info.Depth / 8))));

		for (i = 0; i < 16; i++)
		{
			/* Clear low line */
			memset((uint8_t*)(VideoPtr + 
				(vDevice->Info.BytesPerScanline * (vDevice->CursorLimitY - MCoreFontHeight))),
				0xFF, BytesToCopy);

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
int _VideoPutCharText(void *VideoData, int Character)
{
	MCoreVideoDevice_t *vDevice = (MCoreVideoDevice_t*)VideoData;
	uint16_t Attrib = (uint16_t)(vDevice->FgColor << 8);
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
		uint16_t* VidLoc = (uint16_t*)vDevice->Info.FrameBufferAddr +
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
		uint16_t *VidLoc = (uint16_t*)vDevice->Info.FrameBufferAddr;
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

/* We read the multiboot header for video 
 * information and setup the terminal accordingly */
void VideoInit(void *BootInfo)
{
	/* Cast */
	MCoreVideoDevice_t *vDevice = &GlbBootVideo;
	Multiboot_t *mboot = (Multiboot_t*)BootInfo;

	/* Do we have VESA or is this text mode? */
	switch (mboot->VbeMode)
	{
		case 0:
		{
			/* This means 80x25 text mode */
			vDevice->Info.Width = 80;
			vDevice->Info.Height = 25;
			vDevice->Info.Depth = 16;
			vDevice->Info.BytesPerScanline = 2 * 80;
			vDevice->Info.FrameBufferAddr = STD_VIDEO_MEMORY;

			/* Set */
			vDevice->Type = VideoTypeText;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = 80;
			vDevice->CursorLimitY = 25;
			vDevice->FgColor = (0 << 4) | (15 & 0x0F);
			vDevice->BgColor = 0;

			/* Set functions */
			vDevice->DrawPixel = NULL;
			vDevice->DrawCharacter = NULL;
			vDevice->Put = _VideoPutCharText;

		} break;
		case 1:
		{
			/* This means 80x50 text mode */
			vDevice->Info.Width = 80;
			vDevice->Info.Height = 50;
			vDevice->Info.Depth = 16;
			vDevice->Info.BytesPerScanline = 2 * 80;
			vDevice->Info.FrameBufferAddr = STD_VIDEO_MEMORY;

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
			vDevice->Info.FrameBufferAddr = vbe->ModeInfo_PhysBasePtr;
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
			memset((void*)vDevice->Info.FrameBufferAddr, 0xFF,
				(vDevice->Info.BytesPerScanline * vDevice->Info.Height));

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = vDevice->Info.Width;
			vDevice->CursorLimitY = vDevice->Info.Height;
			vDevice->FgColor = 0;
			vDevice->BgColor = 0xFFFFFFFF;

			/* Set functions */
			vDevice->DrawPixel = _VideoDrawPixelVesa;
			vDevice->DrawCharacter = _VideoPutCharAtLocationVesa;
			vDevice->Put = _VideoPutCharVesa;

		} break;
	}

	/* Setup structure */
	GlbBootVideoDevice.Type = DeviceVideo;
	GlbBootVideoDevice.BusDevice = NULL;
	GlbBootVideoDevice.Data = vDevice;
	GlbBootVideoDevice.Driver.Name = (char*)GlbBootDriverName;
	GlbBootVideoDevice.Driver.Version = 1;
	GlbBootVideoDevice.Driver.Status = DriverActive;
	GlbBootVideoDevice.Driver.Data = NULL;

	/* Register boot video */
	DmRegisterBootVideo(&GlbBootVideoDevice);
}