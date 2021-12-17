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
; Vesa.inc
; - Contains VESA/VBE Routines
; 


%ifndef _MBOOT_VESA_INC_
%define _MBOOT_VESA_INC_

BITS 16

; Structure Definitions
struc VbeControllerStruct
    .Signature			resb 4
    .Version 			resw 1
    .OemNamePtr			resd 1
    .Capabilities		resd 1
    .ModeListPtr		resd 1 	; List of WORDS terminated by 0xFFFF
    .Num64KBlocksMem	resw 1
    .ReservedSpace 		resb 492
endstruc

struc VbeModeStruct
    .Attributes 		resw 1
    .WindowAttribsA 	resb 1
    .WindowAttribsB		resb 1
    .WindowGranKb 		resw 1
    .WindowSizeKb 		resw 1
    .WindowASegment 	resw 1
    .WindowBSegment 	resw 1
    .FuncPtrPosition 	resd 1
    .BytesPerScanline 	resw 1

    .ResolutionX		resw 1
    .ResolutionY 		resw 1
    .CharCellWidth 		resb 1
    .CharCellHeight 	resb 1
    .NumPlanes 		    resb 1
    .Bpp 			    resb 1
    .NumBanks 		    resb 1
    .MemoryModel		resb 1
    .BankSizeKb 		resb 1
    .MaxImagePages 		resb 1
    .Reserved 		    resb 1

    .RedMaskSize 		resb 1
    .RedFieldPos 		resb 1
    .GreenMaskSize 		resb 1
    .GreenFieldPos 		resb 1
    .BlueMaskSize 		resb 1
    .BlueFieldPos 		resb 1
    .AlphaMaskSize		resb 1
    .AlphaFieldPos 		resb 1
    .DirectColorAttribs resb 1

    .FrameBuffer        resd 1
    .Reserved1          resd 1
    .Reserved2          resw 1

    .LinBytesPerScanLine	resw 1
    .BnkNumberOfImagePages	resb 1
    .LinNumberOfImagePages	resb 1
    .LinRedMaskSize	        resb 1
    .LinRedFieldPos	        resb 1
    .LinGreenMaskSize       resb 1
    .LinGreenFieldPos       resb 1
    .LinBlueMaskSize        resb 1
    .LinBlueFieldPos        resb 1
    .LinAlphaMaskSize       resb 1
    .LinAlphaFieldPos       resb 1
    .MaxPixelClock          resd 1

    .ReservedSpace          resb 190
endstruc

; ********************************
; SetTextColor16
; @brief Sets a new text color for console mode. The text color is the high
; byte of the color parameter, and the background color is the low byte.
;
; @param color [In] The new text color.
; @return none
; ********************************
SetTextColor16:
    STACK_FRAME_BEGIN16

    ; Pack AL/AH into BL
    mov ax, ARG0_16
    mov dl, ah
    shl dl, 4
    and al, 0x0F
    or  dl, al
    mov byte [bTextColor], dl

    STACK_FRAME_END16
    ret

; ********************************
; PrintChar16
; @brief Prints a single character to the screen using INT10 if enabled by compiler support. The
; character is also multiplexed to serial port if enabled.
;
; @param character [In] The character to print
; @return none
; ********************************
PrintChar16:
    STACK_FRAME_BEGIN16
    push bx
    mov ax, ARG0_16
%ifdef __OSCONFIG_HAS_VIDEO
    mov ah, 0x0E
    mov bl, byte [bTextColor]
    mov bh, 0x00
    int 0x10
%endif

%ifdef __OSCONFIG_HAS_UART
    mov bl, al
    .WaitForTxEmpty:
        mov dx, UART_PORT_LINE_STATUS
        in  al, dx
        and al, 0x20
        cmp al, 0x20
        jne .WaitForTxEmpty
    mov al, bl
    mov dx, UART_PORT_DATA
    out dx, al
%endif
    pop bx
    STACK_FRAME_END16
    ret

; ********************************
; Print16
; @brief Prints out a zero terminated string to the screen using INT10 and
; also multiplexes output to serial output.
;
; @param string [In] The zero-terminated string to print
; @return            The number of characters printed
; ********************************
Print16:
    STACK_FRAME_BEGIN16
    push si
    mov si, ARG0_16
    mov dx, 0
    xor ax, ax   ; clear out ax beforehand
    .loop:
        lodsb      ; load next byte from string
        or	al, al ; check for zero terminator
        jz	.done

        push ax        ; push argument
        call PrintChar16
        add sp, 2      ; cleanup stack

        inc dx
        jmp	.loop

    .done:
        pop si
        mov ax, dx
        STACK_FRAME_END16
        ret

; ********************************
; PrintNumber16
; @brief Prints out a digit to the screen at current position.
; 
; @param number_low  [In] The low word of the number to print
; @param number_high [In] The high word of the number to print
; @return            None
; ********************************
PrintNumber16:
    STACK_FRAME_BEGIN16
    push bx

    ; load number into eax
    mov eax, dword ARG0_16

    xor bx, bx
    mov ecx, 10

    .convert:
        xor edx, edx
        div ecx

        ; now eax <-- eax/10
        ;     edx <-- eax % 10

        ; print edx
        ; this is one digit, which we have to convert to ASCII
        ; the print routine uses edx and eax, so let's push eax
        ; onto the stack. we clear edx at the beginning of the
        ; loop anyway, so we don't care if we much around with it

        ; convert dl to ascii
        add dx, 48
        push dx      ; store it for print
        inc  bx

        ; if eax is zero, we can quit
        cmp eax, 0
        jnz .convert

    .print:
        call PrintChar16
        pop ax

        dec bx
        jnz .print

    pop bx
    STACK_FRAME_END16
    ret

;**************************
; VideoInitializeConsole16
; @brief Initializes the video output to console/text mode
; @param <none>
; @return <none> 
VideoInitializeConsole16:
    ; Set video mode to 80x25 (Color Mode)
    mov al, 0x03
    mov ah, 0x00
    int 0x10

    ; Set text color to GREEN on BLACK
    mov al, BLACK
    mov ah, GREEN
    push ax
    call SetTextColor16
    add sp, 2
    ret

; **************************
; VesaGetVideoModes
; IN:
; 	- ES:DI: Buffer
; OUT:
; 	- EAX: 0 for success
; **************************
VesaGetVideoModes:
    ; Save state
    pushad

    ; Setup for INT
    xor 	eax, eax
    mov 	ax, 0x4F00
    int 	0x10

    ; Check if success
    test 	ah, ah
    jne 	.NoVesa

    cmp 	al, 0x4F
    jne 	.NoVesa

    ; Yay! Go back
    popad
    mov eax, 0
    ret

    ; Damniiiit
    .NoVesa:
        popad
        mov 	eax, 1
        ret

; **************************
; VesaFindBestSuitableMode
; IN:
;	- BX:    Desired Depth BPP
; 	- ES:SI: List of Modes
; **************************
VesaFindBestSuitableMode:
    ; Save state
    pushad

    ; Save ES
    push 	es
    xor	eax, eax
    mov	es, ax

    .cLoop:
        ; Save BPP
        push 	bx

        ; Null buffer
        push 	esi
        xor 	eax, eax
        mov 	edi, VesaModeInfo
        mov 	ecx, 64
        rep 	stosd
        pop 	esi

        ; Restore BX
        pop	bx

        ; Load mode
        pop	es
        mov 	cx, word [es:si]
        push	es
        xor	eax, eax
        mov	es, ax

        ; End of list? 
        cmp 	cx, 0xFFFF
        je 		.Done

        ; Setup INT 
        mov 	ax, 0x4F01
        mov 	edi, VesaModeInfo
        int 	0x10

        ; Did it work?
        test 	ah, ah
        jne 	.NextEntry

        cmp 	al, 0x4F
        jne 	.NextEntry

        ; Obviously, worked, now lets parse
        mov 	ax, word [VesaModeInfo + VbeModeStruct.Attributes]

        ; We want Color, Graphics, LFB
        and 	ax, 0x009B
        cmp 	ax, 0x009B
        jne 	.NextEntry

        ; We want 32 bit modes!!
        mov 	al, byte [VesaModeInfo + VbeModeStruct.Bpp]
        cmp 	al, bl
        jne 	.NextEntry

        ; Ok... The modes we are looking for are:
        ; 1920x1080
        ; 1600x900
        ; 1280x720
        ; 1024x768
        mov 	ax, word [VesaModeInfo + VbeModeStruct.ResolutionX]
        mov 	dx, word [VesaModeInfo + VbeModeStruct.ResolutionY]

        ; Check 1024x768
        cmp 	ax, 0x0400
        jne 	.Check1280
        cmp 	dx, 0x0300
        jne 	.Check1280
        jmp 	.ValidateEntry

        ; Ok, so this is 1024x768
        .Check1280:
            cmp 	ax, 0x0500
            jne 	.Check1600
            cmp 	dx, 0x02D0
            jne 	.Check1600
            jmp 	.ValidateEntry

        .Check1600:
            cmp 	ax, 0x0640
            jne 	.Check1920
            cmp 	dx, 0x0384
            jne 	.Check1920
            jmp 	.ValidateEntry

        .Check1920:
            cmp 	ax, 0x0780
            jne 	.NextEntry
            cmp 	dx, 0x0438
            jne 	.NextEntry

        .ValidateEntry:
            ; Check if we have better stored
            cmp 	ax, word [wCurrentResX]
            jbe 	.NextEntry
            cmp 	dx, word [wCurrentResY]
            jbe 	.NextEntry

            ; Save
            mov 	word [wCurrentResX], ax
            mov 	word [wCurrentResY], dx
            mov 	word [wOptimizedVesaMode], cx

        .NextEntry:
            ; Increase esi
            add 	si, 2
            jmp 	.cLoop

    .Done:
        ; Return
        pop es
        popad
        ret

; **************************
; VesaFinish
; **************************
VesaFinish:
    ; Save state
    pushad

    ; Make sure it is not 0 or 1
    cmp 	word [wOptimizedVesaMode], 1
    jbe		.Done

    mov 	ax, 0x4F02
    mov 	bx, word [wOptimizedVesaMode]
    or 		bx, 0x4000
    int 	0x10

    .Done:
        ; Restore & Return
        popad
        ret

; **************************
; VesaInitialize16
; IN:
; 	- ES:DI: Buffer
; **************************
VesaInitialize16:
    pushad

    push szVesaInitialize16
    call Print16
    add sp, 2

    ; Setup buffer
    xor 	eax, eax
    xor 	edi, edi
    mov 	es, ax
    mov 	edi, VesaControllerInfo

    ; Get controller info
    call 	VesaGetVideoModes
    test 	eax, eax
    jne 	.GetModeFail

    ; Now parse list
    xor 	eax, eax
    xor	esi, esi
    push	es
    mov 	ax, word [VesaControllerInfo + VbeControllerStruct.ModeListPtr + 2]
    mov	si, word [VesaControllerInfo + VbeControllerStruct.ModeListPtr]
    mov 	es, ax

    ; Try to locate 32 bit modes first
    mov	bx, 0x0020
    call 	VesaFindBestSuitableMode

    ; Yay?
    cmp 	word [wOptimizedVesaMode], 1
    ja 	    .ModeSuccess

    ; Try to locate 24 bit mode then
    mov	bx, 0x18
    call	VesaFindBestSuitableMode

    ; Sanity
    cmp 	word [wOptimizedVesaMode], 1
    jbe     .Fail

    .ModeSuccess:
        pop	es
        
        TRACE16 szVesaModeSpace

        xor eax, eax
        mov ax, word [wCurrentResX]
        push eax
        call PrintNumber16
        add sp, 4

        TRACE16 szVesaModeSeperator

        xor eax, eax
        mov ax, word [wCurrentResY]
        push eax
        call PrintNumber16
        add sp, 4
        
        TRACE16 szVesaModeEnd

        ; Reload structure
        xor ecx, ecx
        mov ax, 0x4F01
        mov cx, word [wOptimizedVesaMode]
        mov edi, VesaModeInfo
        int 0x10

        ; Fill vboot structure with information
        mov eax, dword [VesaModeInfo + VbeModeStruct.FrameBuffer]
        mov dword [BootHeader + VBoot.VideoFramebuffer], eax

        mov ax, word [VesaModeInfo + VbeModeStruct.ResolutionX]
        mov cx, word [VesaModeInfo + VbeModeStruct.ResolutionY]
        mov dx, word [VesaModeInfo + VbeModeStruct.BytesPerScanline]
        mov word [BootHeader + VBoot.VideoWidth], ax
        mov word [BootHeader + VBoot.VideoHeight], cx
        mov word [BootHeader + VBoot.VideoPitch], dx

        mov al, byte [VesaModeInfo + VbeModeStruct.Bpp]
        mov byte [BootHeader + VBoot.VideoBitsPerPixel], al

        mov al, byte [VesaModeInfo + VbeModeStruct.RedMaskSize]
        mov cl, byte [VesaModeInfo + VbeModeStruct.RedFieldPos]
        mov byte [BootHeader + VBoot.VideoRedMask], al
        mov byte [BootHeader + VBoot.VideoRedPosition], cl
        
        mov al, byte [VesaModeInfo + VbeModeStruct.GreenMaskSize]
        mov cl, byte [VesaModeInfo + VbeModeStruct.GreenFieldPos]
        mov byte [BootHeader + VBoot.VideoGreenMask], al
        mov byte [BootHeader + VBoot.VideoGreenPosition], cl

        mov al, byte [VesaModeInfo + VbeModeStruct.BlueMaskSize]
        mov cl, byte [VesaModeInfo + VbeModeStruct.BlueFieldPos]
        mov byte [BootHeader + VBoot.VideoBlueMask], al
        mov byte [BootHeader + VBoot.VideoBluePosition], cl

        mov al, byte [VesaModeInfo + VbeModeStruct.AlphaMaskSize]
        mov cl, byte [VesaModeInfo + VbeModeStruct.AlphaFieldPos]
        mov byte [BootHeader + VBoot.VideoAlphaMask], al
        mov byte [BootHeader + VBoot.VideoAlphaPosition], cl
        
        popad 
        ret

    .GetModeFail:
        pop	es
        TRACE16 szModeFailed

        popad 
        ret

    .Fail:
        pop	es
        TRACE16 szNoAvailModesFailed

        popad 
        ret


; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szVesaInitialize16   db "000000000 [vboot] selecting video mode", 0x00
szVesaModeSpace      db " [", 0x00
szVesaModeSeperator	 db "x", 0x00
szVesaModeEnd        db "]", 0x0D, 0x0A, 0x00
szModeFailed         db " [VBE NO SUPPORT]", 0x0D, 0x0A, 0x00
szNoAvailModesFailed db " [VBE NO 24/32B MODES]", 0x0D, 0x0A, 0x00

%ifdef __OSCONFIG_HAS_VIDEO
wOptimizedVesaMode dw 1
%else
wOptimizedVesaMode dw 0
%endif
wCurrentResX dw 0
wCurrentResY dw 0
bTextColor   db 0x0F

; ****************************
; VBE Structures
; ****************************
VesaControllerInfo:
istruc VbeControllerStruct
    at VbeControllerStruct.Signature,			db		0, 0, 0, 0
    at VbeControllerStruct.Version, 			dw		0
    at VbeControllerStruct.OemNamePtr,			dd 		0
    at VbeControllerStruct.Capabilities,		dd 		0
    at VbeControllerStruct.ModeListPtr,			dd 		0 	; List of WORDS terminated by 0xFFFF
    at VbeControllerStruct.Num64KBlocksMem,		dw 		0
    at VbeControllerStruct.ReservedSpace,		times 	492 db 0
iend

VesaModeInfo:
istruc VbeModeStruct
    at VbeModeStruct.Attributes, 		dw 	0
    at VbeModeStruct.WindowAttribsA, 	db 	0
    at VbeModeStruct.WindowAttribsB,	db 	0
    at VbeModeStruct.WindowGranKb, 		dw 	0
    at VbeModeStruct.WindowSizeKb, 		dw 	0
    at VbeModeStruct.WindowASegment, 	dw 	0
    at VbeModeStruct.WindowBSegment, 	dw 	0
    at VbeModeStruct.FuncPtrPosition, 	dd 	0

    at VbeModeStruct.BytesPerScanline, 	dw 	0
    at VbeModeStruct.ResolutionX,		dw 	0
    at VbeModeStruct.ResolutionY, 		dw 	0
    at VbeModeStruct.CharCellWidth, 	db 	0
    at VbeModeStruct.CharCellHeight, 	db 	0
    at VbeModeStruct.NumPlanes, 		db 	0
    at VbeModeStruct.Bpp, 			db 	0
    at VbeModeStruct.NumBanks, 		db 	0
    at VbeModeStruct.MemoryModel,		db 	0
    at VbeModeStruct.BankSizeKb, 		db 	0
    at VbeModeStruct.MaxImagePages, 	db 	0
    at VbeModeStruct.Reserved, 		db 	0

    at VbeModeStruct.RedMaskSize, 		db 	0
    at VbeModeStruct.RedFieldPos, 		db 	0
    at VbeModeStruct.GreenMaskSize, 	db 	0
    at VbeModeStruct.GreenFieldPos, 	db 	0
    at VbeModeStruct.BlueMaskSize, 		db 	0
    at VbeModeStruct.BlueFieldPos, 		db 	0
    at VbeModeStruct.AlphaMaskSize,		db 	0
    at VbeModeStruct.AlphaFieldPos, 	db 	0
    at VbeModeStruct.DirectColorAttribs,	db 	0

    at VbeModeStruct.FrameBuffer, 		dd 	0
    at VbeModeStruct.Reserved1, 		dd 	0
    at VbeModeStruct.Reserved2, 		dw 	0

    at VbeModeStruct.LinBytesPerScanLine,	dw	0
    at VbeModeStruct.BnkNumberOfImagePages,	db	0
    at VbeModeStruct.LinNumberOfImagePages,	db	0
    at VbeModeStruct.LinRedMaskSize,		db	0
    at VbeModeStruct.LinRedFieldPos,		db	0
    at VbeModeStruct.LinGreenMaskSize,	db	0
    at VbeModeStruct.LinGreenFieldPos,	db	0
    at VbeModeStruct.LinBlueMaskSize,	db	0
    at VbeModeStruct.LinBlueFieldPos,	db	0
    at VbeModeStruct.LinAlphaMaskSize,	db	0
    at VbeModeStruct.LinAlphaFieldPos,	db	0
    at VbeModeStruct.MaxPixelClock,		dd	0

    at VbeModeStruct.ReservedSpace, 	times 	190 db 0
iend

%endif