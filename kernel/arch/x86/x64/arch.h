/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS x86-64 Architecture Header
 */

#ifndef _MCORE_X64_ARCH_
#define _MCORE_X64_ARCH_

/* Architecture Includes */
#include <os/osdefs.h>
#include <os/context.h>
#include <os/spinlock.h>

/* Architecture Definitions */
#define ARCHITECTURE_NAME		    "x86-64"
#define MAX_SUPPORTED_CPUS			64
#define MAX_SUPPORTED_INTERRUPTS    256

/* AddressSpace (Data) Definitions
 * Definitions, bit definitions and magic constants for address spaces */
#define ASPACE_DATA_CR3             0
#define ASPACE_DATA_PDPOINTER       1

#ifndef GDT_IOMAP_SIZE
#define GDT_IOMAP_SIZE				2048
#endif

/* X86-64 Thread */
typedef struct _x86_Thread {
    Flags_t              Flags;
	uint8_t				 IoMap[GDT_IOMAP_SIZE];
	uintptr_t			*FpuBuffer;
} x86Thread_t;

/* Architecture Memory Layout
 * This gives you an idea how memory layout is on the x86-64 platform in MollenOS 
 * 0x0				=>			0x10000000  (Kernel Memory Space 256 mb)
 * 0x10000000		=>			0xFF000000  (Empty 3.5gb)
 * 0xFF000000		=>			0xFFFFFFFF  (Application Stack Space, 16mb)
 * 0x100000000      =>          0x1FFFFFFFF (Driver Memory Space - 4.0gb)
 * 0x200000000      =>          0xFFFFFFFFFFFFFFFF (Application Memory Space - terabytes)
 */
#define MEMORY_LOCATION_KERNEL				0x100000 	/* Kernel Image Space: 1024 kB */
#define MEMORY_LOCATION_RAMDISK				0x200000 	/* RamDisk Image Space: 1024 kB */
#define MEMORY_LOCATION_BITMAP				0x305000 	/* Bitmap Space: 12 mB (first 0x5000 is used for initial page-dir) */
#define MEMORY_LOCATION_HEAP				0x1000000	/* Heap Space: 50 mB */
#define MEMORY_LOCATION_HEAP_END			0x4000000
#define MEMORY_LOCATION_VIDEO				0x4000000	/* Video Space: 16 mB */
#define MEMORY_LOCATION_RESERVED			0x5000000	/* Driver Space: 190~ mB */
#define MEMORY_LOCATION_KERNEL_END			0x10000000

#define MEMORY_LOCATION_RING3_THREAD_START  0xFF000000
#define MEMORY_LOCATION_RING3_STACK_START   0xFFFE0000
#define MEMORY_LOCATION_RING3_STACK_END       0xFE0000
#define MEMORY_LOCATION_RING3_THREAD_END    0xFFFFFFFF

#define MEMORY_LOCATION_RING3_CODE			0x200000000     // 4gb code space
#define MEMORY_LOCATION_RING3_CODE_END      0x300000000
#define MEMORY_LOCATION_RING3_SHM			0x300000000     // 20gb shared memory space
#define MEMORY_LOCATION_RING3_SHM_END		0x800000000
#define MEMORY_LOCATION_RING3_IOSPACE   	0x800000000     // 16gb io memory space
#define MEMORY_LOCATION_RING3_IOSPACE_END	0x1000000000
#define MEMORY_LOCATION_RING3_HEAP			0x1000000000    // xxgb heap memory space
#define MEMORY_LOCATION_RING3_HEAP_END		0xFFFFFFFFFFFFFFFF

#define MEMORY_SEGMENT_SIGSTACK_BASE        MEMORY_LOCATION_RING3_STACK_START
#define MEMORY_SEGMENT_SIGSTACK_SIZE        0x00010000
#define MEMORY_SEGMENT_EXTRA_BASE           (MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE)
#define MEMORY_SEGMENT_EXTRA_SIZE           0x00010000

/* Special addresses must be between 0x11000000 -> 0x11001000 */
#define MEMORY_LOCATION_SIGNAL_RET			0x110000DE	/* Signal Ret Addr */

/* Architecture Locked Interrupts */
#define INTERRUPT_LAPIC					0xF0

#define INTERRUPT_PHYSICAL_BASE			0x90
#define INTERRUPT_PHYSICAL_END  		0xF0

#define INTERRUPT_SPURIOUS				0x7F
#define INTERRUPT_SYSCALL				0x80
#define INTERRUPT_YIELD					0x81
#define INTERRUPT_LVTERROR				0x82

#endif // !_MCORE_X64_ARCH_
