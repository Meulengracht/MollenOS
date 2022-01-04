/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86 Architecture Header
 */

#ifndef __VALI_ARCH_X86_H__
#define __VALI_ARCH_X86_H__

#include <os/osdefs.h>
#include <os/context.h>
#include <os/spinlock.h>

#if defined(i386) || defined(__i386__)
#define ARCHITECTURE_NAME               "x86-32"
#define MAX_SUPPORTED_INTERRUPTS        256
#define ALLOCATION_BLOCK_SIZE           0x1000 // 4kb
#elif defined(amd64) || defined(__amd64__)
#define ARCHITECTURE_NAME               "x86-64"
#define ALLOCATION_BLOCK_SIZE           0x100000 // 1 mb
#else
#error "Either i386 or amd64 must be defined for the x86 arch"
#endif
#define MAX_SUPPORTED_INTERRUPTS        256

/* MemorySpace (Data) Definitions
 * Definitions, bit definitions and magic constants for address spaces */
#define MEMORY_SPACE_CR3                0
#define MEMORY_SPACE_DIRECTORY          1
#define MEMORY_SPACE_IOMAP              2

#ifndef GDT_IOMAP_SIZE
#define GDT_IOMAP_SIZE                  ((0xFFFF / 8) + 1)
#endif

/* Threading (Data) Definitions
 * Definitions, bit definitions and magic constants for threads */
#define THREAD_DATA_FLAGS               0
#define THREAD_DATA_MATHBUFFER          1

// Data Flags for threading
#define X86_THREAD_USEDFPU              0x1

#if defined(i386) || defined(__i386__)
/**
 * Architecture Memory Layout
 * This gives you an idea how memory layout is on the x86-32 platform in MollenOS 
 * 0x0               =>             0x100000   (Various uses, reserved primarily for bios things)
 * 0x100000          =>             0x200000   (Kernel identity mapping 1 mb)
 * 0x200000          =>             0x10200000 (Global Access Memory 256 mb)
 * 0x10200000        =>             0xF0000000 (Application Memory Space 3.5gb)
 * 0xF0000000        =>             0xFF000000 (Thread specific storage 240 mb)
 * 0xFF000000        =>             0xFFFFFFFF (Thread specific stack 16mb)
 */
#define MEMORY_LOCATION_KERNEL              0x100000UL   // Kernel Image Space: 1024 kB
#define MEMORY_LOCATION_KERNEL_END          0x10000000UL
#define MEMORY_LOCATION_SHARED_START        0x10000000UL
#define MEMORY_LOCATION_SHARED_END          0x20000000UL
#define MEMORY_SEGMENT_RING0_LIMIT          0xFFFFFFFFUL

#define MEMORY_LOCATION_RING3_CODE          0x20000000UL // Base for ring3 code
#define MEMORY_LOCATION_RING3_CODE_END      0x30000000UL
#define MEMORY_LOCATION_RING3_HEAP          0x30000000UL // Base for ring3 heap
#define MEMORY_LOCATION_RING3_HEAP_END      0xF0000000UL

#define MEMORY_LOCATION_RING3_THREAD_START  0xFF000000UL
#define MEMORY_LOCATION_RING3_THREAD_END    0xFFFFFFFFUL
#define MEMORY_SEGMENT_RING3_LIMIT          0xFFFFFFFFUL

#define MEMORY_SEGMENT_EXTRA_BASE           MEMORY_LOCATION_RING3_THREAD_START
#define MEMORY_SEGMENT_EXTRA_SIZE           0x00001000UL
#elif defined(amd64) || defined(__amd64__)
/**
 * Architecture Memory Layout
 * This gives you an idea how memory layout is on the x86-64 platform in MollenOS 
 * 0x0                =>          0x100000    (Various uses, reserved primarily for bios things)
 * 0x100000           =>          0x200000    (Kernel identity mapping 1 mb)
 * 0x200000           =>          0x10200000  (Global Access Memory 256 mb)
 * 0x10200000         =>          0xFF000000  (Empty 3.7gb)
 * 0x8000000000       =>          0xFFFFFF0000000000 (Application Memory Space - 260tb)
 * 0xFFFFFFFF00000000 =>          0xFFFFFFFFFFFFFFFF (Thread specific region, 16mb)
 */
#define MEMORY_LOCATION_KERNEL       0x100000ULL    // Kernel Image Space: 1024 kB
#define MEMORY_LOCATION_SHARED_START 0x10000000ULL
#define MEMORY_LOCATION_SHARED_END   0x20000000ULL

// Every gigabyte in page size blocks is 131 Kb
// Every gigabyte in 1mb page blocks is then 512 bytes
#define MEMORY_LOCATION_RING3_CODE          0x8000000000ULL // PML4[1]
#define MEMORY_LOCATION_RING3_CODE_END      0x8100000000ULL // 4gb code space
#define MEMORY_LOCATION_RING3_HEAP          0x8100000000ULL
#define MEMORY_LOCATION_RING3_HEAP_END      0x8200000000ULL // 4gb heap memory space (for testing purposes...)

#define MEMORY_LOCATION_RING3_THREAD_START  0xFFFFFFFF00000000ULL // PML4[511]
#define MEMORY_LOCATION_RING3_THREAD_END    0xFFFFFFFFFFFFFFFFULL

#define MEMORY_SEGMENT_GS_USER_BASE         MEMORY_LOCATION_RING3_THREAD_START
#define MEMORY_SEGMENT_EXTRA_SIZE           0x1000ULL
#else
#error "Either i386 or amd64 must be defined for the x86 arch"
#endif

#define MEMORY_MASK_BIOS  0xFFFFF            // Below 1mb reserved for special things
#define MEMORY_MASK_ISA   0xFFFFFF           // Below 16mb reserved for ISA drivers
#define MEMORY_MASK_2GB   0x7FFFFFFF         // Below 2gb for broken 32bit drivers
#define MEMORY_MASK_32BIT 0xFFFFFFFF         // Below 4gb for 32 bit drivers
#define MEMORY_MASK_64BIT 0xFFFFFFFFFFFFFFFF // 64 bit

// Special addresses must be between 0x11000000 -> 0x11001000
#define MEMORY_LOCATION_SIGNAL_RET 0x110000DE // Signal return address

// Task priorities go from (0x0 => 0x60)
// Software interrupt vectors (0x60 => 0x90)
// Syscall does not set the priority register
#define INTERRUPT_SYSCALL       0x60
#define INTERRUPT_LAPIC         0x61
#define INTERRUPT_LVTERROR      0x62
#define INTERRUPT_SPURIOUS      0x63

// Hardware interrupt vectors (0x80 - 0xE0)
#define INTERRUPT_PHYSICAL_BASE 0x80
#define INTERRUPT_PHYSICAL_END  0xE0

// High priority software interrupts
#define INTERRUPT_SOFTWARE_BASE 0xE0
#define INTERRUPT_SOFTWARE_END  0xF0

#endif // !__VALI_ARCH_X86_H__
