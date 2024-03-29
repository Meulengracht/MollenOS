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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; PELoader.inc
; - Contains routines for loading and relocating PE executables
; 

%ifndef _MBOOT_PELOADER_INC_
%define _MBOOT_PELOADER_INC_

BITS 32

; Definitions
%define DOS_MAGIC 		0x5A4D
%define PE_MAGIC 		0x00004550
%define PE_OPT_MAGIC32 	0x010b
%define PE_OPT_MAGIC64 	0x020b

%define PE_SECTION_EXEC 	0x20000000
%define PE_SECTION_CODE 	0x00000020
%define PE_SECTION_DATA		0x00000040
%define PE_SECTION_ZERO		0x00000080

; Structures
struc PEDosHeader
    .DosMagic			resw	1		; Must contain MZ
    .NumBytesLastPages	resw	1
    .NumPages			resw	1
    .NumRelocEntries	resw	1
    .NumParagraphs		resw	1
    .MinParagraphs		resw	1
    .MaxParagraphs		resw	1
    .InitialSS			resw	1
    .InitialSP			resw	1
    .Checksum			resw	1
    .InitialIP			resw	1
    .InitialCS			resw	1
    .RelocTableAddr		resw	1
    .OverlayNum			resw	1
    .Reserved			resw	4
    .OemId				resw	1
    .OemInfo			resw	1
    .Reserved1			resw	10
    .HeaderAddr			resd	1
endstruc

struc PEMainHeader
    .PeMagic 			resd 	1 		; Must contain PE\0\0
    
    ; File Header Begins Here
    .MachineType 		resw 	1
    .NumSections 		resw 	1
    .DateStamp			resd 	1
    .SymbolTablePtr 	resd 	1
    .NumSymbols 		resd 	1
    .OptHeaderSize	 	resw 	1
    .Attributes 		resw 	1
endstruc

struc PEOptionalHeader32
    ; Optional header begins here 
    .PeOptMagic 		resw 	1
    .MajorLinkVersion 	resb 	1
    .MinorLinkVersion 	resb 	1
    .SizeOfCode 		resd 	1
    .SizeOfInitData 	resd 	1
    .SizeOfUninitData 	resd 	1
    .EntryPoint 		resd 	1
    .BaseOfCode 		resd 	1

    .BaseOfData 		resd 	1
    .ImageBase 			resd 	1
    .SectionAlignment 	resd 	1
    .FileAlignment		resd 	1
    .Reserved           resb    16
    .ImageSize			resd 	1
    .HeaderSize 		resd 	1
    .Checksum 			resd 	1
    .Subsystem			resw 	1
    .DllAttribs			resw 	1
    .Reserved1          resb    16
    .LoaderFlags		resd 	1
    .NumRVA				resd 	1
    .ExportAddrRva		resd	1
    .ExportSize			resd 	1
endstruc

struc PEOptionalHeader64
    ; Optional header begins here 
    .PeOptMagic 		resw 	1
    .MajorLinkVersion 	resb 	1
    .MinorLinkVersion 	resb 	1
    .SizeOfCode 		resd 	1
    .SizeOfInitData 	resd 	1
    .SizeOfUninitData 	resd 	1
    .EntryPoint 		resd 	1
    .BaseOfCode 		resd 	1

    .BaseAddress 		resq 	1
    .SectionAlignment 	resd 	1
    .FileAlignment 	    resd 	1
    .Reserved           resb    16
    .SizeOfImage        resd    1
    .SizeOfHeaders      resd 	1
    .ImageChecksum      resd    1
    .SubSystem          resw    1
    .DllAttributes      resw    1
    .Reserved1          resb    32
    .LoaderFlags        resd    1
    .NumDataDirectories resd    1
    .ExportAddrRva		resd	1
    .ExportSize			resd 	1
endstruc

struc PESectionHeader
    .Name 				resb 	8
    .VirtualSize		resd 	1
    .VirtualAddr		resd 	1
    .RawDataSize 		resd 	1
    .RawDataPtr			resd 	1
    .RelocationPtr		resd 	1
    .LineNumPtr			resd 	1
    .NumRelocations		resw 	1
    .NumLineNums		resw 	1
    .Attributes			resd 	1
endstruc

; ********************************
; PELoad
; @brief Loads a PE executable into memory
;
; @param buffer [In] Pointer to the buffer containing the PE executable
; @param destinationAddress [In] The address to load the executable to
; @returns [eax] Size of the image loaded, 0 if error
; @returns [ecx] Lower 32 bits of entry point of the image, 0 if error
; @returns [edx] Upper 32 bits of entry point of the image, 0 if error
; ********************************
PELoad:
    STACK_FRAME_BEGIN32
    sub esp, 0x20
    push ebx
    push esi
    push edi

    ; LVAR0 = PE File Buffer
    ; LVAR1 = Relocation Address
    ; LVAR2 = Entry Point (lower 32 bit)
    ; LVAR3 = Entry Point (upper 32 bit)
    ; LVAR4 = Last Section Address
    ; LVAR5 = Section iterator
    mov eax, ARG1_32
    mov LVAR1_32, eax

    mov eax, ARG0_32
    mov LVAR0_32, eax

    ; zero out upper 32 bit
    mov LVAR3_32, 0
    
    ; Copy DOS header
    mov esi, eax
    mov ecx, 16 			; 16 * 4 = 64
    mov edi, DosHeader
    cld
    rep movsd
    mov esi, eax

    ; Check the DOS header magic
    mov ax, word [DosHeader + PEDosHeader.DosMagic]
    cmp ax, DOS_MAGIC
    jne .NotPE

    ; Get pe header address 
    mov eax, dword [DosHeader + PEDosHeader.HeaderAddr]
    add esi, eax
   
    ; Check PE magic
    mov eax, dword [esi]
    cmp eax, PE_MAGIC
    jne .NotPE

    ; Read the PE header
    mov ecx, 6 			; 6 * 4 = 24
    mov edi, PEHeader
    cld
    rep movsd
    
    ; Determine the kind of PE file
    mov ax, word [esi]
    cmp ax, PE_OPT_MAGIC32
    je .PE32
    cmp ax, PE_OPT_MAGIC64
    je .PE64
    jmp .NotPE

    .PE32:
        ; Copy PE header
        push esi
        mov ecx, 26 			; 26 * 4 = 104
        mov edi, PEOptHeader32
        cld
        rep movsd
        pop esi

        ; Get entry point
        mov eax, dword [PEOptHeader32 + PEOptionalHeader32.ImageBase]
        add eax, dword [PEOptHeader32 + PEOptionalHeader32.EntryPoint]
        mov LVAR2_32, eax
        jmp .prepare_loop

    .PE64:
        ; Copy PE header
        push esi
        mov ecx, 30 			; 30 * 4 = 120
        mov edi, PEOptHeader64
        cld
        rep movsd
        pop esi

        ; Get entry point
        mov	eax, dword [PEOptHeader64 + PEOptionalHeader64.BaseAddress]
        mov ecx, dword [PEOptHeader64 + PEOptionalHeader64.BaseAddress + 4]
        add	eax, dword [PEOptHeader64 + PEOptionalHeader64.EntryPoint]
        adc ecx, 0
        mov	LVAR2_32, eax
        mov LVAR3_32, ecx

    ; Get sections
    .prepare_loop:
        xor eax, eax
        mov ax, word [PEHeader + PEMainHeader.OptHeaderSize]
        add esi, eax
        
        xor ecx, ecx
        mov cx, word [PEHeader + PEMainHeader.NumSections]
        mov LVAR5_32, ecx

    ; Now we need to relocate
    .section_loop:
        ; Copy Section Header
        mov ecx, 10 			; 10 * 4 = 40
        mov edi, PESection
        cld
        rep movsd

        ; Save locations
        push esi

        ; Calculate file offset
        mov esi, LVAR0_32
        add esi, dword [PESection + PESectionHeader.RawDataPtr]

        ; Calculate memory offset
        mov edi, LVAR1_32
        add edi, dword [PESection + PESectionHeader.VirtualAddr]

        ; How much stuff should be copy?
        mov ecx, dword [PESection + PESectionHeader.RawDataSize]
        mov ebx, dword [PESection + PESectionHeader.VirtualSize]

        ; Size Check
        test ecx, ecx
        je .zero_section0

        ; Copy or zero section? :-)
        mov eax, dword [PESection + PESectionHeader.Attributes]

        ; Code or Data?
        and eax, PE_SECTION_CODE
        cmp eax, PE_SECTION_CODE
        je .copy_section

        mov eax, dword [PESection + PESectionHeader.Attributes]
        and eax, PE_SECTION_DATA
        cmp eax, PE_SECTION_DATA
        je .copy_section

        mov eax, dword [PESection + PESectionHeader.Attributes]
        and eax, PE_SECTION_ZERO
        cmp eax, PE_SECTION_ZERO
        jne .next_section

        ; If the other fails, its zero
        .zero_section:
            xor eax, eax
            cld
            rep stosb
            jmp .next_section

        .zero_section0:
            xor eax, eax
            mov ecx, ebx
            cld
            rep stosb
            jmp .next_section

        .ZeroRemaining:
            xor eax, eax
            sub ebx, ecx
            mov ecx, ebx
            cld
            rep stosb
            jmp .next_section

        ; Copy data
        .copy_section:
            push ecx
            shr ecx, 2 	; divide by 4
            add ecx, 1 	; In case of remainder
            cld
            rep movsd
            pop ecx
            cmp ecx, ebx
            jb .ZeroRemaining

        .next_section:
            ; Save last edi
            mov	LVAR4_32, edi

            ; Restore location
            pop esi

            ; Move to next section
            dec	dword LVAR5_32
            jnz .section_loop
            jmp .success

    .NotPE:
        xor eax, eax
        xor ecx, ecx
        jmp .exit

    .success:
        ; Page align up and calculate size
        mov	eax, LVAR4_32
        and eax, 0xFFFFF000
        add	eax, 0x1000

        sub	eax, LVAR1_32 ; return size
        mov ecx, LVAR2_32 ; return lower 32 bit of entry point
        mov edx, LVAR3_32 ; return upper 32 bit of entry point

    .exit:
        pop edi
        pop esi
        pop ebx
        STACK_FRAME_END32
        ret

; DOS Header (64 Bytes)
DosHeader:
istruc PEDosHeader
    at PEDosHeader.DosMagic,			dw	0
    at PEDosHeader.NumBytesLastPages,	dw	0
    at PEDosHeader.NumPages,			dw	0
    at PEDosHeader.NumRelocEntries,		dw	0
    at PEDosHeader.NumParagraphs,		dw	0
    at PEDosHeader.MinParagraphs,		dw	0
    at PEDosHeader.MaxParagraphs,		dw	0
    at PEDosHeader.InitialSS,			dw	0
    at PEDosHeader.InitialSP,			dw	0
    at PEDosHeader.Checksum,			dw	0
    at PEDosHeader.InitialIP,			dw	0
    at PEDosHeader.InitialCS,			dw	0
    at PEDosHeader.RelocTableAddr,		dw	0
    at PEDosHeader.OverlayNum,			dw	0
    at PEDosHeader.Reserved,			dw	0, 0, 0, 0
    at PEDosHeader.OemId,				dw	0
    at PEDosHeader.OemInfo,				dw	0
    at PEDosHeader.Reserved1,			dw	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    at PEDosHeader.HeaderAddr,			dd	0
iend

; PE MainHeader (24 Bytes)
PEHeader:
istruc PEMainHeader
    at PEMainHeader.PeMagic, 			dd 	0 		; Must contain PE\0\0
    ; File Header Begins Here
    at PEMainHeader.MachineType, 		dw 	0
    at PEMainHeader.NumSections, 		dw 	0
    at PEMainHeader.DateStamp,			dd 	0
    at PEMainHeader.SymbolTablePtr, 	dd  0
    at PEMainHeader.NumSymbols, 		dd 	0
    at PEMainHeader.OptHeaderSize,	 	dw 	0
    at PEMainHeader.Attributes,			dw 	0 ; 24
iend

; PE Optional Header 32 bit (104 Bytes)
PEOptHeader32:
istruc PEOptionalHeader32
    ; Optional (Shared) Header Begins Here 
    at PEOptionalHeader32.PeOptMagic, 		dw 	0
    at PEOptionalHeader32.MajorLinkVersion, db 	0
    at PEOptionalHeader32.MinorLinkVersion, db 	0
    at PEOptionalHeader32.SizeOfCode, 		dd 	0
    at PEOptionalHeader32.SizeOfInitData, 	dd 	0
    at PEOptionalHeader32.SizeOfUninitData, dd 	0 ; 16
    at PEOptionalHeader32.EntryPoint, 		dd 	0
    at PEOptionalHeader32.BaseOfCode, 		dd 	0

    ; Optional (32 bit) Header Begins Here
    at PEOptionalHeader32.BaseOfData, 		dd 	0
    at PEOptionalHeader32.ImageBase, 		dd 	0
    at PEOptionalHeader32.SectionAlignment, dd 	0
    at PEOptionalHeader32.FileAlignment,	dd 	0 ; 40
    at PEOptionalHeader32.Reserved, 	    db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    at PEOptionalHeader32.ImageSize,		dd 	0
    at PEOptionalHeader32.HeaderSize, 		dd 	0
    at PEOptionalHeader32.Checksum, 		dd 	0 ; 68
    at PEOptionalHeader32.Subsystem,		dw 	0
    at PEOptionalHeader32.DllAttribs,		dw 	0
    at PEOptionalHeader32.Reserved1, 	    db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    at PEOptionalHeader32.LoaderFlags,		dd 	0
    at PEOptionalHeader32.NumRVA,			dd 	0 ; 96
    at PEOptionalHeader32.ExportAddrRva,	dd	0 ; 100
    at PEOptionalHeader32.ExportSize,		dd	0 ; 104
iend

PEOptHeader64:
istruc PEOptionalHeader64
    ; Optional (Shared) Header Begins Here 
    at PEOptionalHeader64.PeOptMagic, 		dw 	0
    at PEOptionalHeader64.MajorLinkVersion, db 	0
    at PEOptionalHeader64.MinorLinkVersion, db 	0
    at PEOptionalHeader64.SizeOfCode, 		dd 	0
    at PEOptionalHeader64.SizeOfInitData, 	dd 	0
    at PEOptionalHeader64.SizeOfUninitData, dd 	0 ; 16
    at PEOptionalHeader64.EntryPoint, 		dd 	0
    at PEOptionalHeader64.BaseOfCode, 		dd 	0

    ; Optional (64 bit) Header Begins Here
    at PEOptionalHeader64.BaseAddress, 		dq 	0 ; 32
    at PEOptionalHeader64.SectionAlignment, dd 	0
    at PEOptionalHeader64.FileAlignment,	dd 	0 ; 40
    at PEOptionalHeader64.Reserved, 	    db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    at PEOptionalHeader64.SizeOfImage,		dd 	0
    at PEOptionalHeader64.SizeOfHeaders, 	dd 	0
    at PEOptionalHeader64.ImageChecksum,	dd 	0 ; 68
    at PEOptionalHeader64.SubSystem,		dw 	0
    at PEOptionalHeader64.DllAttributes,	dw 	0
    at PEOptionalHeader64.Reserved1, 	    db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    at PEOptionalHeader64.LoaderFlags,		dd 	0 ; 108
    at PEOptionalHeader64.NumDataDirectories,   dd  0 ; 112
    at PEOptionalHeader64.ExportAddrRva,	dd	0 ; 116
    at PEOptionalHeader64.ExportSize,		dd	0 ; 120
iend

; PE Section Header (40 Bytes)
PESection:
istruc PESectionHeader
    at PESectionHeader.Name, 				db 	0, 0, 0, 0, 0, 0, 0, 0
    at PESectionHeader.VirtualSize,			dd 	0
    at PESectionHeader.VirtualAddr,			dd 	0
    at PESectionHeader.RawDataSize, 		dd 	0
    at PESectionHeader.RawDataPtr,			dd 	0
    at PESectionHeader.RelocationPtr,		dd 	0
    at PESectionHeader.LineNumPtr,			dd 	0
    at PESectionHeader.NumRelocations,		dw 	0
    at PESectionHeader.NumLineNums,			dw 	0
    at PESectionHeader.Attributes,			dd 	0
iend

%endif
