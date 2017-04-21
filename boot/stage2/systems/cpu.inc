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
; Cpu.inc
; - Contains cpu initialization code
; 

%ifndef _MBOOT_CPU_INC_
%define _MBOOT_CPU_INC_

BITS 32

; **************************
; CpuInit 
; Out:
;   - EAX: 1 if 64 bit is ok, 0 if stay in 32 bit
;
; Registers:
; 	- Conserves all
; **************************
CpuInit:
	; Save registers
	pushad

	

	; Done
	popad
	mov 	eax, 0
	ret




%endif