;-----------------------------------------------------------------------------
; cstart.asm - Service Init CRT
; Only for usage in MollenOS Applications
; Copyright (c) Philip Meulengracht. All rights reserved. 
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public __syscall
         
__syscall       proc    near
                assume  cs:_TEXT

				; Stack Frame
				push ebp
				mov ebp, esp

				; Get params
				mov eax, [ebp + 12]
				mov ebx, [ebp + 16]
				mov ecx, [ebp + 20]
				mov edx, [ebp + 24]
				mov esi, [ebp + 28]
				mov edi, [ebp + 32]

				; Release stack frame
				pop ebp
				ret 
					
__syscall       endp

_TEXT           ends
                end