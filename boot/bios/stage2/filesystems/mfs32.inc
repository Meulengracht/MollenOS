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
; Mfs.inc
; - Contains filesystem routines for MFS
; 

%ifndef _MBOOT_MFS32_INC_
%define _MBOOT_MFS32_INC_

%define BUCKETMAP_ADDRESS_SEGMENT 0
%define BUCKETMAP_ADDRESS_OFFSET  0x7E00

BITS 32

; **************************
; MfsGetNextBucket
; @brief Returns the information related to the bucket. A bucket consists
; of a length and a link. Dead links are 0xFFFFFFFF.
; 
; @param bucket [0, In]  The bucket number to get the information from
; @return       [eax] The length of the bucket
; @return       [ecx] The link of the bucket
; **************************
MfsGetNextBucket:
    STACK_FRAME_BEGIN32
    push ebx
    push esi

    mov esi, ARG0_32

    ; Calculte Bucket Sector Offset
    ; BucketsPerSector = BytesPerSector / 8;
    ; SectorOffset = Bucket / BucketsPerSector;
    ; SectorIndex = Bucket % BucketsPerSector;
    xor eax, eax
    xor edx, edx ; Zero out edx before division!!
    mov ax, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
    mov	ebx, 0x8
    div ebx
    mov	ebx, eax ; ebx = BucketsPerSector
    xor	edx, edx
    mov	eax, esi
    div	ebx

    ; EAX contains sector
    ; EDX contains remainder
    push edx
    push eax

    mov	eax, dword [qBucketMapSector]
    mov	edx, dword [qBucketMapSector + 4]
    pop	ebx

    ; addition
    xor	ecx, ecx
    add	eax, ebx
    adc	edx, ecx
    mov	esi, edx

    ; do not re-read the bucket sector if it's loaded already
    cmp eax, dword [dLastReadBucketSector]
    je  .lookup

    ; ok, lets read the sector and mark this is the active
    mov dword [dLastReadBucketSector], eax
    push eax
    push word 1
    push word BUCKETMAP_ADDRESS_OFFSET
    push word BUCKETMAP_ADDRESS_SEGMENT
    mov eax, ReadSectorsLBA16
    push ax
    call BiosCallWrapper
    add esp, 12

    ; Find Entry
    .lookup:
        pop edx                         ; Get the remainder of the division
        xchg esi, edx
        shl	esi, 3
        mov ecx, dword [es:bx + si]     ; move link into ecx
        mov eax, dword [es:bx + si + 4] ; move length into eax

    pop esi
    pop ebx
    STACK_FRAME_END32
    ret

; **************************
; MfsReadBucket
; @brief Reads entire bucket into memory.
;
; @param bucket        [0, In] The bucket to read
; @param length        [1, In] The length of the bucket to read
; @param offset        [2, In] The offset to read from
; @param bufferSegment [3, In] The segment of the buffer to read into
; @param bufferOffset  [4, In] The offset of the buffer to read into
; @return              [eax] The length of the bucket
; @return              [ecx] The link of the bucket
; @return              [edx] buckets read
; **************************
MfsReadBucket:
    STACK_FRAME_BEGIN32
    sub esp, 4
    push esi
    push ebx

    ; LVAR0 = bucketsRead

    ; Calculate Sector
    ; Sector = ReservedCount +  (Bucket + Index) * SectorsPerBucket
    mov	eax, ARG0_32 ; bucket to read from
    mov edx, ARG2_32 ; offset of the bucket to support partial reads
    add eax, edx

    xor	esi, esi
    mov	si, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
    xor	ecx, ecx
    call Mul64

    ; eax is low dword
    ; edx is high dword
    xor ecx, ecx
    mov cx, word [MfsBootSector + MfsBootSectorStruct.ReservedSectors]
    add eax, ecx
    adc edx, 0

    push eax ; store the start sector located in eax:edx
    push edx

    mov ecx, ARG1_32 ; calculate sector count
    mov eax, ARG2_32 ; subtract the offset from the length
    sub ecx, eax

    xor eax, eax
    xor edx, edx
    mov ax, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
    mul ecx

    ; make sure the number of sectors to read fits into cx
    cmp eax, 0xFFFF
    jb  .fitted
    xor ecx, ecx
    mov cx, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
    .fit:
        sub eax, ecx
        cmp eax, 0xFFFF
        ja  .fit

    .fitted:
        mov ebx, eax
        div ecx
        mov LVAR0_32, eax
        mov ecx, eax ; ecx is now how many sectors to read

    pop edx ; restore start sector into eax:edx
    pop eax

    ; push arguments to stack for readsectors
    push eax ; ASSUMPTION HERE IS THAT START SECTOR FITS INTO ONLY EAX!!!!!
    push cx  ; WE CAN READ A MAXIMUM OF 0xFFFF sectors at once! 
    mov eax, ARG4_32 ; offset of the buffer
    push ax
    mov eax, ARG3_32 ; segment of the buffer
    push ax
    mov eax, ReadSectorsLBA16
    push ax
    call BiosCallWrapper
    add esp, 12

    ; get bucket information so we can load information
    ; about the next bucket
    mov  eax, ARG0_32
    push eax
    call MfsGetNextBucket
    add esp, 4

    mov edx, LVAR0_32
    pop ebx
    pop esi
    STACK_FRAME_END32
    ret

; ****************************
; MfsGetTokenLength
; @brief Calculates the length of a string untill path seperator
; @param szString [In] String to calculate length of
; @return              Length of string
; @sideeffects         If eos is reached, bMfsEndOfString is set to 1
; ****************************
MfsGetTokenLength:
    mov	eax, XARG0_32
    mov ecx, 0
    .count:
        mov	al, [ds:eax]
        cmp al, 0x2F     ; test against '/'
        je .done
        test al, al
        je .eos
        inc ecx
        inc eax
        jmp .count

    .eos:
        mov	byte [bMfsEndOfString], 1
    .done:
        mov eax, ecx
        ret

; ****************************
; MfsStringCompare
; @brief Compares two strings to each other
; @param szString1 [In] String to compare
; @param szString2 [In] String to compare
; @param length    [In] Length of the strings to compare
; @return               0 if not equal, 1 if equal
; ****************************
MfsStringCompare:
    mov	eax, XARG0_32

; ****************************
; MfsOpenFile
; @brief Opens a file and fills the provided handle with information
; relating to the file instance. This can then be used to call MfsReadFile
; @param filePath   [0, In] The path of the file to open.
; @param fileHandle [1, In] A pointer to the file-handle that should be used.
; @return Returns 0 on failure
; ****************************
MfsOpenFile:
    STACK_FRAME_BEGIN32
    sub esp, 0x20    ; space for 8 variables

    ; reset bMfsEndOfString
    mov byte [bMfsEndOfString], 0

    ; LVAR0 is path pointer
    ; LVAR1 is the token length
    ; LVAR2 is current bucket
    ; LVAR3 is current bucket length
    ; LVAR4 is next bucket
    ; LVAR5 is free
    ; LVAR6 is free
    ; LVAR7 is free
    mov eax, ARG0_32
    mov LVAR0_32, eax

    ; read the root directory to start looking for the file
    mov eax, dword [dRootBucket]
    mov LVAR2_32, eax

    push eax
    call MfsGetNextBucket
    add esp, 4

    ; eax is the length of the root bucket
    ; ecx is the link to the next
    mov LVAR3_32, eax
    mov LVAR4_32, ecx

    ; Initialize handle data
    ; Data1 = CurrentBucket
    ; Data2 = CurrentBucketLength
    ; Data3 = CurrentBucketOffset
    mov eax, ARG1_32
    mov dword [eax + FileHandle.Position], 0
    mov dword [eax + FileHandle.Size], 0
    mov dword [eax + FileHandle.Data1], 0
    mov dword [eax + FileHandle.Data2], 0
    mov dword [eax + FileHandle.Data3], 0

    .chain_loop:       
        mov eax, LVAR0_32
        push eax 
        call MfsGetTokenLength
        add esp, 4
        mov LVAR1_32, eax

        .bucket_loop:
            ; load the current bucket into the loading bay
            push FILELOADING_BAY_OFFSET
            push FILELOADING_BAY_SEGMENT
            push 0             ; offset 0 for reading directories
            mov eax, LVAR3_32  ; push bucket length
            push eax
            mov eax, LVAR2_32  ; push bucket
            push eax
            call MfsReadBucket
            add esp, 20

            ; eax contains length of bucket (already have it)
            ; ecx contains link of bucket
            ; edx contains number of buckets read
            mov LVAR4_32, ecx ; store link to next bucket

            ; calculate the number of entries for this bucket
            mov ax, word [wMfsEntriesPerBucket]
            mov cx, dx
            xor dx, dx
            mul cx
            mov cx, ax

            mov esi, FILELOADING_BAY_LINEAR
            .directory_loop:
                test byte [es:esi + 3], 0x80 ; bit 31 is set when the entry is in use
                jz .next_entry

                ; No, phew, lets check if filename matches
                mov eax, LVAR1_32
                push eax
                mov eax, LVAR0_32
                push eax
                mov eax, esi
                add eax, 0x44
                push eax
                call MfsStringCompare
                add esp, 12

                test al, al
                jz .next_entry

                ; We found an entry that matched the filename we were looking for
                ; lets get the bucket data
                mov	eax, dword [es:esi + 0x4]
                mov LVAR2_32, eax
                mov edx, dword [es:esi + 0x8]
                mov LVAR3_32, edx

                ; Was it end of chain?
                cmp	byte [bMfsEndOfString], 1
                je .found_entry

                ; We have to go into the directory
                mov LVAR2_32, eax ; store bucket
                mov LVAR3_32, edx ; store length

                ; Fixup path tokens
                mov eax, LVAR0_32
                add eax, LVAR1_32
                inc eax            ; skip the '/'
                mov LVAR0_32, eax
                jmp .chain_loop ; reset loop

                .found_entry:
                    mov ecx, ARG1_32
                    mov dword [ecx + FileHandle.Data1], eax
                    mov dword [ecx + FileHandle.Data2], edx
                    mov eax, dword [es:esi + 0x30]
                    mov dword [ecx + FileHandle.Size], eax
                    mov eax, 1
                    jmp .exit

                .next_entry:
                    add esi, 512
                    dec cx
                    jnz .directory_loop
                    ; drop down to next bucket

            .next_bucket:
                ; get next bucket
                mov eax, LVAR4_32
                cmp eax, 0xFFFFFFFF
                je .exit ; end of directory, not found
                
                mov LVAR2_32, eax ; store link as current bucket now
                push eax
                call MfsGetNextBucket
                add esp, 4

                mov LVAR3_32, eax ; store length of bucket
                mov LVAR4_32, ecx ; store link to next bucket
                jmp .bucket_loop  ; next bucket in directory


    .fail:
        xor eax, eax
    .exit:
        STACK_FRAME_END32
        ret

; ****************************
; MfsReadFile
; @brief Reads data from a file from the current position
; @param fileHandle [0, In] The handle of the file to read from
; @return                   Returns the number of bytes read
; ****************************
MfsReadFile:
    STACK_FRAME_BEGIN32
    push esi

    ; Data1 = CurrentBucket
    ; Data2 = CurrentBucketLength
    ; Data3 = CurrentBucketOffset
    mov esi, ARG0_32

    ; Calculate the number of buckets that fit into bay area
    xor eax, eax
    mov ax, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
    mov ecx, dword [esi + FileHandle.BaySize]
    div ecx
    mov ecx, eax
    xor eax, eax
    mov ax, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
    div ecx

    ; eax now contains the number of buckets that fit into bay area
    ; Now calculate the start bucket to read, and the length
    mov ecx, dword [esi + FileHandle.Data3] ; load offset
    mov edx, dword [esi + FileHandle.Data2] ; load length
    sub edx, ecx                            ; available length of bucket
    cmp edx, eax                            ; does it fit?
    jbe .load_loop                              ; it fits

    ; otherwise we fit it
    mov edx, eax                            ; cap it

    .load_loop:
        mov eax, dword [eax + FileHandle.BayOffset]
        push eax
        mov eax, dword [eax + FileHandle.BaySegment]
        push eax
        mov eax, dword [esi + FileHandle.Data3]
        push eax
        push edx
        mov eax, dword [esi + FileHandle.Data1]
        push eax
        call MfsReadBucket
        add esp, 20
        
        ; did we read it all?
        ; are we done?
        ; are we out of buffer space?

        cmp esi, 0xFFFFFFFF
        jne	.load_loop

    pop esi
    STACK_FRAME_END32
    ret

; ****************************
; 64 Bit Division
; IN:
;	- EAX: Low Dword
;	- EDX: High Dword
;	- ECX: Divisor Value
; OUT:
;	- EAX: Low Dword Result
;	- EDX: High Dword Result
; ****************************
Div64:
    ; Save ebx & init
    push ebx
    xor	ebx, ebx

    ; Save lower 
    push eax
    mov eax, edx
    xor edx, edx

    ; Don't divide by zero
    test eax, eax
    je .SkipHigh

    ; Divide high by ECX
    div ecx 
    mov	ebx, eax
    
    .SkipHigh:
    pop eax
    div ecx
    mov edx, ebx
    pop ebx
    ret

; ****************************
; 64 Bit Mul
; IN:
;	- EAX: Right hand
;	- ESI: Low Dword left hand
;	- ECX: High Dword left hand
; OUT:
;	- EAX: Low Dword Result
;	- EDX: High Dword Result
; ****************************
Mul64:
    mov edx, eax
    sar edx, 31
    imul ecx, eax ; ecx = y_high *{signed} x
    mov ebx, edx
    imul ebx, esi ; ebx = sign_extension(x) *{signed} y_low
    add ecx, ebx ; ecx = y_high *{signed} x_low + x_high *{signed} y_low
    mul esi ; edx:eax = x_low *{unsigned} y_low
    lea edx, [ecx + edx] ; edx = high(x_low *{unsigned} y_low + y_high *{signed} x_low + x_high *{signed} y_low)
    ret

%endif
