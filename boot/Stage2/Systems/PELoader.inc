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
	; Optional Header Begins Here 
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
	.MajorOSVersion 	resw	1
	.MinorOSVersion 	resw 	1
	.MajorFileVersion 	resw 	1
	.MinorFileVersion 	resw 	1
	.MajorSubSysVersion resw 	1
	.MinorSubSysVersion resw 	1
	.Win32Version		resd 	1
	.ImageSize			resd 	1
	.HeaderSize 		resd 	1
	.Checksum 			resd 	1
	.Subsystem			resw 	1
	.DllAttribs			resw 	1
	.SizeStackReserve 	resd 	1
	.SizeStackCommit 	resd 	1
	.SizeHeapReserve 	resd 	1
	.SizeHeapCommit 	resd 	1
	.LoaderFlags		resd 	1
	.NumRVA				resd 	1
	.ExportAddrRva			resd	1
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
; IN:
; 	- ESI: PE File Buffer
; 	- EDI: Relocation Address
; OUT:
; 	- EAX: Status (0 - Success, 1 - Invalid File Format, 2 - Error)
; 	- EBX: Entry Point
; ********************************
PELoad:
	; Save state
	pushad
	mov 	dword [dPEFileBase], esi

	; Save target address
	mov	dword [BootDescriptor + MollenOsBootDescriptor.KernelAddr], edi

	; Copy DOS header
	pushad
	mov 	ecx, 16 			; 16 * 4 = 64
	mov 	edi, DosHeader
	cld
	rep 	movsd
	popad

	; Do checks
	xor 	eax, eax
	mov 	ax, word [DosHeader + PEDosHeader.DosMagic]
	cmp 	ax, DOS_MAGIC
	jne 	.NotPE

	; Get next header address 
	mov 	eax, dword [DosHeader + PEDosHeader.HeaderAddr]
	add 	esi, eax

	; Copy PE header
	pushad
	mov 	ecx, 32 			; 30 * 4 = 120
	mov 	edi, PEHeader
	cld
	rep 	movsd
	popad

	; Do checks
	xor 	eax, eax
	mov 	eax, dword [PEHeader + PEMainHeader.PeMagic]
	cmp 	eax, PE_MAGIC
	jne 	.NotPE

	; We only support 32 bit for now
	mov 	ax, word [PEHeader + PEMainHeader.PeOptMagic]
	cmp 	ax, PE_OPT_MAGIC32
	jne 	.NotPE

	; Get entry point
	mov 	eax, dword [PEHeader + PEMainHeader.ImageBase]
	add 	eax, dword [PEHeader + PEMainHeader.EntryPoint]
	mov 	dword [dPEEntryPoint], eax

	; Get export section
	mov	eax, dword [PEHeader + PEMainHeader.ExportAddrRva]
	mov	dword [BootDescriptor + MollenOsBootDescriptor.ExportAddr], eax
	mov	eax, dword [PEHeader + PEMainHeader.ExportSize]
	mov	dword [BootDescriptor + MollenOsBootDescriptor.ExportSize], eax

	; Get sections
	xor 	eax, eax
	xor 	ecx, ecx
	mov 	ax, word [PEHeader + PEMainHeader.OptHeaderSize]
	add 	esi, eax
	add 	esi, 24  ; Size of FILE header
	mov 	cx, word [PEHeader + PEMainHeader.NumSections]

	; Now we need to relocate
	.cLoop:
		; Copy Section Header
		pushad
		mov 	ecx, 10 			; 10 * 4 = 40
		mov 	edi, PESection
		cld
		rep 	movsd
		popad

		; Save locations
		push 	esi
		push 	edi
		push 	ecx

		; Calculate file offset
		mov 	esi, dword [dPEFileBase]
		add 	esi, dword [PESection + PESectionHeader.RawDataPtr]

		; Calculate memory offset
		add 	edi, dword [PESection + PESectionHeader.VirtualAddr]

		; How much stuff should be copy?
		mov 	ecx, dword [PESection + PESectionHeader.RawDataSize]
		mov 	ebx, dword [PESection + PESectionHeader.VirtualSize]

		; Size Check
		test 	ecx, ecx
		je 		.ZeroSection0

		; Copy or zero section? :-)
		mov 	eax, dword [PESection + PESectionHeader.Attributes]

		; Code or Data?
		and 	eax, PE_SECTION_CODE
		cmp 	eax, PE_SECTION_CODE
		je 		.CopySection

		mov 	eax, dword [PESection + PESectionHeader.Attributes]
		and 	eax, PE_SECTION_DATA
		cmp 	eax, PE_SECTION_DATA
		je 		.CopySection

		mov 	eax, dword [PESection + PESectionHeader.Attributes]
		and 	eax, PE_SECTION_ZERO
		cmp 	eax, PE_SECTION_ZERO
		jne 	.NextSection

		; If the other fails, its zero
		.ZeroSection:
			xor 	eax, eax
			cld
			rep 	stosb
			jmp 	.NextSection

		.ZeroSection0:
			xor 	eax, eax
			mov 	ecx, ebx
			cld
			rep 	stosb
			jmp 	.NextSection

		.ZeroRemaining:
			xor 	eax, eax
			sub 	ebx, ecx
			mov 	ecx, ebx
			cld
			rep 	stosb
			jmp 	.NextSection

		; Copy data
		.CopySection:
			push 	ecx
			shr 	ecx, 2 	; divide by 4
			add 	ecx, 1 	; In case of remainder
			cld
			rep 	movsd
			pop 	ecx
			cmp 	ecx, ebx
			jb 		.ZeroRemaining

		.NextSection:
			; Save last edi
			mov	dword [dPELastPage], edi

			; Restore locations
			pop 	ecx
			pop 	edi
			pop 	esi

			; Move to next section
			add 	esi, 40
			dec 	ecx
			jnz 	.cLoop

		; Loop Done
		jmp 	.Done

	.NotPE:
		; Damn!
		popad
		mov 	eax, 1
		xor 	ebx, ebx
		ret

	.Done:
		; Page align up
		mov	eax, dword [dPELastPage]
		and 	eax, 0xFFFFF000
		add	eax, 0x1000
		mov	dword [BootDescriptor + MollenOsBootDescriptor.KernelSize], eax

		; Done
		popad
		xor 	eax, eax
		mov 	ebx, dword [dPEEntryPoint]
		ret


; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)


; Etc
dPEEntryPoint 					dd 		0
dPEFileBase						dd 		0
dPELastPage					dd 		0

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

; PE MainHeader (120 Bytes)
PEHeader:
istruc PEMainHeader
	at PEMainHeader.PeMagic, 			dd 	0 		; Must contain PE\0\0
	; File Header Begins Here
	at PEMainHeader.MachineType, 		dw 	0
	at PEMainHeader.NumSections, 		dw 	0
	at PEMainHeader.DateStamp,			dd 	0
	at PEMainHeader.SymbolTablePtr, 	dd 	0
	at PEMainHeader.NumSymbols, 		dd 	0
	at PEMainHeader.OptHeaderSize,	 	dw 	0
	at PEMainHeader.Attributes,			dw 	0 ; 24
	; Optional Header Begins Here 
	at PEMainHeader.PeOptMagic, 		dw 	0
	at PEMainHeader.MajorLinkVersion, 	db 	0
	at PEMainHeader.MinorLinkVersion, 	db 	0
	at PEMainHeader.SizeOfCode, 		dd 	0
	at PEMainHeader.SizeOfInitData, 	dd 	0
	at PEMainHeader.SizeOfUninitData, 	dd 	0 ; 40
	at PEMainHeader.EntryPoint, 		dd 	0
	at PEMainHeader.BaseOfCode, 		dd 	0
	at PEMainHeader.BaseOfData, 		dd 	0
	at PEMainHeader.ImageBase, 			dd 	0
	at PEMainHeader.SectionAlignment, 	dd 	0
	at PEMainHeader.FileAlignment,		dd 	0 ; 64
	at PEMainHeader.MajorOSVersion, 	dw	0
	at PEMainHeader.MinorOSVersion, 	dw 	0
	at PEMainHeader.MajorFileVersion, 	dw 	0
	at PEMainHeader.MinorFileVersion, 	dw 	0
	at PEMainHeader.MajorSubSysVersion, dw 	0
	at PEMainHeader.MinorSubSysVersion, dw 	0
	at PEMainHeader.Win32Version,		dd 	0
	at PEMainHeader.ImageSize,			dd 	0
	at PEMainHeader.HeaderSize, 		dd 	0
	at PEMainHeader.Checksum, 			dd 	0 ; 92
	at PEMainHeader.Subsystem,			dw 	0
	at PEMainHeader.DllAttribs,			dw 	0
	at PEMainHeader.SizeStackReserve, 	dd 	0
	at PEMainHeader.SizeStackCommit, 	dd 	0
	at PEMainHeader.SizeHeapReserve, 	dd 	0
	at PEMainHeader.SizeHeapCommit, 	dd 	0
	at PEMainHeader.LoaderFlags,		dd 	0
	at PEMainHeader.NumRVA,				dd 	0 ; 120
	at PEMainHeader.ExportAddrRva,			dd	0 ; 124
	at PEMainHeader.ExportSize,			dd	0 ; 128
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