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
* MollenOS x86-32 Video Component
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
Graphics_t gfx_info;
TTY_t term;

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
			gfx_info.ResX = 80;
			gfx_info.ResY = 25;
			gfx_info.BitsPerPixel = 16;
			gfx_info.BytesPerScanLine = 2 * 80;
			gfx_info.GraphicMode = 0;
			gfx_info.VideoAddr = STD_VIDEO_MEMORY;

		} break;
		case 1:
		{
			/* This means 80x50 text mode */
			gfx_info.ResX = 80;
			gfx_info.ResY = 50;
			gfx_info.BitsPerPixel = 16;
			gfx_info.GraphicMode = 1;
			gfx_info.BytesPerScanLine = 2 * 80;
			gfx_info.VideoAddr = STD_VIDEO_MEMORY;

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
			gfx_info.VideoAddr = vbe->ModeInfo_PhysBasePtr;
			gfx_info.ResX = vbe->ModeInfo_XResolution;
			gfx_info.ResY = vbe->ModeInfo_YResolution;
			gfx_info.BitsPerPixel = vbe->ModeInfo_BitsPerPixel;
			gfx_info.BytesPerScanLine = vbe->ModeInfo_BytesPerScanLine;
			gfx_info.Attributes = vbe->ModeInfo_ModeAttributes;
			gfx_info.DirectColorModeInfo = vbe->ModeInfo_DirectColorModeInfo;
			gfx_info.RedMaskPos = vbe->ModeInfo_RedMaskPos;
			gfx_info.BlueMaskPos = vbe->ModeInfo_BlueMaskPos;
			gfx_info.GreenMaskPos = vbe->ModeInfo_GreenMaskPos;
			gfx_info.ReservedMaskPos = vbe->ModeInfo_ReservedMaskPos;
			gfx_info.RedMaskSize = vbe->ModeInfo_RedMaskSize;
			gfx_info.BlueMaskSize = vbe->ModeInfo_BlueMaskSize;
			gfx_info.GreenMaskSize = vbe->ModeInfo_GreenMaskSize;
			gfx_info.ReservedMaskSize = vbe->ModeInfo_ReservedMaskSize;
			gfx_info.GraphicMode = 3;

		} break;
	}

	/* Initialise the TTY structure */
	term.CursorX = 0;
	term.CursorY = 0;
	term.CursorLimitX = gfx_info.ResX / 10;
	term.CursorLimitY = gfx_info.ResY / 16;
	term.FgColor = (0 << 4) | (15 & 0x0F);
	term.BgColor = 0;
	
	/* Prepare spinlock */
	SpinlockReset(&term.Lock);

	/* Done */
	return OS_STATUS_OK;
}

/* Write a character in VESA mode */
int VideoPutCharVesa(int Character)
{
	/* Decls */
	uint32_t *video_ptr;
	uint8_t *ch_ptr;
	uint32_t row, i;

	/* Get spinlock */
	SpinlockAcquire(&term.Lock);

	/* Handle Special Characters */
	switch (Character)
	{
		/* New line */
		case '\n':
		{
			term.CursorX = 0;
			term.CursorY += 16;
		} break;

		/* Carriage Return */
		case '\r':
		{
			term.CursorX = 0;
		} break;

		/* Default */
		default:
		{
			/* Calculate video offset */
			video_ptr = (uint32_t*)(gfx_info.VideoAddr + ((term.CursorY * gfx_info.BytesPerScanLine)
				+ (term.CursorX * (gfx_info.BitsPerPixel / 8))));
			ch_ptr = (uint8_t*)&x86Font8x16[Character * 16];

			for (row = 0; row < 16; row++)
			{
				uint8_t data = ch_ptr[row];
				uint32_t _;

				for (i = 0; i < 8; i++)
					video_ptr[i] = (data >> (7 - i)) & 1 ? 0xFFFFFFFF : 0;

				_ = (uint32_t)video_ptr;
				_ += gfx_info.BytesPerScanLine;
				video_ptr = (uint32_t*)_;
			}

			/* Increase position */
			term.CursorX += 10;
		} break;
	}

	/* Newline check */
	if ((term.CursorX + 10) >= gfx_info.ResX)
	{
		term.CursorX = 0;
		term.CursorY += 16;
	}

	/* Do scroll check here */
	if ((term.CursorY + 16) >= gfx_info.ResY)
	{
		memcpy((uint8_t*)gfx_info.VideoAddr,
			(uint8_t*)gfx_info.VideoAddr + (gfx_info.BytesPerScanLine * 16),
			(size_t)(gfx_info.BytesPerScanLine * (gfx_info.ResY - 16)));
		memset((uint8_t*)(gfx_info.VideoAddr + (gfx_info.BytesPerScanLine * (gfx_info.ResY - 16))),
			(int8_t)0,
			(size_t)gfx_info.BytesPerScanLine * 16);

		//We scrolled, set it back one line.
		term.CursorY -= 16;
	}

	/* Release spinlock */
	SpinlockRelease(&term.Lock);

	return Character;
}

/* Write a character in TEXT mode */
int VideoPutCharText(int Character)
{
	uint16_t attribute = (uint16_t)(term.FgColor << 8);
	uint16_t cursorLocation = 0;

	/* Get spinlock */
	SpinlockAcquire(&term.Lock);

	/* Check special characters */
	if (Character == 0x08 && term.CursorX)		//Backspace
		term.CursorX--;
	else if (Character == 0x09)					//Tab
		term.CursorX = (uint32_t)((term.CursorX + 8) & ~(8 - 1));
	else if (Character == '\r')					//Carriage return
		term.CursorX = 0;				//New line
	else if (Character == '\n') {
		term.CursorX = 0;
		term.CursorY++;
	}
	//Printable characters
	else if (Character >= ' ')
	{
		uint16_t* location = (uint16_t*)gfx_info.VideoAddr + (term.CursorY * gfx_info.ResX + term.CursorX);
		*location = (uint16_t)(Character | attribute);
		term.CursorX++;
	}

	//Go to new line?
	if (term.CursorX >= gfx_info.ResX) {

		term.CursorX = 0;
		term.CursorY++;
	}

	//Scroll if at last line
	if (term.CursorY >= gfx_info.ResY)
	{
		uint16_t attribute = (uint16_t)(term.FgColor << 8);
		uint16_t *vid_mem = (uint16_t*)gfx_info.VideoAddr;
		uint16_t i;

		//Move display one line up
		for (i = 0 * gfx_info.ResX; i < (gfx_info.ResY - 1) * gfx_info.ResX; i++)
			vid_mem[i] = vid_mem[i + gfx_info.ResX];

		//Clear last line
		for (i = (gfx_info.ResY - 1) * gfx_info.ResX; i < (gfx_info.ResY * gfx_info.ResX); i++)
			vid_mem[i] = (uint16_t)(attribute | ' ');

		term.CursorY = (gfx_info.ResY - 1);
	}

	//Update HW Cursor
	cursorLocation = (uint16_t)((term.CursorY * gfx_info.ResX) + term.CursorX);

	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(cursorLocation >> 8)); // Send the high byte.
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)cursorLocation);      // Send the low byte.

	/* Release spinlock */
	SpinlockRelease(&term.Lock);

	return Character;
}

/* PutChar Wrapper */
OsStatus_t video_putchar(int Character)
{
	if (gfx_info.GraphicMode == 0 || gfx_info.GraphicMode == 1)
		VideoPutCharText(Character);
	else
		VideoPutCharVesa(Character);

	/* Done */
	return OS_STATUS_OK;
}