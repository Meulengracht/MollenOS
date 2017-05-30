;;
;; aPLib compression library  -  the smaller the better :)
;;
;; nasm assembler depacker
;;
;; Copyright (c) 1998-2014 Joergen Ibsen
;; All Rights Reserved
;;
;; http://www.ibsensoftware.com/
;;
BITS 32

; lz_decompress(const void *source, void *destination)
; ********************************
; lz_decompress
; IN:
; 	- ESI: Source Data Package Address
; 	- EDI: Destination Data Address
; OUT:
;       - EAX: Unpacked size
; ********************************
lz_decompress:
    pushad
    mov    dword [dLZDestination], edi
    cld
    mov    dl, 80h
    xor    ebx,ebx

literal:
    movsb
    mov    bl, 2
nexttag:
    call   getbit
    jnc    literal

    xor    ecx, ecx
    call   getbit
    jnc    codepair
    xor    eax, eax
    call   getbit
    jnc    shortmatch
    mov    bl, 2
    inc    ecx
    mov    al, 10h
  .getmorebits:
    call   getbit
    adc    al, al
    jnc    .getmorebits
    jnz    domatch
    stosb
    jmp    nexttag
codepair:
    call   getgamma_no_ecx
    sub    ecx, ebx
    jnz    normalcodepair
    call   getgamma
    jmp    domatch_lastpos

shortmatch:
    lodsb
    shr    eax, 1
    jz     donedepacking
    adc    ecx, ecx
    jmp    domatch_with_2inc

normalcodepair:
    xchg   eax, ecx
    dec    eax
    shl    eax, 8
    lodsb
    call   getgamma

    cmp    eax, 32000
    jae    domatch_with_2inc
    cmp    ah, 5
    jae    domatch_with_inc
    cmp    eax, 7fh
    ja     domatch_new_lastpos

domatch_with_2inc:
    inc    ecx

domatch_with_inc:
    inc    ecx

domatch_new_lastpos:
    xchg   eax, ebp
domatch_lastpos:
    mov    eax, ebp

    mov    bl, 1

domatch:
    push   esi
    mov    esi, edi
    sub    esi, eax
    rep    movsb
    pop    esi
    jmp    nexttag

getbit:
    add    dl, dl
    jnz    .stillbitsleft
    mov    dl, [esi]
    inc    esi
    adc    dl, dl
  .stillbitsleft:
    ret

getgamma:
    xor    ecx, ecx
getgamma_no_ecx:
    inc    ecx
  .getgammaloop:
    call   getbit
    adc    ecx, ecx
    call   getbit
    jc     .getgammaloop
    ret

donedepacking:
    sub    edi, dword [dLZDestination]
    mov    dword [dLZOriginalSize], edi

    ; Return unpacked length in eax
    popad
    mov eax, dword [dLZOriginalSize] 
    ret

; ****************************
; Variables
; ****************************
dLZOriginalSize					dd 		0
dLZDestination					dd 		0
