/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Architecture Header
*/

#ifndef _MCORE_X86_ARCH_
#define _MCORE_X86_ARCH_

/* Architecture Includes */
#include <os/osdefs.h>
#include <os/context.h>
#include <os/spinlock.h>

/* Architecture Definitions */
#define ARCHITECTURE_NAME		"x86-32"

/* X86-32 Address Space */
#define ADDRESSSPACE_MEMBERS		Addr_t Cr3; void *PageDirectory;

#include "Gdt.h"

/* X86-32 Thread */
typedef struct _x86_Thread {
	Flags_t				Flags;
	uint8_t				IoMap[GDT_IOMAP_SIZE];
	Context_t			*Context;
	Context_t			*UserContext;
	Addr_t				*FpuBuffer;
} x86Thread_t;

/* Architecture Prototypes, you should define 
 * as many as these as possible */
#include "../Cpu.h"
#include "../Video.h"

/* Interrupt stuff */
#define NUM_ISA_INTERRUPTS			16

/* Port IO */
__EXTERN uint8_t __CRTDECL inb(uint16_t port);
__EXTERN uint16_t __CRTDECL inw(uint16_t port);
__EXTERN uint32_t __CRTDECL inl(uint16_t port);

__EXTERN void __CRTDECL outb(uint16_t port, uint8_t data);
__EXTERN void __CRTDECL outw(uint16_t port, uint16_t data);
__EXTERN void __CRTDECL outl(uint16_t port, uint32_t data);

/* Memory */
#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

#ifndef PAGE_MASK
#define PAGE_MASK 0xFFFFF000
#endif

#ifndef ATTRIBUTE_MASK
#define ATTRIBUTE_MASK 0x00000FFF
#endif

__EXTERN void ApicSendIpi(UUId_t CpuTarget, uint32_t Vector);
__EXTERN void kernel_panic(const char *str);

/* Architecture Memory Layout, this
 * gives you an idea how memory layout
 * is on the x86-32 platform in MollenOS 
 * 0x0				=>			0x10000000 (Kernel Memory Space 256 mb)
 * 0x10000000		=>			0xB0000000 (Application Memory Space 2.5gb) 
 * 0xB0000000		=>			0xF0000000 (Driver Io Memory Space, 1gb)
 * 0xF0000000		=>			0xFF000000 (Empty)
 * 0xFF000000		=>			0xFFFFFFFF (Application Stack Space, 16mb) 
 */
#define MEMORY_LOCATION_KERNEL				0x100000 	/* Kernel Image Space: 1024 kB */
#define MEMORY_LOCATION_RAMDISK				0x200000 	/* RamDisk Image Space: 1024 kB */
#define MEMORY_LOCATION_BITMAP				0x300000 	/* Bitmap Space: 12 mB */
#define MEMORY_LOCATION_HEAP				0x1000000	/* Heap Space: 64 mB */
#define MEMORY_LOCATION_HEAP_END			0x4000000
#define MEMORY_LOCATION_VIDEO				0x4000000	/* Video Space: 16 mB */
#define MEMORY_LOCATION_RESERVED			0x5000000	/* Driver Space: 190~ mB */
#define MEMORY_LOCATION_KERNEL_END			0x10000000

#define MEMORY_SEGMENT_KERNEL_CODE_LIMIT	MEMORY_LOCATION_RAMDISK
#define MEMORY_SEGMENT_KERNEL_DATA_LIMIT	0xFFFFFFFF

#define MEMORY_LOCATION_RING3_ARGS			0x1F000000	/* Base for ring3 arguments */
#define MEMORY_LOCATION_RING3_CODE			0x20000000	/* Base for ring3 code */
#define MEMORY_LOCATION_RING3_HEAP			0x30000000	/* Base for ring3 heap */
#define MEMORY_LOCATION_RING3_SHM			0xA0000000	/* Base for ring3 shm */
#define MEMORY_LOCATION_RING3_IOSPACE		0xB0000000	/* Base for ring3 io-space (1gb) */
#define MEMORY_LOCATION_RING3_IOSPACE_END	0xF0000000

#define MEMORY_SEGMNET_RING3_CODE_LIMIT		MEMORY_LOCATION_RING3_HEAP
#define MEMORY_SEGMENT_RING3_DATA_LIMIT		0xFFFFFFFF

#define MEMORY_SEGMENT_STACK_BASE			0xFFFFFFFF	/* RING3 Stack Initial */
#define MEMORY_SEGMENT_STACK_LIMIT			  0xFFFFFF	/* RING3 Stack Space: 16 mB */
#define MEMORY_LOCATION_STACK_END			(MEMORY_SEGMENT_STACK_BASE - MEMORY_SEGMENT_STACK_LIMIT)

/* Special addresses must be between 0x11000000 -> 0x11001000 */
#define MEMORY_LOCATION_SIGNAL_RET			0x110000DE	/* Signal Ret Addr */

/* Architecture Locked Interrupts */
#define INTERRUPT_LAPIC					0xF0
#define INTERRUPT_DEVICE_BASE			0xA0

#define INTERRUPT_SPURIOUS7				0x27
#define INTERRUPT_SPURIOUS				0x7F
#define INTERRUPT_SYSCALL				0x80
#define INTERRUPT_YIELD					0x81
#define INTERRUPT_LVTERROR				0x82
#define INTERRUPT_ACPIBASE				0x90

/* Free ISA interrupts */
#define INTERRUPT_FREE0					0x3
#define INTERRUPT_FREE1					0x4
#define INTERRUPT_FREE2					0x5

#endif // !_MCORE_X86_ARCH_
