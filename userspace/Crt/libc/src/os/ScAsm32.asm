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
				push ecx
				push edx
				push esi
				push edi

				; Get params
				mov eax, [ebp + 8]
				mov ebx, [ebp + 12]
				mov ecx, [ebp + 16]
				mov edx, [ebp + 20]
				mov esi, [ebp + 24]
				mov edi, [ebp + 28]

				; Syscall
				int	80h

				; Restore
				pop edi
				pop esi
				pop edx
				pop ecx
				pop ebx

				; Release stack frame
				pop ebp
				ret 
					
__syscall       endp

_TEXT           ends
                end