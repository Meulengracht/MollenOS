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
; 0x00001000 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x00008FFF		Used by subsystems in this bootloader (BOOT ONLY)
; 0x00009000 - 0x00009FFF		Memory Map (PERSIST)
; 0x0000A000 - 0x0007FFFF		File Loading Bay (467 Kb ? ish)
; Rest above is not reliable

%define NULL_DESC   0
%define CODE_DESC   0x8
%define DATA_DESC   0x10
%define	CODE16_DESC 0x18
%define DATA16_DESC 0x20
%define	CODE64_DESC 0x28
%define DATA64_DESC 0x30

%define KILOBYTE 0x400
%define PAGESIZE 0x1000
%define MEGABYTE 0x100000

%define MEMLOCATION_INITIALSTACK        0x7C00
%define MEMLOCATION_BOOTCODE			0x7C00
%define MEMLOCATION_TRAMPOLINE_CODE     0x8000
%define MEMLOCATION_MEMORY_MAP			0x9000

%define FILELOADING_BAY_LINEAR  0xA000
%define FILELOADING_BAY_SEGMENT 0x0000
%define FILELOADING_BAY_OFFSET  0xA000
%define FILELOADING_BAY_SIZE    0x70000

%define KERNEL_STACK_SIZE   0x10000
%define KERNEL_BASE_ADDRESS 0x100000
