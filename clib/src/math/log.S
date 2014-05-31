;-----------------------------------------------------------------------------
; log.S - takes the logorithmic value in base 2
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  _log


_log			proc    near
				assume  cs:_TEXT

				push    ebp
				mov     ebp,esp
				fld     qword ptr [ebp+8]       ; Load real from stack
				fldln2                          ; Load log base e of 2
				fxch    st(1)                   ; Exchange st, st(1)
				fyl2x                           ; Compute the natural log(x)
				pop     ebp
				ret

_log			endp

_TEXT           ends
                end