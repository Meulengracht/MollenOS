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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; Vali BIOS bootloader (Common, Stage2)
; - Calling convention for writing assembler in this bootloader
;   will be the usual cdecl. So that means arguments are pushed in reverse
;   order on the stack, EAX, ECX and EDX are caller-saved, and rest are calee-saved.
;   Return values are returned in EAX.
;

; 16 Bit Code, Origin at 0x1000
BITS 16
ORG 0x1000

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
%include "systems/paging64.inc"
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

    ; allocate memory for the memory map, this is fixed for now.
    push VBOOT_MEMORY_TYPE_RECLAIM
    push PAGESIZE
    push MEMLOCATION_MEMORY_MAP
    call MemoryAllocateFixed
    add esp, 8
    test eax, eax
    jz .Stage2Failed

    ; allocate memory for the kernel, this is fixed for now so
    ; we might as well just allocate it right out of the box.
    push VBOOT_MEMORY_TYPE_FIRMWARE
    push MEGABYTE
    push KERNEL_BASE_ADDRESS
    call MemoryAllocateFixed
    add esp, 8
    test eax, eax
    jz .Stage2Failed

    ; load kernel image to memory and unpack/relocate it
    TRACE32 szLoadKernelMessage
    push VBOOT_MEMORY_TYPE_BOOT
    push szKernelUtf
    push szKernel
    call LoadFile32
    add esp, 12
    test eax, eax
    jz .Stage2Failed

    ; LoadFile returns 
    ; eax - pointer to file
    ; ecx - number of bytes read
    push eax
    call UnpackFile
    add esp, 4
    test eax, eax
    jz .Stage2Failed

    ; UnpackFile returns
    ; eax - pointer to unpacked file
    ; ecx - size of unpacked file
    mov ebx, eax ; store it in a preserved register

    ; Now load the PE image
    push KERNEL_BASE_ADDRESS
    push eax
    call PELoad
    add esp, 8
    test eax, eax
    jz .Stage2Failed

    ; eax - size
    ; ecx - (lower 32 bits) entry point
    ; edx - (upper 32 bits) entry point
    mov dword [BootHeader + VBoot.KernelBase], KERNEL_BASE_ADDRESS
    mov dword [BootHeader + VBoot.KernelEntry], ecx
    mov dword [BootHeader + VBoot.KernelEntry + 4], edx
    mov dword [BootHeader + VBoot.KernelLength], eax

    ; free unpacked buffer
    push ebx
    call MemoryFree
    add esp, 4

    ; load ramdisk image to memory
    TRACE32 szLoadRdMessage
    push VBOOT_MEMORY_TYPE_FIRMWARE
    push szRamdiskUtf
    push szRamdisk
    call LoadFile32
    add esp, 12
    test eax, eax
    jz .Stage2Failed

    ; LoadFile returns 
    ; eax - pointer to file
    ; ecx - number of bytes read
    mov dword [BootHeader + VBoot.RamdiskBase], eax
    mov dword [BootHeader + VBoot.RamdiskLength], ecx

    ; load bootstrapper to memory
    TRACE32 szLoadPhoenixMessage
    push VBOOT_MEMORY_TYPE_BOOT
    push szPhoenixUtf
    push szPhoenix
    call LoadFile32
    add esp, 12
    test eax, eax
    jz .Stage2Failed

    ; LoadFile returns 
    ; eax - pointer to file
    ; ecx - number of bytes read
    mov dword [BootHeader + VBoot.PhoenixBase], eax
    mov dword [BootHeader + VBoot.PhoenixLength], ecx

    ; Allocate memory for us to relocate the image into
    push VBOOT_MEMORY_TYPE_FIRMWARE
    push KILOBYTE * 512 ; ASSUMPTION: 512K is enough for the bootstrapper
    call MemoryAllocate
    add esp, 8
    test eax, eax
    jz .Stage2Failed

    ; swap the file pointer address and load address
    mov ebx, dword [BootHeader + VBoot.PhoenixBase]
    mov dword [BootHeader + VBoot.PhoenixBase], eax
    
    ; Now load the PE image
    push eax ; load address
    push ebx ; file buffer
    call PELoad
    add esp, 8
    test eax, eax
    jz .Stage2Failed

    ; eax - size
    ; ecx - (lower 32 bits) entry point
    ; edx - (upper 32 bits) entry point
    mov dword [BootHeader + VBoot.PhoenixLength], eax
    mov dword [BootHeader + VBoot.PhoenixEntry], ecx
    mov dword [BootHeader + VBoot.PhoenixEntry + 4], edx

    ; free file buffer
    push ebx
    call MemoryFree
    add esp, 4

    TRACE32 szFinalMessage

    ; switch video mode
%ifdef __OSCONFIG_HAS_VIDEO
    push VesaFinish
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
    push VBOOT_MEMORY_TYPE_FIRMWARE
    push KERNEL_STACK_SIZE
    call MemoryAllocate

    ; store metrics for the stack inside the boot header
    mov dword [BootHeader + VBoot.StackBase], eax
    mov dword [BootHeader + VBoot.StackLength], KERNEL_STACK_SIZE

    ; switch stack to the kernel stack
    add eax, KERNEL_STACK_SIZE
    sub eax, 8
    mov esp, eax

%ifdef __amd64__
    ; Jump into 64 bit mode if available
    call CpuDetect64
    test eax, eax
    jz Skip64BitMode

    ; If eax is set to 1, 
    ; we will enter 64 bit mode instead
    call PagingInitialize64
    test eax, eax
    jz .Stage2Failed

    ; finalize memory map before going 64 bit
    call MemoryFinalizeMap
    jmp CODE64_DESC:LoadKernel64
%else
    jmp Skip64BitMode
%endif

    .Stage2Failed:
        push szFailed
        call SystemsFail32

; This loads the kernel in 32 bit mode. The state when entering the kernel
; at 32 bit mode must be non-paging in segmenetatino mode. This allows the kernel
; access to all physical memory.
; EAX - VBoot Magic
; EBX - VBoot Header
Skip64BitMode:
    ; finalize memory map
    call MemoryFinalizeMap

    mov eax, VBOOT_MAGIC
    mov ebx, BootHeader
    mov ecx, dword [BootHeader + VBoot.KernelEntry]
    jmp ecx ; There is no return from this jump.

; This loads the kernel in 64 bit mode. The state when entering the kernel
; at 64 bit mode must be paging with all physical memory identity mapped.
; RAX - VBoot Magic
; RBX - VBoot Header
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

    ; clear out 64 bit parts of registers as we do not
    ; know the state of them, and we need to use them
    ; for passing state
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx

    mov eax, VBOOT_MAGIC
    mov ebx, BootHeader
    mov ecx, dword [BootHeader + VBoot.KernelEntry]
    jmp rcx ; There is no return from this jump.

; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szStartMessage       db "000000000 [vboot] version: 1.0.0-dev", 0x0D, 0x0A, 0x00
szA20GateMessage     db "000000000 [vboot] enabling a20 gate", 0x0D, 0x0A, 0x00
szGdtMessage         db "000000000 [vboot] installing new gdt", 0x0D, 0x0A, 0x00
szVesaMessage        db "000000000 [vboot] initializing vesa subsystem", 0x0D, 0x0A, 0x00
szFsMessage          db "000000000 [vboot] initializing current filesystem", 0x0D, 0x0A, 0x00
szMemoryMessage      db "000000000 [vboot] initializing memory subsystem", 0x0D, 0x0A, 0x00
szLoadKernelMessage  db "000000000 [vboot] loading kernel", 0x0D, 0x0A, 0x00
szLoadRdMessage      db "000000000 [vboot] loading ramdisk", 0x0D, 0x0A, 0x00
szLoadPhoenixMessage db "000000000 [vboot] loading bootstrapper", 0x0D, 0x0A, 0x00
szFinalMessage       db "000000000 [vboot] finalizing", 0x0D, 0x0A, 0x00
szSuccess            db " [ok]", 0x0D, 0x0A, 0x00
szFailed             db " [err]", 0x0D, 0x0A, 0x00
szNewline            db 0x0D, 0x0A, 0x00

szKernel      db "KERNEL  MOS"
szRamdisk     db "INITRD  MOS"
szPhoenix     db "PHOENIX MOS"
szKernelUtf   db "kernel.mos", 0x00
szRamdiskUtf  db "initrd.mos", 0x00
szPhoenixUtf  db "phoenix.mos", 0x00

; Practical stuff
bDriveNumber db 0
dBaseSector  dd 0

; 2 -> FAT12, 3 -> FAT16, 4 -> FAT32
; 5 -> MFS1
bStage1Type	db 0
