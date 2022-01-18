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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; A20.inc
; - Contains routines for enabling A20 Gate
; 

%ifndef _MBOOT_A20_INC_
%define _MBOOT_A20_INC_

BITS 16

; ********************************
; A20IsEnabled
; @brief Checks if A20 is enabled
; @param <none>
; @returns 1 if A20 is enabled, 0 otherwise
; ******************************** 
A20IsEnabled:
    pushf
    push ds
    push es
    push di
    push si
    cli
 
    xor ax, ax ; ax = 0
    mov es, ax
 
    not ax ; ax = 0xFFFF
    mov ds, ax
 
    mov di, 0x0500
    mov si, 0x0510
 
    mov al, byte [es:di]
    push ax
 
    mov al, byte [ds:si]
    push ax
 
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
 
    cmp byte [es:di], 0xFF
 
    pop ax
    mov byte [ds:si], al
 
    pop ax
    mov byte [es:di], al
 
    mov ax, 0
    je 	.exit
 
    mov ax, 1
 
    .exit:
        pop si
        pop di
        pop es
        pop ds
        popf
        ret

; ********************************
; A20MethodKeyboardController
; ******************************** 
A20MethodKeyboardController:
    cli

    call    A20Wait
    mov     al, 0xAD
    out     0x64, al

    call    A20Wait
    mov     al, 0xD0
    out     0x64, al

    call    A20Wait2
    in      al, 0x60
    push    ax

    call    A20Wait
    mov     al, 0xD1
    out     0x64, al

    call    A20Wait
    pop     ax
    or      al, 2
    out     0x60, al

    call    A20Wait
    mov     al, 0xAE
    out     0x64, al

    call    A20Wait
    sti
    ret

A20Wait:
    in      al, 0x64
    test    al, 2
    jnz     A20Wait
    ret

A20Wait2:
    in      al, 0x64
    test    al, 1
    jz      A20Wait2
    ret


; ********************************
; A20MethodBios
; ******************************** 
A20MethodBios:
	mov ax, 0x2401
	int 0x15
	ret

; ********************************
; A20Enable16
; @brief Enables the A20 Gate through various methods
;
; @param <none>
; @return <none>
; ******************************** 
A20Enable16:
	call A20IsEnabled      ; don't enable A20 if it's already enabled
	cmp ax, 1
	je 	.success

	call A20MethodBios     ; try enabling through bios as primary option
	call A20IsEnabled      ; verify if it's enabled
	cmp ax, 1
	je 	.success

	call A20MethodKeyboardController ; try enabling through keyboard controller
	call A20IsEnabled     ; verify if it's enabled
	cmp ax, 1
	je 	.success

	.failed:
        xor ax, ax
	.success:
		ret

%endif
