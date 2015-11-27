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
#include <Devices/Video.h>
#include <Arch.h>
#include <Video.h>
#include <Multiboot.h>
#include <string.h>
#include <stddef.h>

/* Externs (Import from MCore) */
extern const uint8_t MCoreFontBitmaps[];
extern const uint32_t MCoreFontNumChars;
extern const uint32_t MCoreFontHeight;
extern const uint32_t MCoreFontWidth;

#ifdef UNICODE
extern const uint16_t MCoreFontIndex[];
#endif

/* We have no memory allocation system in place yet,
 * so we allocate some static memory */
Graphics_t GfxInformation;

/* Draw Pixel */
void _VideoDrawPixelVesa(void *VideoData, uint32_t X, uint32_t Y, uint32_t Color)
{
	/* Vars */
	Graphics_t *Gfx = (Graphics_t*)VideoData;
	uint32_t *VideoPtr;

	/* Calculate video offset */
	VideoPtr = (uint32_t*)(Gfx->VideoAddr + ((Y * Gfx->BytesPerScanLine)
		+ (X * (Gfx->BitsPerPixel / 8))));

	/* Set */
	(*VideoPtr) = (0xFF000000 | Color);
}

/* Base write */
void _VideoPutCharAtLocationVesa(void *VideoData, 
	int Character, uint32_t CursorY, uint32_t CursorX, uint32_t FgColor, uint32_t BgColor)
{
	_CRT_UNUSED(VideoData);

	/* Decls */
	uint32_t *vPtr;
	uint8_t *ChPtr;
	uint32_t Row, i;

	/* Calculate video offset */
	vPtr = (uint32_t*)(GfxInformation.VideoAddr + ((CursorY * GfxInformation.BytesPerScanLine)
		+ (CursorX * (GfxInformation.BitsPerPixel / 8))));

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
		_ += GfxInformation.BytesPerScanLine;
		vPtr = (uint32_t*)_;
	}
}

/* Write a character in VESA mode */
int _VideoPutCharVesa(void *VideoData, int Character)
{
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
		VideoPtr = (uint8_t*)(GfxInformation.VideoAddr + ((vDevice->CursorStartY * GfxInformation.BytesPerScanLine)
			+ (vDevice->CursorStartX * (GfxInformation.BitsPerPixel / 8))));
		BytesToCopy = ((vDevice->CursorLimitX - vDevice->CursorStartX) * (GfxInformation.BitsPerPixel / 8));

		/* Do a scroll */
		for (i = 0; i < Lines; i++)
		{
			/* Copy a line up */
			memcpy(VideoPtr, VideoPtr + (GfxInformation.BytesPerScanLine * MCoreFontHeight), BytesToCopy);

			/* Increament */
			VideoPtr += GfxInformation.BytesPerScanLine;
		}

		/* Clear */
		VideoPtr = (uint8_t*)(GfxInformation.VideoAddr + ((vDevice->CursorStartY * GfxInformation.BytesPerScanLine)
			+ (vDevice->CursorStartX * (GfxInformation.BitsPerPixel / 8))));

		for (i = 0; i < 16; i++)
		{
			/* Clear low line */
			memset((uint8_t*)(VideoPtr + 
				(GfxInformation.BytesPerScanLine * (vDevice->CursorLimitY - MCoreFontHeight))),
				0xFF, BytesToCopy);

			/* Increament */
			VideoPtr += GfxInformation.BytesPerScanLine;
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
	uint16_t attribute = (uint16_t)(vDevice->FgColor << 8);
	uint16_t cursorLocation = 0;

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
		uint16_t* location = (uint16_t*)GfxInformation.VideoAddr + 
			(vDevice->CursorY * GfxInformation.ResX + vDevice->CursorX);
		*location = (uint16_t)(Character | attribute);
		vDevice->CursorX++;
	}

	//Go to new line?
	if (vDevice->CursorX >= GfxInformation.ResX) {

		vDevice->CursorX = 0;
		vDevice->CursorY++;
	}

	//Scroll if at last line
	if (vDevice->CursorY >= GfxInformation.ResY)
	{
		uint16_t attribute = (uint16_t)(vDevice->FgColor << 8);
		uint16_t *vid_mem = (uint16_t*)GfxInformation.VideoAddr;
		uint16_t i;

		//Move display one line up
		for (i = 0 * GfxInformation.ResX; i < (GfxInformation.ResY - 1) * GfxInformation.ResX; i++)
			vid_mem[i] = vid_mem[i + GfxInformation.ResX];

		//Clear last line
		for (i = (GfxInformation.ResY - 1) * GfxInformation.ResX; i < (GfxInformation.ResY * GfxInformation.ResX); i++)
			vid_mem[i] = (uint16_t)(attribute | ' ');

		vDevice->CursorY = (GfxInformation.ResY - 1);
	}

	//Update HW Cursor
	cursorLocation = (uint16_t)((vDevice->CursorY * GfxInformation.ResX) + vDevice->CursorX);

	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(cursorLocation >> 8)); // Send the high byte.
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)cursorLocation);      // Send the low byte.

	/* Release spinlock */
	SpinlockRelease(&vDevice->Lock);

	return Character;
}

/* We read the multiboot header for video 
 * information and setup the terminal accordingly */
void _VideoSetup(void *VideoInfo, void *BootInfo)
{
	/* Cast */
	MCoreVideoDevice_t *vDevice = (MCoreVideoDevice_t*)VideoInfo;
	MCoreBootInfo_t *mCoreBoot = (MCoreBootInfo_t*)BootInfo;
	Multiboot_t *mboot = (Multiboot_t*)mCoreBoot->ArchBootInfo;

	/* Save info */
	vDevice->Data = (void*)&GfxInformation;

	/* Do we have VESA or is this text mode? */
	switch (mboot->VbeMode)
	{
		case 0:
		{
			/* This means 80x25 text mode */
			GfxInformation.ResX = 80;
			GfxInformation.ResY = 25;
			GfxInformation.BitsPerPixel = 16;
			GfxInformation.BytesPerScanLine = 2 * 80;
			GfxInformation.GraphicMode = 0;
			GfxInformation.VideoAddr = STD_VIDEO_MEMORY;

			/* Set */
			vDevice->Type = VideoTypeText;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = GfxInformation.ResX / (MCoreFontWidth + 2);
			vDevice->CursorLimitY = GfxInformation.ResY / MCoreFontHeight;
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
			GfxInformation.ResX = 80;
			GfxInformation.ResY = 50;
			GfxInformation.BitsPerPixel = 16;
			GfxInformation.GraphicMode = 1;
			GfxInformation.BytesPerScanLine = 2 * 80;
			GfxInformation.VideoAddr = STD_VIDEO_MEMORY;

			/* Set */
			vDevice->Type = VideoTypeText;

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = GfxInformation.ResX / (MCoreFontWidth + 2);
			vDevice->CursorLimitY = GfxInformation.ResY / MCoreFontHeight;
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
			vDevice->CursorLimitX = GfxInformation.ResX / (MCoreFontWidth + 2);
			vDevice->CursorLimitY = GfxInformation.ResY / MCoreFontHeight;
			vDevice->FgColor = (0 << 4) | (15 & 0x0F);
			vDevice->BgColor = 0;
		} break;

		default:
		{
			/* Assume VESA otherwise */

			/* Get info struct */
			VbeMode_t *vbe = (VbeMode_t*)mboot->VbeModeInfo;

			/* Fill our structure */
			GfxInformation.VideoAddr = vbe->ModeInfo_PhysBasePtr;
			GfxInformation.ResX = vbe->ModeInfo_XResolution;
			GfxInformation.ResY = vbe->ModeInfo_YResolution;
			GfxInformation.BitsPerPixel = vbe->ModeInfo_BitsPerPixel;
			GfxInformation.BytesPerScanLine = vbe->ModeInfo_BytesPerScanLine;
			GfxInformation.Attributes = vbe->ModeInfo_ModeAttributes;
			GfxInformation.DirectColorModeInfo = vbe->ModeInfo_DirectColorModeInfo;
			GfxInformation.RedMaskPos = vbe->ModeInfo_RedMaskPos;
			GfxInformation.BlueMaskPos = vbe->ModeInfo_BlueMaskPos;
			GfxInformation.GreenMaskPos = vbe->ModeInfo_GreenMaskPos;
			GfxInformation.ReservedMaskPos = vbe->ModeInfo_ReservedMaskPos;
			GfxInformation.RedMaskSize = vbe->ModeInfo_RedMaskSize;
			GfxInformation.BlueMaskSize = vbe->ModeInfo_BlueMaskSize;
			GfxInformation.GreenMaskSize = vbe->ModeInfo_GreenMaskSize;
			GfxInformation.ReservedMaskSize = vbe->ModeInfo_ReservedMaskSize;
			GfxInformation.GraphicMode = 3;

			/* Set */
			vDevice->Type = VideoTypeLFB;

			/* Clear background */
			memset((void*)GfxInformation.VideoAddr, 0xFF, 
				(GfxInformation.BytesPerScanLine * GfxInformation.ResY));

			/* Initialise the TTY structure */
			vDevice->CursorLimitX = GfxInformation.ResX;
			vDevice->CursorLimitY = GfxInformation.ResY;
			vDevice->FgColor = 0;
			vDevice->BgColor = 0xFFFFFFFF;

			/* Set functions */
			vDevice->DrawPixel = _VideoDrawPixelVesa;
			vDevice->DrawCharacter = _VideoPutCharAtLocationVesa;
			vDevice->Put = _VideoPutCharVesa;

		} break;
	}
}