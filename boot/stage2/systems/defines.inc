; MollenOS
;
; Copyright 2011, Philip Meulengracht
;
; This program is free software : you can redistribute it and / or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation ? , either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.If not, see <http://www.gnu.org/licenses/>.
;
; MollenOS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; Memory Map:
; 0x00000000 - 0x000004FF		Reserved
; 0x00000500 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x00008FFF		Used by subsystems in this bootloader
; 0x00009000 - 0x00009FFF		Memory Map
; 0x0000A000 - 0x0000AFFF		Vesa Mode Map / Controller Information
; 0x0000B000 - 0x0007FFFF		File Loading Bay (467 Kb ? ish)
; Rest above is not reliable

; Definitions
%define         MEMLOCATION_INITIALSTACK        0x7C00
%define 		MEMLOCATION_BOOTCODE			0x7C00
%define 		MEMLOCATION_FAT_GETCLUSTER		0x7E00
%define         MEMLOCATION_TRAMPOLINE_CODE     0x8000
%define 		MEMLOCATION_FAT_FATTABLE		0x8000
%define 		MEMLOCATION_MEMORY_MAP			0x9000
%define 		MEMLOCATION_VESA_INFO_BASE		0xA000
%define 		MEMLOCATION_FLOAD_SEGMENT		0x0000
%define 		MEMLOCATION_FLOAD_OFFSET		0xB000
%define 		MEMLOCATION_FLOAD_LOWER			0xB000 
%define 		MEMLOCATION_KERNEL_UPPER		0x100000
%define         MEMLOCATION_RAMDISK_UPPER		0x200000
%define         MEMLOCATION_KERNEL_STACK        0x9F000

; Unpack area will be not used afterwards and will then be used for 
; initial page directory in 64 bit. We will need 0x5000 space
%define			MEMLOCATION_UNPACK_AREA			0x300000
%define         MEMLOCATION_PML4T               0x300000
%define         MEMLOCATION_PDPT                0x301000
%define         MEMLOCATION_PDT                 0x302000
%define         MEMLOCATION_PT                  0x303000
%define         MEMLOCATION_PT2                 0x304000

