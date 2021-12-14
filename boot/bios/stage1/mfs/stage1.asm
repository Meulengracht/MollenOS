; Copyright 2011, Philip Meulengracht
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
; Vali BIOS bootloader (MFS, Stage1)
; - Calling convention for writing assembler in this bootloader
;   will be the usual cdecl. So that means arguments are pushed in reverse
;   order on the stack, EAX, ECX and EDX are caller-saved, and rest are calee-saved.
;   Return values are returned in EAX.
; TODO
;   - Read disk geometry from BIOS instead of MBR
;   - Remove the uart stuff and print stuff, figoure out how to handle errors
;

; Calling convention helpers
%macro STACK_FRAME_BEGIN 0
    push bp
    mov bp, sp
%endmacro
%macro STACK_FRAME_END 0
    mov sp, bp
    pop bp
%endmacro

%define ARG0 [bp + 4]
%define ARG1 [bp + 6]
%define ARG2 [bp + 8]
%define ARG3 [bp + 10]
%define ARG4 [bp + 12]
%define ARG5 [bp + 14]

%define LVAR0 word [bp - 2]
%define LVAR1 word [bp - 4]
%define LVAR2 word [bp - 6]
%define LVAR3 word [bp - 8]

; *******************************************************
; Memory Map:
; 0x00000000 - 0x000004FF		Reserved
; 0x00000500 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x0007FFFF		Kernel Loading Bay (480.5 Kb)
; Rest above is not reliable
%define UART_PORT_BASE          0x3F8
%define UART_PORT_DATA          UART_PORT_BASE
%define UART_PORT_IRQ_ENABLE    UART_PORT_BASE + 1
%define UART_PORT_FIFO		    UART_PORT_BASE + 2
%define UART_PORT_LINE_CONTROL  UART_PORT_BASE + 3
%define UART_PORT_MODEM_CONTROL UART_PORT_BASE + 4
%define UART_PORT_LINE_STATUS   UART_PORT_BASE + 5

; 16 Bit Code, Origin at 0x0
BITS 16
ORG 0x7C00

; Jump Code, 3 Bytes
jmp short entry
nop

; *************************
; MFS1 Boot Parameter Block
; *************************
dMagic						dd		0
bVersion					db		0
bFlags						db		0
bMediaType					db		0
wBytesPerSector				dw		0
wSectorsPerTrack			dw		0
wHeadsPerCylinder			dw		0
qTotalSectors				dq		0

wReservedSectorCount		dw		0
wSectorsPerBucket			dw		0

qMasterBucketSector			dq		0
qMasterBucketMirror			dq		0
szVolumeLabel				db		"MollenOS"

; *************************
; Bootloader Entry Point
; dl = drive number
; si = partition table entry
; *************************
entry:
    cli ; disable irqs while modifying stack
    jmp 0x0:fix_cs ; far jump to fix segment registers

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

    ; save dl (drive-num) and the partition information
    mov byte [bPhysicalDriveNum], dl
    mov di, PartitionData
    mov cx, 0x0008 ; 8 words = 16 bytes
    repnz movsw

    ; Initialize the UART so output is visible in serial aswell
    call InitializeUART

    ; Verify that CPU is not too old
    call Verify386
    test ax, ax
    jnz .cpu_ok
    push szCpuNotCompatable
    call Print
    cli
    hlt

    ; ok cpu is compatable (hopefully) lets continue to load stage2
    .cpu_ok:
        mov eax, dword [dBaseSector] ; load the base sector for the partition we are on
        inc	eax                      ; load from reserved sector start (sector N+1)

        xor edx, edx                 ; always clear edx before div
        xor ecx, ecx
        mov cx, WORD [wSectorsPerTrack]  ; convert LBA to CHS
        div ecx                      ; must perform 32 bit division
        inc dx                       ; adjust for sector 0, dx is sector, ax is cylinder
        push dx                      ; push sector

        xor dx, dx                   ; reset DX and calculate cylinder/head
        mov cx, WORD [wHeadsPerCylinder]
        div cx
        push dx                      ; push head
        push ax                      ; push cylinder

        mov dx, word [wReservedSectorCount]
        push dx                      ; push sector count
        push 0x0500                  ; push buffer segment offset
        push 0                       ; push segment
        call ReadSectorsCHS
        add sp, 12                   ; 6*2

        ; stage2 is now loaded at 0:0500, lets jump to bootcode.
        ; vboot specifies we must put the drive-num in DL and the partition
        ; data into si.
        mov dl, byte [bPhysicalDriveNum]
        mov si, PartitionData
        mov dh, 5
        jmp 0x0:0x500

; **************************
; ReadSectorsCHS
; @brief Reads a number of sectors from the disk. Because we read in segmented space only one
; sector is read at the time to take into account that our target buffer may overflow into a new
; segment.
; @param bufferSegment [0, In] The segment of the buffer address
; @param bufferOffset  [1, In] The offset into the segment of the buffer address
; @param count         [2, In] The number of sectors to read
; @param cylinder      [3, In] The disk cylinder of the sector start
; @param head          [4, In] The disk head of the sector start
; @param sector        [5, In] The sector component of the head
; @return none
; **************************
ReadSectorsCHS:
    STACK_FRAME_BEGIN
    sub sp, 8
    mov ax, es
    mov LVAR0, ax
    mov LVAR1, bx

    ; LVAR2 = retry_count
    ; LVAR3 = sectors_to_read
    mov ax, ARG2
    mov LVAR3, ax

    ; setup registers for bios call
    ; cl - [0-5]sector [6-7]cylinder_high
    ; ch - cylinder_low
    ; dl - drive
    ; dh - head
    ; es:bx - address
    mov ax, ARG5
    mov cl, al
    mov ax, ARG4
    mov dl, byte [bPhysicalDriveNum]
    mov dh, al
    mov ax, ARG3
    mov ch, al
    shr ax, 2
    and al, 0xC0
    or cl, al
    mov ax, ARG0
    mov es, ax
    mov bx, ARG1

    .retry:
        mov LVAR2, 5

    .read:
        mov ax, 0x0201
        int 0x13
        jnc .read_ok

    .read_failed:
        xor ax, ax ; perform disk reset
        int 0x13
        dec LVAR2
        jnz .read
        
        push szDiskError
        call Print
        cli
        hlt

    .read_ok:
        mov ax, cx
        and ax, 0x003F ; only bits 0-5 are valid for sector
        inc al
        cmp al, byte [wSectorsPerTrack]
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
            cmp dh, byte [wHeadsPerCylinder]
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
        add bx, word [wBytesPerSector]
        jnc .next_read
        mov ax, es
        add ah, 0x10
        mov es, ax

    .next_read:
        dec LVAR3
        jnz .read

    ; restore registers
    mov bx, LVAR1
    mov ax, LVAR0
    mov es, ax
    STACK_FRAME_END
    ret

; ********************************
; InitializeUART
; @brief Initializes the onboard UART so that print will also print to serial port.
; @param none
; @return none
; ********************************
InitializeUART:
    mov dx, UART_PORT_IRQ_ENABLE
    mov al, 0x00                   ; disable IRQs
    out dx, al

    mov dx, UART_PORT_LINE_CONTROL
    mov al, 0x80                   ; enable DLAB
    out dx, al

    mov dx, UART_PORT_DATA
    mov al, 0x03                   ; set BAUD to 38kbs
    out dx, al

    mov dx, UART_PORT_IRQ_ENABLE
    mov al, 0x00                   ; disable irqs again to make sure they haven't been turned on
    out dx, al

    mov dx, UART_PORT_LINE_CONTROL
    mov al, 0x03                   ; remove DLAB and set 8 bits, 1 stop bit, no parity
    out dx, al

    mov dx, UART_PORT_FIFO
    mov al, 0xC7                   ; enable FIFO with 14 bytes threshold
    out dx, al

    mov dx, UART_PORT_MODEM_CONTROL
    mov al, 0x0B                   ; enable IRQS and enable RTS/DSR
    out dx, al
    ret

; ********************************
; PrintChar
; @brief Prints a single character to the screen using INT10 if enabled by compiler support. The
; character is also multiplexed to serial port if enabled.
; @param character [In] The character to print
; @return none
; ********************************
PrintChar:
    STACK_FRAME_BEGIN
    push bx
    mov ax, ARG0
%ifdef __OSCONFIG_HAS_VIDEO
    mov ah, 0x0E
    mov bx, 0x00
    int 0x10
%endif

%ifdef __OSCONFIG_HAS_UART
    mov bl, al
    .WaitForTxEmpty:
        mov dx, UART_PORT_LINE_STATUS
        in  al, dx
        and al, 0x20
        cmp al, 0x20
        jne .WaitForTxEmpty
    mov al, bl
    mov dx, UART_PORT_DATA
    out dx, al
%endif
    pop bx
    STACK_FRAME_END
    ret

; ********************************
; Print
; @brief Prints out a zero terminated string to the screen using INT10 and
; also multiplexes output to serial output.
; @param string [In] The zero-terminated string to print
; @return            The number of characters printed
; ********************************
Print:
    STACK_FRAME_BEGIN
    push si
    mov si, ARG0
    mov dx, 0
    xor ax, ax   ; clear out ax beforehand
    .loop:
        lodsb      ; load next byte from string
        or	al, al ; check for zero terminator
        jz	.done

        push ax        ; push argument
        call PrintChar
        add sp, 2      ; cleanup stack

        inc dx
        jmp	.loop

    .done:
        pop si
        mov ax, dx
        STACK_FRAME_END
        ret

; ********************************
; Verify386
; @brief Verifies whether the CPU is atleast a 386 cpu. We require 32 bit registers
; and 32 bit support before even proceeding, otherwise the system is simply to old.
; @return Returns 0 if the CPU is not a 386.
Verify386:
    pushf               ; Preserve the flags
    mov ax, 7000h       ; Set the NT and IOPL flag bits only available for
                        ; 386 processors and above
    push ax             ; push ax so we can pop 7000h into the flag register
    popf                ; pop 7000h off of the stack
    pushf               ; push the flags back on
    pop ax              ; get the pushed flags into ax
    and ah, 0x70        ; see if the NT and IOPL flags are still set
    mov ax, 0           ; set ax to the 286 value
    jz  .exit           ; If NT and IOPL not set it's a 286
    inc ax              ; ax now is 1 to indicate 386 or higher

    .exit:
        popf
        ret

; **************************
; Variables
; **************************
szCpuNotCompatable db "cpu old", 0x00
szDiskError db "disk error", 0x00
bPhysicalDriveNum db 0

; store the partition entry from the mbr
PartitionData     dq 0
dBaseSector       dd 0
dSectorCount      dd 0

; epilogue of the boot sector 
times 510-($-$$) db 0
db 0x55, 0xAA
