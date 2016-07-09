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