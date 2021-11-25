; MollenOS
;
; Copyright 2011 - 2016, Philip Meulengracht
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
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; lz.inc
; - Contains routines for validating and unpacking lz-archives
; 
BITS 32

%include "systems/lzcheck.inc"
%include "systems/lzunpack.inc"

; LZLoad(const void *Source, void *Destination)
; ********************************
; LZLoad
; IN:
; 	- ESI: Source Base Address
; 	- EDI: Destination Base Address
; OUT:
;	- EAX: Unpacked size or -1 if invalid
; ********************************
LZLoad:
	; Step 1 - Validate the LZ package
	call lz_validate
	cmp eax, -1
	je .Skip

	; Step 2 - Unpack
	call lz_decompress

	; Done
	.Skip:
		ret