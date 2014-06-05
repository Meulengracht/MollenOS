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
* MollenOS x86-32 Video Header
*/
#ifndef _X86_VIDEO_
#define _X86_VIDEO_


/* Includes */
#include <arch.h>
#include <stdint.h>

/* This is the VBE Graphic Information
* Descriptor which we have setup in
* the bootloader */
#pragma pack(push, 1)
typedef struct vbe_mode_info
{
	uint16_t ModeInfo_ModeAttributes;
	uint8_t ModeInfo_WinAAttributes;
	uint8_t ModeInfo_WinBAttributes;
	uint16_t ModeInfo_WinGranularity;
	uint16_t ModeInfo_WinSize;
	uint16_t ModeInfo_WinASegment;
	uint16_t ModeInfo_WinBSegment;
	uint32_t ModeInfo_WinFuncPtr;
	uint16_t ModeInfo_BytesPerScanLine;
	uint16_t ModeInfo_XResolution;
	uint16_t ModeInfo_YResolution;
	uint8_t ModeInfo_XCharSize;
	uint8_t ModeInfo_YCharSize;
	uint8_t ModeInfo_NumberOfPlanes;
	uint8_t ModeInfo_BitsPerPixel;
	uint8_t ModeInfo_NumberOfBanks;
	uint8_t ModeInfo_MemoryModel;
	uint8_t ModeInfo_BankSize;
	uint8_t ModeInfo_NumberOfImagePages;
	uint8_t ModeInfo_Reserved_page;
	uint8_t ModeInfo_RedMaskSize;
	uint8_t ModeInfo_RedMaskPos;
	uint8_t ModeInfo_GreenMaskSize;
	uint8_t ModeInfo_GreenMaskPos;
	uint8_t ModeInfo_BlueMaskSize;
	uint8_t ModeInfo_BlueMaskPos;
	uint8_t ModeInfo_ReservedMaskSize;
	uint8_t ModeInfo_ReservedMaskPos;
	uint8_t ModeInfo_DirectColorModeInfo;

	/* VBE 2.0 Extensions */
	uint32_t ModeInfo_PhysBasePtr;
	uint32_t ModeInfo_OffScreenMemOffset;
	uint16_t  ModeInfo_OffScreenMemSize;
} vbe_mode_t;
#pragma pack(pop)

/* The Graphic Info structure we save 
 * in the kernel. */
#pragma pack(push, 1)
typedef struct graphic_info
{
	//Graphics mode (Text, VGA, VESA, NATIVE)
	uint8_t GraphicMode;

	//Resolution
	uint16_t ResX;
	uint16_t ResY;

	//Screen info
	uint8_t BitsPerPixel;
	uint16_t BytesPerScanLine;

	//Video Address
	uint32_t VideoAddr;
	uint8_t DirectColorModeInfo;

	//Info
	uint8_t RedMaskSize;
	uint8_t RedMaskPos;
	uint8_t GreenMaskSize;
	uint8_t GreenMaskPos;
	uint8_t BlueMaskSize;
	uint8_t BlueMaskPos;
	uint8_t ReservedMaskSize;
	uint8_t ReservedMaskPos;
	uint16_t Attributes;

} graphics_t;
#pragma pack(pop)


/* The Graphic Terminal structure we save
* in the kernel. */
#pragma pack(push, 1)
typedef struct terminal
{
	/* Cursor Position */
	uint32_t CursorX;
	uint32_t CursorY;

	/* Cursor Limits */
	uint32_t CursorLimitX;
	uint32_t CursorLimitY;
	
	/* TTY Spinlock */
	spinlock_t lock;

} tty_t;
#pragma pack(pop)

#endif // !_X86_VIDEO_