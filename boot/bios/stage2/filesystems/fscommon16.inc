; MollenOS
;
; Copyright 2011 - 2016, Philip Meulengracht
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
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; FsCommon.inc
; - Contains shared filesystem routines
; 

%ifndef _MBOOT_FILESYSTEM_COMMON16_INC_
%define _MBOOT_FILESYSTEM_COMMON16_INC_

BITS 16

%include "filesystems/fat16.inc"
%include "filesystems/mfs16.inc"

; **************************
; FsCommonSetGeometry16
; @brief Sets the geometry of the current disk. This must be called
; before any filesystem operations
;
; @param headsPerCylinder [In] The number of heads per cylinder
; @param sectorsPerTrack  [In] The number of sectors per track
; @param bytesPerSector   [In] The number of bytes per sector
; @returns <none>
FsCommonSetGeometry16:
    STACK_FRAME_BEGIN16
    mov ax, ARG0_16
    mov word [wFsCommonHeadsPerCylinder], ax

    mov ax, ARG1_16
    mov word [wFsCommonSectorsPerTrack], ax

    mov ax, ARG2_16
    mov word [wFsCommonSectorSize], ax
    STACK_FRAME_END16
    ret

; **************************
; ReadSectorsCHS16
; @brief Reads a number of sectors from the disk. Because we read in segmented space only one
; sector is read at the time to take into account that our target buffer may overflow into a new
; segment.
;
; @param bufferSegment [0, In] The segment of the buffer address
; @param bufferOffset  [1, In] The offset into the segment of the buffer address
; @param count         [2, In] The number of sectors to read
; @param cylinder      [3, In] The disk cylinder of the sector start
; @param head          [4, In] The disk head of the sector start
; @param sector        [5, In] The sector component of the head
; @return none
; **************************
ReadSectorsCHS16:
    STACK_FRAME_BEGIN16
    sub sp, 8
    mov ax, es
    mov LVAR0_16, ax
    mov LVAR1_16, bx

    ; LVAR2 = retry_count
    ; LVAR3 = sectors_to_read
    mov ax, ARG2_16
    mov LVAR3_16, ax

    ; setup registers for bios call
    ; cl - sector
    ; ch - cylinder
    ; dl - drive
    ; dh - head
    ; es:bx - address
    mov ax, ARG5_16
    mov cl, al
    mov ax, ARG4_16
    mov dl, byte [bFsCommonDrive]
    mov dh, al
    mov ax, ARG3_16
    mov ch, al
    shr ax, 2
    and al, 0xC0
    or cl, al
    mov ax, ARG0_16
    mov es, ax
    mov bx, ARG1_16

    .retry:
        mov LVAR2_16, 5

    .read:
        mov ax, 0x0201
        int 0x13
        jnc .read_ok

    .read_failed:
        xor ax, ax ; perform disk reset
        int 0x13
        dec LVAR2_16
        jnz .read
        
        push szDiskError
        call Print16
        call SystemsFail

    .read_ok:
        mov ax, cx
        and ax, 0x003F ; only bits 0-5 are valid for sector
        inc al
        cmp al, byte [wFsCommonSectorsPerTrack]
        jb .no_sector_overflow
        and cl, 0xC0
        or cl, 0x01
        jmp .sector_overflow

        .no_sector_overflow:
            and cl, 0xC0
            or cl, al
            jmp .next_address

        .sector_overflow:
            ; now we do the same, but for heads
            inc dh
            cmp dh, byte [wFsCommonHeadsPerCylinder]
            jb  .next_address

            mov dh, 0
            cmp ch, 0xFF ; detect overflow here
            je .cylinder_overflow
            inc ch
            jmp .next_address ; no overflow, just increase and go

        .cylinder_overflow:
            mov ch, 0    ; clear lower byte of cylinder
            mov al, cl
            and al, 0x3F ; keep only sector bits
            and cl, 0xC0 ; keep only cylinder bits
            add cl, 0x40
            or cl, al    ; restore sector bits

    .next_address:
        add bx, word [wFsCommonSectorSize]
        jnc .next_read
        mov ax, es
        add ah, 0x10
        mov es, ax

    .next_read:
        dec LVAR3_16
        jnz .read

    ; restore registers
    mov bx, LVAR1_16
    mov ax, LVAR0_16
    mov es, ax
    STACK_FRAME_END16
    ret

; **************************
; ReadSectorsLBA16
; @brief Reads a number of sectors from the disk. Because we read in segmented space only one
; sector is read at the time to take into account that our target buffer may overflow into a new
; segment.
;
; @param bufferSegment [0, In] The segment of the buffer address
; @param bufferOffset  [1, In] The offset into the segment of the buffer address
; @param count         [2, In] The number of sectors to read
; @param sectorLbaLow  [3, In] The low part of the LBA sector start
; @param sectorLbaHigh [4, In] The high part of the LBA sector start
; @return none
; **************************
ReadSectorsLBA16:
    STACK_FRAME_BEGIN16

    mov ax, ARG4_16  ; load high word of LBA
    shl eax, 16      ; shift into upper 32 bits
    mov ax, ARG3_16  ; load low word of LBA
    
    ; add base sector onto start-sector
    add eax, dword [dBaseSector]
    ; adc edx, 0

    xor edx, edx                 ; always clear edx before div
    xor ecx, ecx
    mov cx, WORD [wFsCommonSectorsPerTrack]  ; convert LBA to CHS
    div ecx                      ; must perform 32 bit division
    inc dx                       ; adjust for sector 0, dl is sector
    push dx                      ; push sector

    xor dx, dx                   ; reset DX and calculate cylinder/head
    div WORD [wFsCommonHeadsPerCylinder]
    push dx                      ; push head
    push ax                      ; push cylinder
    
    mov ax, ARG2_16
    mov cx, ARG1_16
    mov dx, ARG0_16

    push ax
    push cx
    push dx
    call ReadSectorsCHS16
    add sp, 12 ; 6 parameters
    STACK_FRAME_END16
    ret

; **************************
; BIOS ReadSector 
; IN:
; 	- ES:BX: Buffer
;	- EAX: Sector start low-dword
;	- ESI: Sector start high-dword
; 	- ECX: Sector count
; 	- EDX: Sector size in bytes
;
; Registers:
; 	- Conserves all but ES:BX
; **************************
ReadSectorExtended16:
    pushad

    ; add base sector onto start-sector
    add eax, dword [dBaseSector]
    adc edx, 0

    ; store values into disk package
    mov word [DiskPackage.Segment], es
    mov word [DiskPackage.Offset], bx
    mov dword [DiskPackage.Sector], eax
    mov	dword [DiskPackage.Sector + 4], esi

    .ReadLoop:
        ; Setup Package
        mov word [DiskPackage.SectorsToRead], 1

        ; Setup INT
        push edx
        mov ax, 0x4200
        mov dl, byte [bFsCommonDrive]
        mov si, DiskPackage
        int 0x13

        ; It's important we check for offset overflow
        pop edx
        mov ax, word [DiskPackage.Offset]
        add ax, dx
        mov word [DiskPackage.Offset], ax
        test ax, ax
        jne .NoOverflow

    .Overflow:
        ; So overflow happened
        add word [DiskPackage.Segment], 0x1000
        mov word [DiskPackage.Offset], 0x0000

    .NoOverflow:
        ; Loop
        inc dword [DiskPackage.Sector]
        loop .ReadLoop

    .End:
    ; Restore registers 
    popad

    ; Save position
    push eax
    xor eax, eax
    mov ax, word [DiskPackage.Segment]
    mov es, ax
    mov bx, word [DiskPackage.Offset]
    pop eax
    ret

; ****************************
; FileSystemInitialize16
; @brief Initializes the filesystem subsystem based on the type
; of stage1 loader specified when called.
; @param FileSysType [In] The filesystem type to initialize
; @param DriveNum    [In] The drive number used for reading sectors
; @return <none>
; ****************************
FileSystemInitialize16:
    STACK_FRAME_BEGIN16

    mov ax, ARG0_16
    mov cx, ARG1_16
    mov byte [bFsCommonType], al
    mov byte [bFsCommonDrive], cl

    ; 2, 3, 4 => FAT
    cmp al, 2
    je  .init_fat12

    cmp al, 3
    je  .init_fat16

    cmp al, 4
    je  .init_fat32

    ; 5 => MFS1
    cmp al, 5
    je 	.init_mfs
    jmp .unknown

    .init_fat12:
        TRACE16 szFsSetup
        TRACE16 szFat12

        ; Setup
        pop 	ax
        mov 	al, FAT_TYPE_FLOPPY12
        call 	SetupFat
        jmp 	.done

    .init_fat16:
        TRACE16 szFsSetup
        TRACE16 szFat16

        ; Setup
        pop 	ax
        mov 	al, FAT_TYPE_HDD16
        call 	SetupFat
        jmp 	.done
        
    .init_fat32:
        TRACE16 szFsSetup
        TRACE16 szFat32

        ; Setup
        pop 	ax
        mov 	al, FAT_TYPE_HDD32
        call 	SetupFat
        jmp 	.done

    .init_mfs:
        TRACE16 szFsSetup
        TRACE16 szMfs
        call MfsInitialize16
        jmp .done

    .unknown:
        push szNotSupported
        call Print16
        call SystemsFail

    .done:
        TRACE16 szSuccess
        STACK_FRAME_END16
        ret


; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szFsSetup      db "000000000 [vboot] initializing ", 0x00
szFsLoading    db "loading kernel image", 0x00
szNotSupported db "filesystem is not supported", 0x00
szDiskError    db "disk error", 0x00
szMfs          db "MFS-1", 0x00
szFat12        db "FAT 12", 0x00
szFat16        db "FAT 16", 0x00
szFat32        db "Fat 32", 0x00

bFsCommonType db 0
bFsCommonDrive db 0
wFsCommonHeadsPerCylinder dw 0
wFsCommonSectorsPerTrack dw 0
wFsCommonSectorSize dw 0

; This is used for the extended read function (int 0x13)
DiskPackage:				db 0x10
                            db 0
    .SectorsToRead			dw 0
    .Offset					dw 0
    .Segment 				dw 0
    .Sector 				dq 0

%endif
