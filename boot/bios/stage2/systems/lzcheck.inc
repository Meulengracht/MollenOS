;;
;; aPLib compression library  -  the smaller the better :)
;;
;; fasm safe assembler crc checker
;;
;; Copyright (c) 1998-2014 Joergen Ibsen
;; All Rights Reserved
;;
;; http://www.ibsensoftware.com/
;;
BITS 32

%include "systems/lzcrc32.inc"

; lz_validate(const void *source)
; ********************************
; lz_validate
; @param source [In] Pointer to the lz packed data
; OUT:
;   - EAX: -1 if error, length of unpacked
;   - ECX: Source Data Packet Address
; ********************************
lz_validate:
    STACK_FRAME_BEGIN32
    push ebx
    push esi

    mov esi, ARG0_32
    test esi, esi
    jz .return_error

    mov ebx, [esi]         ; ebx = header.tag

    cmp ebx, 032335041h    ; check tag == 'AP32'
    jne .return_error

    mov ebx, [esi + 4]     ; ebx = header.header_size
    cmp ebx, 24            ; check header_size >= 24
    jb .return_error

    add ebx, esi                ; ebx -> packed data
    mov ecx, dword [esi + 8]    ; push header.packed_size
    
    push ebx
    push esi
    mov esi, ebx
    call lz_crc32
    pop esi

    cmp eax, [esi + 12]    ; check eax == header.packed_crc
    mov eax, [esi + 16]    ; eax = header.orig_size
    pop esi
    je .return_eax

    .return_error:
        or eax, -1
        xor ecx, ecx

   ; return unpacked length in eax
    .return_eax:
        mov ecx, esi
        pop esi
        pop ebx
        STACK_FRAME_END32
        ret
