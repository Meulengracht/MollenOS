; MollenOS
; Copyright 2011-2016, Philip Meulengracht
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation?, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
;
; MollenOS x86 framebuffer present methods

bits 32
segment .text

;Functions in this asm
global _present_basic
global _present_sse
global _present_sse2


; present_basic(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes)
; Copies data <Rows> times from _Backbuffer to Framebuffer
_present_basic:
	; Stack Frame
	push	ebp
	mov		ebp, esp

    ; Store state
    push    edi
    push    esi
    push    ebx
    push    ecx
    push    edx

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop counters
	mov		ebx, dword [ebp + 16]
	mov		ecx, dword [ebp + 20]
	mov		eax, dword [ebp + 24]
	mov		edx, dword [ebp + 28]

    ; Since we do byte copies, just add eax to ecx
    add     ecx, eax

    ; Iterate for ebx times
    .NextRow:
        push    ecx
        push    edi
        rep     movsb
        pop     edi
        pop     ecx
        sub     edi, edx

        ; Loop Epilogue
        dec ebx
        jnz .NextRow
    
    ; Store state
    pop     edx
    pop     ecx
    pop     ebx
    pop     esi
    pop     edi
	pop     ebp
	ret


; present_sse(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes)
; Copies data <Rows> times from _Backbuffer to Framebuffer
_present_sse:
	; Stack Frame
	push	ebp
	mov		ebp, esp

    ; Store state
    push    edi
    push    esi
    push    ebx
    push    ecx
    push    edx

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop counters
	mov		ebx, dword [ebp + 16]
	mov		ecx, dword [ebp + 20]
	mov		eax, dword [ebp + 24]
	mov		edx, dword [ebp + 28]

    ; Iterate for ebx times
    .NextRow:

        ; Iterate for ecx times
        push    ecx
        push    edi
        .NextCopy:
            movdqa xmm0, [esi]
            movdqa xmm1, [esi + 16]
            movdqa xmm2, [esi + 32]
            movdqa xmm3, [esi + 48]
            movdqa xmm4, [esi + 64]
            movdqa xmm5, [esi + 80]
            movdqa xmm6, [esi + 96]
            movdqa xmm7, [esi + 112]

            movntdq [edi], xmm0
            movntdq [edi + 16], xmm1
            movntdq [edi + 32], xmm2
            movntdq [edi + 48], xmm3
            movntdq [edi + 64], xmm4
            movntdq [edi + 80], xmm5
            movntdq [edi + 96], xmm6
            movntdq [edi + 112], xmm7
            add		esi, 128
            add		edi, 128
            dec		ecx
            jnz     .NextCopy
        
        ; Copy remainder bytes
        mov     ecx, eax
        rep     movsb
        pop     edi
        pop     ecx
        sub     edi, edx

        ; Loop Epilogue
        dec     ebx
        jnz     .NextRow
    
    ; Store state
    pop     edx
    pop     ecx
    pop     ebx
    pop     esi
    pop     edi
	pop     ebp
	emms
	ret

; present_sse2(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes)
; Copies data <Rows> times from _Backbuffer to Framebuffer
_present_sse2:
	; Stack Frame
	push	ebp
	mov		ebp, esp

    ; Store state
    push    edi
    push    esi
    push    ebx
    push    ecx
    push    edx

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop counters
	mov		ebx, dword [ebp + 16]
	mov		ecx, dword [ebp + 20]
	mov		eax, dword [ebp + 24]
	mov		edx, dword [ebp + 28]

    ; Iterate for ebx (Rows) times
    .NextRow:

        ; Iterate for ecx (RowLoops) times
        push    ecx
        push    edi
        .NextCopy:
            prefetchnta [esi + 128]
            prefetchnta [esi + 160]
            prefetchnta [esi + 192]
            prefetchnta [esi + 224]

            movdqa xmm0, [esi]
            movdqa xmm1, [esi + 16]
            movdqa xmm2, [esi + 32]
            movdqa xmm3, [esi + 48]
            movdqa xmm4, [esi + 64]
            movdqa xmm5, [esi + 80]
            movdqa xmm6, [esi + 96]
            movdqa xmm7, [esi + 112]

            movntdq [edi], xmm0
            movntdq [edi + 16], xmm1
            movntdq [edi + 32], xmm2
            movntdq [edi + 48], xmm3
            movntdq [edi + 64], xmm4
            movntdq [edi + 80], xmm5
            movntdq [edi + 96], xmm6
            movntdq [edi + 112], xmm7
            add		esi, 128
            add		edi, 128
            dec		ecx
            jnz     .NextCopy
        
        ; Copy remainder bytes
        mov     ecx, eax
        rep     movsb
        pop     edi
        pop     ecx
        add     edi, edx

        ; Loop Epilogue
        dec     ebx
        jnz     .NextRow
    
    ; Store state
    pop     edx
    pop     ecx
    pop     ebx
    pop     esi
    pop     edi
	pop     ebp
	emms
	ret
