;-----------------------------------------------------------------------------
; sqrt.S - takes the square root
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  _sqrt


_sqrt          proc    near
                assume  cs:_TEXT

				push    ebp
				mov     ebp,esp
				fld     qword ptr [ebp+8]       ; Load real from stack
				fsqrt                           ; Take the square root
				pop     ebp
				ret


_sqrt          endp

_TEXT           ends
                end