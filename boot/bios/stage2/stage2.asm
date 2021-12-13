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
; Vali BIOS bootloader (Common, Stage2)
; - Calling convention for writing assembler in this bootloader
;   will be the usual cdecl. So that means arguments are pushed in reverse
;   order on the stack, EAX, ECX and EDX are caller-saved, and rest are calee-saved.
;   Return values are returned in EAX.
;

; 16 Bit Code, Origin at 0x500
BITS 16
ORG 0x0500

%include "systems/defines.inc"

; *****************************
; REAL ENTRY POINT
; *****************************
jmp LoaderEntry16

; Includes
%include "systems/common16.inc"
%include "systems/memory16.inc"
%include "systems/a20.inc"
%include "systems/gdt.inc"
%include "systems/output.inc"
%include "systems/peloader.inc"

; FileSystem Includes
%include "filesystems/fscommon16.inc"

; *****************************
; Entry Point
; dl = drive number
; dh = stage1 type
; si = partition table entry
; *****************************
LoaderEntry16:
    cli

    xor ax, ax
    mov	ds, ax
    mov	es, ax
    mov	fs, ax
    mov	gs, ax

    mov	ss, ax
    mov	ax, MEMLOCATION_INITIALSTACK
    mov	sp, ax
    sti

    ; save information passed by bootloader (stage1)
    mov byte [bDriveNumber], dl   ; store drivenum
    mov byte [bStage1Type], dh    ; store stage1 type
    mov esi, dword [si + 8]       ; load base-sector from partition entry
    mov dword [dBaseSector], esi  ; store it for our ReadSectors function

    ; Initialize the output system (early)
    call VideoInitializeConsole16
    TRACE16 szStartMessage

    TRACE16 szA20GateMessage
    call A20Enable16       ; enable a20 gate if it not already
    test ax, ax
    jnz Continue_Part1
    call SystemsFail

Continue_Part1:
    TRACE16 szGdtMessage
    call GdtInstall     ; install gdt table with 16, 32 and 64 bit entries

%ifdef __OSCONFIG_HAS_VIDEO
    ; Initialize vesa memory structures and find the mode we want
    TRACE16 szVesaMessage
    call VesaInitialize16
%endif

    ; Initialize filesystem subsystem
    TRACE16 szFsMessage
    xor ax, ax
    mov al, [bDriveNumber]
    push ax
    mov al, [bStage1Type]
    push ax
    call FileSystemInitialize16
    add sp, 4

    ; load memory map to known address so we can parse it and
    ; prepare the vboot memory map
    TRACE16 szMemoryMessage
    call MemoryInitialize16

    ; enable 32 bit mode
    mov	eax, cr0
    or  eax, 1
    mov	cr0, eax
    jmp CODE_DESC:LoaderEntry32

ALIGN 32
BITS  32
%include "systems/common32.inc"
%include "systems/memory32.inc"
%include "systems/cpu.inc"
%include "systems/lz.inc"
%include "filesystems/fscommon32.inc"

LoaderEntry32:
    cli
    xor eax, eax
    mov ax, DATA_DESC
    mov ds, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov es, ax
    mov esp, MEMLOCATION_INITIALSTACK

    ; allocate memory for the kernel, this is fixed for now so
    ; we might as well just allocate it right out of the box.
    xchg bx, bx
    push KERNEL_BASE_ADDRESS
    push MEGABYTE
    call MemoryAllocateFixed
    add esp, 8
    cmp eax, 0
    je .Stage2Failed

    ; load kernel image to memory and unpack/relocate it
    TRACE32 szLoadKernelMessage
    push szKernelUtf
    push szKernel
    call LoadFile32
    add esp, 8
    cmp eax, 0
    je .Stage2Failed

    ; LoadFile returns 
    ; eax - pointer to file
    ; ecx - number of bytes read

    push eax 
    call UnpackFile
    add esp, 4
    cmp eax, 0
    je .Stage2Failed

    ; UnpackFile returns
    ; eax - pointer to unpacked file
    ; ecx - size of unpacked file
    mov ebx, eax ; store it in a preserved register

    ; Now load the PE image
    push KERNEL_BASE_ADDRESS
    push eax
    call PELoad
    add esp, 8
    cmp eax, 0
    je .Stage2Failed

    ; eax - size
    ; ecx - entry point
    mov dword [BootHeader + VBoot.KernelBase], ebx
    mov dword [BootHeader + VBoot.KernelLength], eax
    mov dword [dKernelEntry], ecx

    ; free unpacked buffer
    push ebx
    call MemoryFreeFirmware
    add esp, 4

    ; load ramdisk image to memory
    TRACE32 szLoadRdMessage
    push szRamdiskUtf
    push szRamdisk
    call LoadFile32
    add esp, 8
    cmp eax, 0
    je .Stage2Failed

    ; LoadFile returns 
    ; eax - pointer to file
    ; ecx - number of bytes read

    push eax 
    call UnpackFile
    add esp, 4
    cmp eax, 0
    je .Stage2Failed

    ; UnpackFile returns
    ; eax - pointer to unpacked file
    ; ecx - size of unpacked file
    mov dword [BootHeader + VBoot.RamdiskBase], eax
    mov dword [BootHeader + VBoot.RamdiskLength], ecx

    TRACE32 szFinalMessage

    ; switch video mode
%ifdef __OSCONFIG_HAS_VIDEO
    mov eax, VesaFinish
    push eax
    call BiosCallWrapper
    add esp, 4
%endif

    ; At this point we no longer need 16 bit mode calls, so
    ; we can mask all irq from this point in the PIC to avoid
    ; anything from this point even tho interrupts are disabled atm.
    mov al, 0xff
    out 0xa1, al
    out 0x21, al

    ; allocate memory for the kernel stack
    push KERNEL_STACK_SIZE
    call MemoryAllocateFirmware
    mov esp, eax

    mov dword [BootHeader + VBoot.StackBase], eax
    mov dword [BootHeader + VBoot.StackLength], KERNEL_STACK_SIZE

%ifdef __amd64__
    ; go into 64 bit mode if possible
    call	CpuDetect64
    cmp     eax, 1
    jne     Skip64BitMode

    ; If eax is set to 1, 
    ; we will enter 64 bit mode instead
    call    CpuSetupLongMode
    jmp     CODE64_DESC:LoadKernel64
%endif

    .Stage2Failed:
        push szFailed
        call SystemsFail32

Skip64BitMode:
    ; Setup Registers
    xor esi, esi
    xor edi, edi
    mov ecx, dword [dKernelEntry]
    mov eax, VBOOT_MAGIC
    mov ebx, BootHeader
    
    ; Jump to kernel (Entry Point in ECX)
    jmp ecx
    cli
    hlt

ALIGN 64
BITS  64
LoadKernel64:
    xor rax, rax
    mov ax, DATA64_DESC
    mov ds, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov es, ax

    ; clear out upper 32 bits of stack to make sure
    ; no intended bits are left up there
    mov eax, esp
    xor rsp, rsp
    mov rsp, rax

    ; Setup Registers
    xor rsi, rsi
    xor rdi, rdi
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    mov ecx, dword [dKernelEntry]
    mov eax, VBOOT_MAGIC
    mov ebx, BootHeader

    ; Jump to kernel (Entry Point in ECX)
    jmp rcx
    cli
    hlt

; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szStartMessage      db "000000000 [vboot] version: 1.0.0-dev", 0x0D, 0x0A, 0x00
szA20GateMessage    db "000000000 [vboot] enabling a20 gate", 0x0D, 0x0A, 0x00
szGdtMessage        db "000000000 [vboot] installing new gdt", 0x0D, 0x0A, 0x00
szVesaMessage       db "000000000 [vboot] initializing vesa subsystem", 0x0D, 0x0A, 0x00
szFsMessage         db "000000000 [vboot] initializing current filesystem", 0x0D, 0x0A, 0x00
szMemoryMessage     db "000000000 [vboot] initializing memory subsystem", 0x0D, 0x0A, 0x00
szLoadKernelMessage db "000000000 [vboot] loading kernel image", 0x0D, 0x0A, 0x00
szLoadRdMessage     db "000000000 [vboot] loading ramdisk", 0x0D, 0x0A, 0x00
szFinalMessage      db "000000000 [vboot] finalizing", 0x0D, 0x0A, 0x00
szSuccess           db " [ok]", 0x0D, 0x0A, 0x00
szFailed            db " [err]", 0x0D, 0x0A, 0x00
szNewline           db 0x0D, 0x0A, 0x00

szKernel      db "MCORE   MOS"
szRamdisk     db "INITRD  MOS"
szKernelUtf   db "syskrnl.mos", 0x0
szRamdiskUtf  db "initrd.mos", 0x0

; Practical stuff
bDriveNumber db 0
dBaseSector  dd 0
dKernelEntry dd	0

; 2 -> FAT12, 3 -> FAT16, 4 -> FAT32
; 5 -> MFS1
bStage1Type	db 0
