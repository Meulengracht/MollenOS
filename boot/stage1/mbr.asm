; MollenOS
;
; Copyright 2021, Philip Meulengracht
;
; This program is free software : you can redistribute it and / or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation ? , either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.If not, see <http://www.gnu.org/licenses/>.
;
; Stage 1 Bootloader - MBR
; Version 1.0
;

; 16 Bit Code, Origin at 0x0
BITS 16
ORG 0x7C00

entry:
	cli
	jmp 0x0:fix_cs ; don't trust the code segment register

fix_cs:
	xor ax, ax
	mov ss, ax
	mov	ds, ax
	mov	es, ax

    ; setup stack to point just below this boot-code
	mov	ax, 0x7C00
	mov	sp, ax
    sti
    cld

    ; save the drive number
    mov byte [bPhysicalDriveNum], dl

    ; move the rest of the MBR to 0x0600 as we will override this mbr
    ; code with the sector of the bootloader from the partition table
    mov si, sp
    mov di, 0x0600
    mov cx, 0x0100 ; 256 words = 512 bytes
    repnz movsw
    jmp 0x0:0x628  ; offset to lower_entry
    ;jmp 0x0:0x62A  ; offset to lower_entry (with xchg)

lower_entry:
    mov si, 0x07BE ; partition table at offset BE
    mov bl, 4      ; partition table has 4 entries

    ; Partition Entry:
    ; 0: Status Byte (0x80 for active)
    ; 1-3: CHS Start
    ; 4: Type
    ; 5-7: CHS End
    ; 8: LBA Start
    ; 12: LBA Size
ptable_loop:
    cmp byte [si], 0x80
    jz .found
    cmp byte [si], 0x00
    jnz .invalid ; The value must be either 0 or 0x80
    add si, 0x10
    dec bl
    jnz ptable_loop
    int 0x18

    .found:
    mov dx, word [si]     ; store CHS information for INT 13
    mov cx, word [si + 2]
    mov bp, si            ; store this for later when we jump to VBR

    ; now we've found one, make sure there is only one
    .validate_loop:
    add si, 0x10
    dec bl
    jz read_mbr
    cmp byte [si], 0x00
    jz .validate_loop

    .invalid:
    mov si, [szInvalidTable]
    jmp failure

read_mbr:
    mov di, 5

    .read_loop:
    mov bx, 0x7C00
    mov ax, 0x0201
    
    push di
    int 0x13
    pop di
    jnb load_vbr
    
    ; read failed, reset disk and try again
    xor ax, ax
    int 0x13
    dec di
    jnz .read_loop
    mov si, [szErrorLoading]
    jmp failure

load_vbr:
    mov di, 0x7DFE
    cmp word [di], 0xAA55
    jnz .invalid_signature
    call get_drivenum
    mov si, bp
    jmp 0x0:0x7C00

    .invalid_signature:
    mov si, [szNoBootSig]

failure:
    call fixup_address
    .display:
    lodsb
    cmp al, 0x00
    jz .halt
    push si
    mov bx, 0x0007
    mov ah, 0x0E
    int 0x10
    pop si
    jmp .display

    .halt:
    jmp .halt

get_drivenum:
    mov bx, bPhysicalDriveNum
    sub bx, 0x7C00
    add bx, 0x0600
    mov dl, byte [bx]
    ret

; address in si
fixup_address:
    sub si, 0x7C00
    add si, 0x0600
    ret

szInvalidTable db "Invalid partition table!", 0x0D, 0x0A, 0x00
szErrorLoading db "Error loading system!", 0x0D, 0x0A, 0x00
szNoBootSig db "Missing boot signature!", 0x0D, 0x0A, 0x00
bPhysicalDriveNum db 0

; Fill out bootloader
times 510-($-$$) db 0
db 0x55, 0xAA ; boot signature
