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
#include <Arch.h>
#include <Video.h>
#include <Multiboot.h>
#include <string.h>

/* Externs */
extern const uint8_t x86Font8x16[];

/* We have no memory allocation system in place yet,
 * so we allocate some static memory */
const char *GlbBootVideoWindowTitle = "MollenOS Boot Log";
Graphics_t GfxInformation;
TTY_t TtyInfo;

/* Helper */
int __abs(int i) {
	return (i < 0 ? -i : i);
}

/* Draw Pixel */
void VideoDrawPixel(uint32_t X, uint32_t Y, uint32_t Color)
{
	/* Vars */
	uint32_t *VideoPtr;

	/* Calculate video offset */
	VideoPtr = (uint32_t*)(GfxInformation.VideoAddr + ((Y * GfxInformation.BytesPerScanLine)
		+ (X * (GfxInformation.BitsPerPixel / 8))));

	/* Set */
	(*VideoPtr) = (0xFF000000 | Color);
}

/* Draw Line */
void VideoDrawLine(uint32_t StartX, uint32_t StartY, uint32_t EndX, uint32_t EndY, uint32_t Color)
{
	int dx = __abs(EndX - StartX), sx = StartX < EndX ? 1 : -1;
	int dy = __abs(EndY - StartY), sy = StartY < EndY ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2, e2;

	for (;;) {
		VideoDrawPixel(StartX, StartY, Color);
		if (StartX == EndX && StartY == EndY) break;
		e2 = err;
		if (e2 >-dx) { err -= dy; StartX += sx; }
		if (e2 < dy) { err += dx; StartY += sy; }
	}
}

/* Base write */
void VideoPutCharAtLocationVesa(int Character, int CursorY, int CursorX, uint32_t FgColor, uint32_t BgColor)
{
	/* Decls */
	uint32_t *video_ptr;
	uint8_t *ch_ptr;
	uint32_t row, i;

	/* Calculate video offset */
	video_ptr = (uint32_t*)(GfxInformation.VideoAddr + ((CursorY * GfxInformation.BytesPerScanLine)
		+ (CursorX * (GfxInformation.BitsPerPixel / 8))));
	ch_ptr = (uint8_t*)&x86Font8x16[Character * 16];

	for (row = 0; row < 16; row++)
	{
		uint8_t data = ch_ptr[row];
		uint32_t _;

		for (i = 0; i < 8; i++)
			video_ptr[i] = (data >> (7 - i)) & 1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);

		_ = (uint32_t)video_ptr;
		_ += GfxInformation.BytesPerScanLine;
		video_ptr = (uint32_t*)_;
	}
}

/* Draw Window */
void VideoDrawBootTerminal(uint32_t X, uint32_t Y, uint32_t Width, uint32_t Height)
{
	/* Title pointer */
	uint32_t TitleStartX = X + (Width / 3);
	uint32_t TitleStartY = Y + 8;
	int i;
	char *TitlePtr = (char*)GlbBootVideoWindowTitle;

	/* Draw Borders */
	for (i = 0; i < 32; i++)
		VideoDrawLine(X, Y + i, X + Width, Y + i, 0x7F8C8D);
	VideoDrawLine(X, Y, X, Y + Height, 0x7F8C8D);
	VideoDrawLine(X + Width, Y, X + Width, Y + Height, 0x7F8C8D);
	VideoDrawLine(X, Y + Height, X + Width, Y + Height, 0x7F8C8D);

	/* Draw Title */
	while (*TitlePtr)
	{
		char Char = *TitlePtr;
		VideoPutCharAtLocationVesa((int)Char, TitleStartY, TitleStartX, 0xD35400, 0x7F8C8D);
		TitleStartX += 10;

		TitlePtr++;
	}

	/* Define Virtual Borders */
	TtyInfo.CursorX = TtyInfo.CursorStartX = X + 11;
	TtyInfo.CursorLimitX = X + Width - 1;
	TtyInfo.CursorY = TtyInfo.CursorStartY = Y + 33;
	TtyInfo.CursorLimitY = Y + Height - 17;
}

/* We read the multiboot header for video 
 * information and setup the terminal accordingly */
OsStatus_t VideoInit(void *BootInfo)
{
	/* Cast */
	Multiboot_t *mboot = (Multiboot_t*)BootInfo;

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

		} break;
		case 2:
		{
			/* This means VGA Mode */
			//N/A

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

			/* Clear background */
			memset((void*)GfxInformation.VideoAddr, 0xFF, (GfxInformation.BytesPerScanLine * GfxInformation.ResY));

		} break;
	}

	/* Initialise the TTY structure */
	TtyInfo.CursorX = 0;
	TtyInfo.CursorY = 0;
	TtyInfo.CursorStartX = 0;
	TtyInfo.CursorStartY = 0;
	TtyInfo.CursorLimitX = GfxInformation.ResX / 10;
	TtyInfo.CursorLimitY = GfxInformation.ResY / 16;
	TtyInfo.FgColor = (0 << 4) | (15 & 0x0F);
	TtyInfo.BgColor = 0;

	/* Draw boot terminal */
	if (GfxInformation.GraphicMode == 3)
		VideoDrawBootTerminal((GfxInformation.ResX / 2) - 290, (GfxInformation.ResY / 2) - 210, 580, 420);
	
	/* Prepare spinlock */
	SpinlockReset(&TtyInfo.Lock);

	/* Done */
	return OS_STATUS_OK;
}

/* Write a character in VESA mode */
int VideoPutCharVesa(int Character)
{
	/* Get spinlock */
	SpinlockAcquire(&TtyInfo.Lock);

	/* Handle Special Characters */
	switch (Character)
	{
		/* New line */
		case '\n':
		{
			TtyInfo.CursorX = TtyInfo.CursorStartX;
			TtyInfo.CursorY += 16;
		} break;

		/* Carriage Return */
		case '\r':
		{
			TtyInfo.CursorX = TtyInfo.CursorStartX;
		} break;

		/* Default */
		default:
		{
			/* Print */
			VideoPutCharAtLocationVesa(Character, TtyInfo.CursorY, TtyInfo.CursorX, 0, 0xFFFFFFFF);

			/* Increase position */
			TtyInfo.CursorX += 10;
		} break;
	}

	/* Newline check */
	if ((TtyInfo.CursorX + 10) >= TtyInfo.CursorLimitX)
	{
		TtyInfo.CursorX = TtyInfo.CursorStartX;
		TtyInfo.CursorY += 16;
	}

	/* Do scroll check here */
	if ((TtyInfo.CursorY + 16) >= TtyInfo.CursorLimitY)
	{
		/* Calculate video offset */
		uint8_t *VideoPtr;
		uint32_t BytesToCopy;
		int i = 0;
		int Lines = (TtyInfo.CursorLimitY - TtyInfo.CursorStartY);
		VideoPtr = (uint8_t*)(GfxInformation.VideoAddr + ((TtyInfo.CursorStartY * GfxInformation.BytesPerScanLine)
			+ (TtyInfo.CursorStartX * (GfxInformation.BitsPerPixel / 8))));
		BytesToCopy = ((TtyInfo.CursorLimitX - TtyInfo.CursorStartX) * (GfxInformation.BitsPerPixel / 8));

		/* Do a scroll */
		for (i = 0; i < Lines; i++)
		{
			/* Copy a line up */
			memcpy(VideoPtr, VideoPtr + (GfxInformation.BytesPerScanLine * 16), BytesToCopy);

			/* Increament */
			VideoPtr += GfxInformation.BytesPerScanLine;
		}

		/* Clear */
		VideoPtr = (uint8_t*)(GfxInformation.VideoAddr + ((TtyInfo.CursorStartY * GfxInformation.BytesPerScanLine)
			+ (TtyInfo.CursorStartX * (GfxInformation.BitsPerPixel / 8))));

		for (i = 0; i < 16; i++)
		{
			/* Clear low line */
			memset((uint8_t*)(VideoPtr + (GfxInformation.BytesPerScanLine * (TtyInfo.CursorLimitY - 16))),
				0xFF, BytesToCopy);

			/* Increament */
			VideoPtr += GfxInformation.BytesPerScanLine;
		}
		
		//We scrolled, set it back one line.
		TtyInfo.CursorY -= 16;
	}

	/* Release spinlock */
	SpinlockRelease(&TtyInfo.Lock);

	return Character;
}

/* Write a character in TEXT mode */
int VideoPutCharText(int Character)
{
	uint16_t attribute = (uint16_t)(TtyInfo.FgColor << 8);
	uint16_t cursorLocation = 0;

	/* Get spinlock */
	SpinlockAcquire(&TtyInfo.Lock);

	/* Check special characters */
	if (Character == 0x08 && TtyInfo.CursorX)		//Backspace
		TtyInfo.CursorX--;
	else if (Character == 0x09)					//Tab
		TtyInfo.CursorX = (uint32_t)((TtyInfo.CursorX + 8) & ~(8 - 1));
	else if (Character == '\r')					//Carriage return
		TtyInfo.CursorX = 0;				//New line
	else if (Character == '\n') {
		TtyInfo.CursorX = 0;
		TtyInfo.CursorY++;
	}
	//Printable characters
	else if (Character >= ' ')
	{
		uint16_t* location = (uint16_t*)GfxInformation.VideoAddr + (TtyInfo.CursorY * GfxInformation.ResX + TtyInfo.CursorX);
		*location = (uint16_t)(Character | attribute);
		TtyInfo.CursorX++;
	}

	//Go to new line?
	if (TtyInfo.CursorX >= GfxInformation.ResX) {

		TtyInfo.CursorX = 0;
		TtyInfo.CursorY++;
	}

	//Scroll if at last line
	if (TtyInfo.CursorY >= GfxInformation.ResY)
	{
		uint16_t attribute = (uint16_t)(TtyInfo.FgColor << 8);
		uint16_t *vid_mem = (uint16_t*)GfxInformation.VideoAddr;
		uint16_t i;

		//Move display one line up
		for (i = 0 * GfxInformation.ResX; i < (GfxInformation.ResY - 1) * GfxInformation.ResX; i++)
			vid_mem[i] = vid_mem[i + GfxInformation.ResX];

		//Clear last line
		for (i = (GfxInformation.ResY - 1) * GfxInformation.ResX; i < (GfxInformation.ResY * GfxInformation.ResX); i++)
			vid_mem[i] = (uint16_t)(attribute | ' ');

		TtyInfo.CursorY = (GfxInformation.ResY - 1);
	}

	//Update HW Cursor
	cursorLocation = (uint16_t)((TtyInfo.CursorY * GfxInformation.ResX) + TtyInfo.CursorX);

	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(cursorLocation >> 8)); // Send the high byte.
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)cursorLocation);      // Send the low byte.

	/* Release spinlock */
	SpinlockRelease(&TtyInfo.Lock);

	return Character;
}

/* PutChar Wrapper */
int VideoPutChar(int Character)
{
	if (GfxInformation.GraphicMode == 0 || GfxInformation.GraphicMode == 1)
		VideoPutCharText(Character);
	else
		VideoPutCharVesa(Character);

	/* Done */
	return Character;
}