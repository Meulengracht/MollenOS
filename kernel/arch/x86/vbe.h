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
 * MollenOS X86-32 VBE Information Header
 * - Contains definitions related to VBE/VESA/Video for X86
 */

#ifndef _X86_VBE_H_
#define _X86_VBE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* Includes 
 * - System */
#include <multiboot.h>

/* Definitions 
 * This is primarily fixed memory addresses that
 * we will need to fall-back to in case no video */
#define STD_VIDEO_MEMORY		0xB8000

/* This is the VBE Graphic Information
 * Descriptor which we have setup in
 * the bootloader */
PACKED_TYPESTRUCT(VbeMode, {
	uint16_t        ModeAttributes;
	uint8_t         WinAAttributes;
	uint8_t         WinBAttributes;
	uint16_t        WinGranularity;
	uint16_t        WinSize;
	uint16_t        WinASegment;
	uint16_t        WinBSegment;
	uint32_t        WinFuncPtr;
	uint16_t        BytesPerScanLine;
	uint16_t        XResolution;
	uint16_t        YResolution;
	uint8_t         XCharSize;
	uint8_t         YCharSize;
	uint8_t         NumberOfPlanes;
	uint8_t         BitsPerPixel;
	uint8_t         NumberOfBanks;
	uint8_t         MemoryModel;
	uint8_t         BankSize;
	uint8_t         NumberOfImagePages;
	uint8_t         Reserved_page;
	uint8_t         RedMaskSize;
	uint8_t         RedMaskPos;
	uint8_t         GreenMaskSize;
	uint8_t         GreenMaskPos;
	uint8_t         BlueMaskSize;
	uint8_t         BlueMaskPos;
	uint8_t         ReservedMaskSize;
	uint8_t         ReservedMaskPos;
	uint8_t         DirectColorModeInfo;

	/* VBE 2.0 Extensions */
	uint32_t        PhysBasePtr;
	uint32_t        OffScreenMemOffset;
	uint16_t        OffScreenMemSize;
});

/* VbeInitialize
 * Initializes the X86 video sub-system and provides
 * boot-video interface for the entire OS */
__EXTERN
void 
VbeInitialize(
	_In_ Multiboot_t *BootInfo);

#endif // !_X86_VBE_H_
