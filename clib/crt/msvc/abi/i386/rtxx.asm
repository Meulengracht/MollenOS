; MollenOS
;
; Copyright 2011-2014, Philip Meulengracht
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation?, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
;
; MollenOS Visual C++ Implementation
;
bits 32
segment .text

;Externs
extern _CxxHandleV8Frame@20
extern _CxxHandleFrame
extern _CxxThrowException

;Functions in this asm
global ___CxxFrameHandler3
global _CallCxxFrameHandler
global ___CxxFrameHandler
global __CxxThrowException@8
global _RtlpCaptureContext
global _RtlCaptureContext

; Throw Exception redirection
__CxxThrowException@8:
	jmp _CxxThrowException

; Frame Handler callback for VC++ V8
___CxxFrameHandler3:
    push eax
    push dword [esp + 20]
    push dword [esp + 20]
    push dword [esp + 20]
    push dword [esp + 20]
    call _CxxHandleV8Frame@20
    ret

; Yet another redirection
___CxxFrameHandler:
	push 0
    push 0
    push eax
    push dword [esp + 28]
    push dword [esp + 28]
    push dword [esp + 28]
    push dword [esp + 28]
    call _CxxHandleFrame
    add esp, 28
    ret

; Redirection function
_CallCxxFrameHandler:
    mov eax, dword [esp + 20]
    jmp ___CxxFrameHandler

; Capture Context
_RtlCaptureContext:

    ; Preserve EBX and put the context in it
    push ebx
    mov ebx, [esp+8]

    ; Save the basic register context
    mov [ebx+0xB0], eax
    mov [ebx+0xAC], ecx
    mov [ebx+0xA8], edx
    mov eax, [esp]
    mov [ebx+0xA4], eax
    mov [ebx+0xA0], esi
    mov [ebx+0x9C], edi

    ; Capture the other regs
    jmp CaptureRest


; Capture Context
_RtlpCaptureContext:

    ; Preserve EBX and put the context in it
    push ebx
    mov ebx, [esp+8]

    ; Clear the basic register context
    mov dword [ebx+0xB0], 0
    mov dword [ebx+0xAC], 0
    mov dword [ebx+0xA8], 0
    mov dword [ebx+0xA4], 0
    mov dword [ebx+0xA0], 0
    mov dword [ebx+0x9C], 0

CaptureRest:
    ; Capture the segment registers
    mov [ebx+0xBC], cs
    mov [ebx+0x98], ds
    mov [ebx+0x94], es
    mov [ebx+0x90], fs
    mov [ebx+0x8C], gs
    mov [ebx+0xC8], ss

    ; Capture flags
    pushfd
    pop dword [ebx+0xC0]

    ; The return address should be in [ebp+4]
    mov eax, [ebp+4]
    mov [ebx+0xB8], eax

    ; Get EBP
    mov eax, [ebp+0]
    mov [ebx+0xB4], eax

    ; And get ESP
    lea eax, [ebp+8]
    mov [ebx+0xC4], eax

    ; Return to the caller
    pop ebx
    ret 4