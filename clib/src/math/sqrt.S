;-----------------------------------------------------------------------------
; fabs.S - absolute float
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  _fabs


_fabs			proc    near
				assume  cs:_TEXT

				push    ebp
				mov     ebp,esp
				fld     qword ptr [ebp+8]       ; Load real from stack
				fabs                            ; Take the absolute value
				pop     ebp
				ret


_fabs           endp

_TEXT           ends
                end