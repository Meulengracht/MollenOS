;-----------------------------------------------------------------------------
; cstart.asm - Service Init CRT
; Only for usage in MollenOS Applications
; Copyright (c) Philip Meulengracht. All rights reserved. 
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public __mSrvCrt
				extern _main:near
         
__mSrvCrt       proc    near
                assume  cs:_TEXT

				; Step 1. Init Crt


				; Step 2. Parse Arguments


				; Step 3. Call main
				call	_main

				; Step 4. Cleanup CRT

				; Step 5. Terminate with exit code
				mov		ebx, eax
				mov		eax, 1h
				int		80h

				; Step 6. Yield
				mov		eax, 2h
				int		80h

				; Step 7. Catch Loop
Forever:
					nop
					jmp	Forever
					
__mSrvCrt       endp

_TEXT           ends
                end