/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS x86-32 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

#ifndef _GDT_H_
#define _GDT_H_

#include <os/osdefs.h>

/* Customization of TSS and GDT entry limits
 * we allow for 16 gdt descriptors and tss descriptors 
 * they are allocated using static storage since we have
 * no dynamic memory at the time we need them */
#define GDT_MAX_TSS                 128
#define GDT_MAX_DESCRIPTORS         (GDT_MAX_TSS + 8)
#define GDT_IOMAP_SIZE              ((0xFFFF / 8) + 1)

/* 8 Hardcoded system descriptors, we must have a 
 * null descriptor to catch cases where segment has
 * been set to 0, and we need 2 for ring 0, and 4 for ring 3 */
#define GDT_NULL_SEGMENT            0x00
#define GDT_KCODE_SEGMENT           0x08    // Kernel
#define GDT_KDATA_SEGMENT           0x10    // Kernel
#define GDT_UCODE_SEGMENT           0x18    // Applications
#define GDT_UDATA_SEGMENT           0x20    // Applications
#define GDT_PCODE_SEGMENT           0x28    // Drivers
#define GDT_PDATA_SEGMENT           0x30    // Drivers
#define GDT_EXTRA_SEGMENT           0x38    // Shared

/* Gdt type codes, they set the appropriate bits
 * needed for our needs, both for code and data segments
 * where kernel == ring0 and user == ring3 */
#define GDT_GRANULARITY             0xCF
#define GDT_RING0_CODE              0x9A
#define GDT_RING0_DATA              0x92
#define GDT_RING3_CODE              0xFA
#define GDT_RING3_DATA              0xF2
#define GDT_TSS_ENTRY               0xE9

/* The GDT access flags, they define general information
 * about the code / data segment */

/* Data Access */
#define GDT_ACCESS_WRITABLE         0x02
#define GDT_ACCESS_DOWN             0x04    /* Grows down instead of up */

/* Code Access */
#define GDT_ACCESS_READABLE         0x02
#define GDT_ACCESS_CONFORMS         0x04

/* Shared Access */
#define GDT_ACCESS_ACCESSED         0x01
#define GDT_ACCESS_EXECUTABLE       0x08
#define GDT_ACCESS_RESERVED         0x10
#define GDT_ACCESS_PRIV3            (0x20 | 0x40)
#define GDT_ACCESS_PRESENT          0x80

#define GDT_FLAG_32BIT              0x40
#define GDT_FLAG_PAGES              0x80

/* GdtObject
 * The GDT base structure, this is what the hardware
 * will poin to, that describes the memory range where
 * all the gdt-descriptors reside */
PACKED_TYPESTRUCT(GdtObject, {
    uint16_t                Limit;
    uint32_t                Base;
});

/* GdtDescriptor
 * The GDT descriptor structure, this is the actual entry
 * in the gdt table, and keeps information about the ring
 * segment structure. */
PACKED_TYPESTRUCT(GdtDescriptor, {
    uint16_t                LimitLow;    /* Bits  0-15 */
    uint16_t                BaseLow;    /* Bits  0-15 */
    uint8_t                 BaseMid;    /* bits 16-23 */

    /* Access Flags 
     * Bit 0: Accessed bit, is set by cpu when accessed
     * Bit 1: Readable (Code), Writable (Data). 
     * Bit 2: Direction Bit (Data), 0 (Grows up), 1 (Grows down)
     *        Conform Bit (Code), If 1 code in this segment can be executed from an equal or lower privilege level.
     *                            If 0 code in this segment can only be executed from the ring set in privl.
     * Bit 3: Executable, 1 (Code), 0 (Data) 
     * Bit 4: Reserved, must be 1
     * Bit 5-6: Privilege, 0 (Ring 0 - Highest), 3 (Ring 3 - Lowest) 
     * Bit 7: Present Bit, must be 1 */
    uint8_t                Access;

    /* Limit 
     * Bit 0-3: Bits 16-19 of Limit 
     * Bit 4-5: Reserved, 0 
     * Bit 6: 16 Bit Mode (0), 32 Bit Mode (1) 
     * Bit 7: Limit is bytes (0), Limit is page-blocks (1) */
    uint8_t                Flags;
    uint8_t                BaseHigh;    /* Base 24:31 */
});

/* TssDescriptor
 * Describes a task descriptor for the cpu. The cpu then knows
 * which values to fill in when switching between rings. */
PACKED_TYPESTRUCT(TssDescriptor, {
    uint32_t            PreviousTSS;
    uint32_t            Esp0;
    uint32_t            Ss0;
    uint32_t            Esp1;
    uint32_t            Ss1;
    uint32_t            Esp2;
    uint32_t            Ss2;
    uint32_t            Cr3;
    uint32_t            Eip;
    uint32_t            EFlags;
    uint32_t            Eax; 
    uint32_t            Ecx;
    uint32_t            Edx;
    uint32_t            Ebx;
    uint32_t            Esp;
    uint32_t            Ebp;
    uint32_t            Esi;
    uint32_t            Edi;
    uint32_t            Es;
    uint32_t            Cs;
    uint32_t            Ss;
    uint32_t            Ds;
    uint32_t            Fs;
    uint32_t            Gs;
    uint32_t            Ldt;
    uint16_t            Trap;
    uint16_t            IoMapBase;
    uint8_t             IoMap[GDT_IOMAP_SIZE]; // 0 => Granted, 1 => Denied
});

/* GdtInitialize
 * Initialize the gdt table with the 5 default
 * descriptors for kernel/user mode data/code segments */
KERNELAPI void KERNELABI
GdtInitialize(void);

/* GdtInstall
 * This installs the current gdt-object in the
 * gdt register for the calling cpu, use to setup gdt */
KERNELAPI void KERNELABI
GdtInstall(void);

/* TssInitialize
 * Helper for setting up a new task state segment for
 * the given cpu core, this should be done once per
 * core, and it will set default params for the TSS */
KERNELAPI void KERNELABI
TssInitialize(
    _In_ int    PrimaryCore);

/* TssUpdateThreadStack
 * Updates the kernel/interrupt stack for the current
 * cpu tss entry, this should be updated at each task-switch */
KERNELAPI void KERNELABI
TssUpdateThreadStack(
    _In_ UUId_t     Cpu, 
    _In_ uintptr_t  Stack);

/* TssGetBootIoSpace
 * Retrieves the boot-io bitmap space for the kernel threads. */
KERNELAPI uintptr_t KERNELABI
TssGetBootIoSpace(void);

/* TssUpdateIo
 * Updates the io-map for the current runinng task, should
 * be updated each time there is a task-switch to reflect
 * io-privs. Iomap given must be length GDT_IOMAP_SIZE */
KERNELAPI void KERNELABI
TssUpdateIo(
    _In_ UUId_t     Cpu,
    _In_ uint8_t*   IoMap);

/* TssEnableIo
 * Enables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
KERNELAPI void KERNELABI
TssEnableIo(
    _In_ UUId_t     Cpu,
    _In_ uint16_t   Port);

/* TssDisableIo
 * Disables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
KERNELAPI void KERNELABI
TssDisableIo(
    _In_ UUId_t     Cpu,
    _In_ uint16_t   Port);

#endif //!_GDT_H_
