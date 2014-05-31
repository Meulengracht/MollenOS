;-----------------------------------------------------------------------------
; ftol2.S - convert float to long sse & sse2
; Adapted from Visual Studio C runtime library
; Portions Copyright (c) Microsoft Corporation. All rights reserved. 
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  __ftol2
				public  __ftol2_sse
				extrn	__ftol:PROC


__ftol2         proc    near
                assume  cs:_TEXT

				; Jump to ftol
				jmp __ftol


__ftol2         endp

__ftol2_sse     proc    near
                assume  cs:_TEXT

				; Jump to ftol
				jmp __ftol


__ftol2_sse     endp


_TEXT           ends
                end