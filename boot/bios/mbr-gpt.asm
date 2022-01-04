; MollenOSmbr
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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; Stage 1 Bootloader - GPT Protective MBR
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
    ;xchg bx, bx
    ;jmp 0x0:0x62A  ; offset to lower_entry (with xchg)

lower_entry:
    ; Load GPT entries, ignore the header.
    ; Entries are located from LBA 2
    xor ax, ax
	mov es, ax
	mov	bx, 0x7C00
	mov eax, 2
	mov ecx, 1
	mov edx, 512
    call read_sector

    ; ASSUMPTIONS:
    ; entries located on LBA2
    ; size of entry is 128 bytes
    ; these are completely fair to make as long as image is built with osbuilder
    mov bx, 0x7C00
    mov cx, 4
    .parse_gpt:
        ; detect end of table
        push cx
        mov si, bx
        call get_empty_guid
        mov cx, 16
        repe cmpsb
        pop cx
        je .parse_error

        ; check bit 2 for legacy bios bootable
        test byte [bx + 48], 0x04
        jz .next_entry

        ; fill in entry at empty entry, we store start sector
        call get_empty_guid
        mov eax, dword [bx + 32]
        mov dword [di + 8], eax
        jmp load_vbr

    .parse_error:
		; Give control to next OS, we failed
        mov si, [szInvalidTable]
        jmp failure

    .next_entry:
        add bx, 128
        dec cx
        jnz .parse_gpt
        jmp .parse_error

; BX points to the GPT entry
; ASSUMPTIONS:
; FirstLBA must be inside 32 bits so the first
; sector of the bootable partition MUST be in the first 2TB
load_vbr:
    xor ax, ax
	mov es, ax
    mov eax, dword [bx + 32]
	mov	bx, 0x7C00
    mov ecx, 1
	mov edx, 512
    call read_sector

    ; sanitize boot signature
    mov di, 0x7DFE
    cmp word [di], 0xAA55
    jnz .invalid_signature

    ; initialize parameters for vbr
    call get_drivenum
    call get_empty_guid
    mov si, di
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

get_empty_guid:
    mov di, empty_entry
    sub di, 0x7C00
    add di, 0x0600
    ret

; address in si
fixup_address:
    sub si, 0x7C00
    add si, 0x0600
    ret

; **************************
; BIOS ReadSector
; IN:
; 	- ES:BX: Buffer
;	- EAX: Sector start
; 	- ECX: Sector count
; 	- EDX: Sector size in bytes
;
; Registers:
; 	- Trashes ES, BX and EAX
; **************************
read_sector:
	; store values into disk package
	mov si, disk_package
	call fixup_address

	mov word [si + 6], es
	mov word [si + 4], bx
	mov dword [si + 8], eax

	.loop:
		mov word [si + 2], 1
		push edx
		mov ax, 0x4200
		call get_drivenum
		int 0x13

		; It's important we check for offset overflow
		pop edx
		mov ax, word [si + 4]
		add ax, dx
		mov word [si + 4], ax
		test ax, ax
		jne .no_overflow

	.overflow:
		; So overflow happened
		add word [si + 6], 0x1000
		mov word [si + 4], 0x0000

	.no_overflow:
		inc dword [si + 8]
		loop .loop
    ret

szInvalidTable db "Invalid partition table!", 0x0D, 0x0A, 0x00
szErrorLoading db "Error loading system!", 0x0D, 0x0A, 0x00
szNoBootSig db "Missing boot signature!", 0x0D, 0x0A, 0x00
bPhysicalDriveNum db 0

; This is used for the extended read function (int 0x13)
disk_package: db 0x10
			  db 0
	.count	  dw 0
	.offset	  dw 0
	.segment  dw 0
	.sector   dq 0

; Fill out bootloader
times 446-($-$$) db 0

; Setup a partation table that has a single entry marked EE
db 0, 0, 1, 0, 0xEE, 0xFE, 0xFF, 0xFF, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF
empty_entry:
db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
db 0x55, 0xAA ; boot signature
