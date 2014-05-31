;-----------------------------------------------------------------------------
; fabs.S - absolute float
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  _floor


_floor			proc    near
				assume  cs:_TEXT

				push    ebp
				mov     ebp,esp
				sub     esp,4                   ; Allocate temporary space
				fld     qword ptr [ebp+8]       ; Load real from stack
				fstcw   [ebp-2]                 ; Save control word
				fclex                           ; Clear exceptions
				mov     word ptr [ebp-4], 0763h ; Rounding control word
				fldcw   [ebp-4]                 ; Set new rounding control
				frndint                         ; Round to integer
				fclex                           ; Clear exceptions
				fldcw   [ebp-2]                 ; Restore control word
				mov     esp,ebp
				pop     ebp
				ret


_floor			endp

_TEXT           ends
                end