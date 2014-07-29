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
#include <arch.h>
#include <video.h>
#include <multiboot.h>
#include <string.h>

/* Externs */
extern const unsigned char x86_font_8x16[];

/* We have no memory allocation system in place yet,
 * so we allocate some static memory */
graphics_t gfx_info;
tty_t term;

/* We read the multiboot header for video 
 * information and setup the terminal accordingly */
void video_init(void *bootinfo)
{
	/* Cast */
	multiboot_info_t *mboot = (multiboot_info_t*)bootinfo;

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
			vbe_mode_t *vbe = (vbe_mode_t*)mboot->VbeControllerInfo;

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
	spinlock_reset(&term.lock);
}

int video_putchar(int character)
{
	/* Decls */
	uint32_t *video_ptr;
	uint8_t *ch_ptr;
	uint32_t row, i;

	/* Get spinlock */
	spinlock_acquire(&term.lock);

	/* Handle Special Characters */
	switch (character)
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
			ch_ptr = (uint8_t*)&x86_font_8x16[character * 16];

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
	spinlock_release(&term.lock);

	return character;
}