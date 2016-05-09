;-----------------------------------------------------------------------------
; ftol.S - convert float to long no sse
; Adapted from Visual Studio C runtime library
; Portions Copyright (c) Microsoft Corporation. All rights reserved. 
;-----------------------------------------------------------------------------
                .386
_TEXT           segment use32 para public 'CODE'
                public  __ftol


__ftol          proc    near
                assume  cs:_TEXT

				; Set up stack frame
				push ebp
				mov ebp, esp
    
				; Set "round towards zero" mode
				fstcw [ebp-2]
				wait
				mov ax, [ebp-2]
				or ah, 12
				mov [ebp-4], ax
				fldcw [ebp-4]
    
				; Do the conversion
				fistp qword ptr [ebp-12]
    
				; Restore rounding mode
				fldcw [ebp-2]
    
				; Return value */
				mov eax, [ebp-12]
				mov edx, [ebp-8]
    
				; Remove stack frame and return
				leave
				ret


__ftol          endp

_TEXT           ends
                end