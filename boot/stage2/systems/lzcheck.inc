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

; lz_validate(const void *source, void *destination)
; ********************************
; lz_validate
; IN:
; 	- ESI: Source Data Package Address
; OUT:
;       - EAX: -1 if error, length of unpacked
; ********************************
lz_validate:
    push   ebx
    push   ecx
    push   esi

    test   esi, esi
    jz     .return_error

    mov    ebx, [esi]         ; ebx = header.tag

    cmp    ebx, 032335041h    ; check tag == 'AP32'
    jne    .return_error

    mov    ebx, [esi + 4]     ; ebx = header.header_size
    cmp    ebx, 24            ; check header_size >= 24
    jb     .return_error

    add    ebx, esi           ; ebx -> packed data
    push   esi
    mov    esi, ebx

    mov    ecx, dword [esi + 8]    ; push header.packed_size
    call   lz_crc32
    pop    esi

    cmp    eax, [esi + 12]    ; check eax == header.packed_crc

    mov    eax, [esi + 16]    ; eax = header.orig_size

    je     .return_eax

  .return_error:
    or     eax, -1            ; eax = -1

   ; return unpacked length in eax
  .return_eax:
    pop   esi
    pop   ecx
    pop   ebx
    ret
