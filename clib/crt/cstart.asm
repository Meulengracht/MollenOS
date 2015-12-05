;-----------------------------------------------------------------------------
; cstart.asm - Console Init CRT
; Only for usage in MollenOS Applications
; Copyright (c) Philip Meulengracht. All rights reserved. 
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public __mConCrt
				extern _main:near
         
__mConCrt       proc    near
                assume  cs:_TEXT

				; Step 1. Init Crt


				; Step 2. Parse Arguments


				; Step 3. Call main
				call	_main

				; Step 4. Terminate with exit code
				mov		ebx, eax
				mov		eax, 1h
				int		80h

				; Step 5. Yield
				mov		eax, 1h
				int		80h

				; Step 6. Catch Loop
Forever:
					nop
					jmp	Forever

__mConCrt       endp

_TEXT           ends
                end