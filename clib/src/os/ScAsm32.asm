;-----------------------------------------------------------------------------
; ScAsm32.asm - Syscall Routine for X32
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

				; Save
				push ebx
				push esi
				push edi

				; Get params
				mov eax, [ebp + 12 + 12]
				mov ebx, [ebp + 16 + 12]
				mov ecx, [ebp + 20 + 12]
				mov edx, [ebp + 24 + 12]
				mov esi, [ebp + 28 + 12]
				mov edi, [ebp + 32 + 12]

				; Syscall
				int	80h

				; Restore
				pop edi
				pop esi
				pop ebx

				; Release stack frame
				pop ebp
				ret 
					
__syscall       endp

_TEXT           ends
                end