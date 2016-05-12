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
#include <stdint.h>
#include <crtdefs.h>

/* Architecture Definitions */
#define ARCHITECTURE_NAME		"x86-32"

/* Architecture Typedefs */
typedef uint32_t IntStatus_t;
typedef unsigned int PhysAddr_t;
typedef unsigned int VirtAddr_t;
typedef unsigned int Addr_t;
typedef signed int SAddr_t;
typedef unsigned int Cpu_t;

/* Diagnostics */
//#define X86_ACPICA_DIAGNOSE
//#define X86_HPET_DIAGNOSE
//#define X86_PCI_DIAGNOSE
//#define _OHCI_DIAGNOSTICS_

typedef struct _x86_Spinlock
{
	/* The lock itself */
	volatile unsigned Lock;

	/* The INTR state */
	IntStatus_t IntrState;

	/* Lock Holder */
	uint32_t Owner;

} Spinlock_t;

/* OsStatus Return Codes */
#define OS_STATUS_OK			0
#define OS_STATUS_FAIL			-1

/* X86-32 Context */
typedef struct _Registers
{
	/* General Registers */
	uint32_t Edi;
	uint32_t Esi;
	uint32_t Ebp;
	uint32_t Esp;
	uint32_t Ebx;
	uint32_t Edx;
	uint32_t Ecx;
	uint32_t Eax;
	
	/* Segments */
	uint32_t Gs;
	uint32_t Fs;
	uint32_t Es;
	uint32_t Ds;

	/* Stuff */
	uint32_t Irq;
	uint32_t ErrorCode;
	uint32_t Eip;
	uint32_t Cs;
	uint32_t Eflags;

	/* User Stuff */
	uint32_t UserEsp;
	uint32_t UserSs;
	uint32_t UserArg;

} Registers_t;

/* X86-32 Address Space */
typedef struct _AddressSpace
{
	/* Flags */
	uint32_t Flags;

	/* Physical Address of 
	 * Paging Structure */
	Addr_t Cr3;

	/* The Page Directory */
	void *PageDirectory;

} AddressSpace_t;

/* X86-32 Thread */
typedef struct _x86_Thread
{
	/* Flags */
	uint32_t Flags;

	/* Context(s) */
	Registers_t *Context;
	Registers_t *UserContext;

	/* Math Buffer */
	Addr_t *FpuBuffer;

} x86Thread_t;

/* Architecture Prototypes, you should define 
 * as many as these as possible */
#include "../Cpu.h"
#include "../Video.h"
#include "../Interrupts.h"

/* Port IO */
_CRT_EXTERN uint8_t __CRTDECL inb(uint16_t port);
_CRT_EXTERN uint16_t __CRTDECL inw(uint16_t port);
_CRT_EXTERN uint32_t __CRTDECL inl(uint16_t port);

_CRT_EXTERN void __CRTDECL outb(uint16_t port, uint8_t data);
_CRT_EXTERN void __CRTDECL outw(uint16_t port, uint16_t data);
_CRT_EXTERN void __CRTDECL outl(uint16_t port, uint32_t data);

/* Initialises all available timers in system */
_CRT_EXTERN void DevicesInitTimers(void);

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

/* Utils */
_CRT_EXTERN Cpu_t ApicGetCpu(void);
_CRT_EXTERN void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector);
_CRT_EXTERN void Idle(void);
_CRT_EXPORT void kernel_panic(const char *str);

/* Utils Definitions */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define DIVUP(a, b) ((a / b) + (((a % b) > 0) ? 1 : 0))
#define INCLIMIT(i, limit) i++; if (i == limit) i = 0;
#define ALIGN(Val, Alignment, Roundup) ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)

/* Utils Functions */

/* Get first available bit */
static int FirstSetBit(size_t Value)
{
	/* Vars */
	int bCount = 0;
	size_t Cc = Value;

	/* Keep bit-shifting */
	for (; Cc != 0;) {
		bCount++;
		Cc >>= 1;
	}

	/* Done */
	return bCount;
}

/* Architecture Memory Layout, this
 * gives you an idea how memory layout
 * is on the x86-32 platform in MollenOS */
#define MEMORY_LOCATION_KERNEL			0x100000 /* Kernel Image Space: 1024 kB */
#define MEMORY_LOCATION_RAMDISK			0x200000 /* RamDisk Image Space: 1024 kB */
#define MEMORY_LOCATION_BITMAP			0x300000 /* Bitmap Space: 12 mB */

#define MEMORY_LOCATION_HEAP			0x1000000 /* Heap Space: 64 mB */
#define MEMORY_LOCATION_HEAP_END		0x4000000

#define MEMORY_LOCATION_VIDEO			0x4000000 /* Video Space: 16 mB */
#define MEMORY_LOCATION_MODULES			0x5000000 /* Module Space: 190~ mB */

#define MEMORY_LOCATION_RESERVED		0x10000000 /* Device Space: 1.256 mB */

#define MEMORY_LOCATION_USER_ARGS		0x60000000 /* Arg Space: 4 kB */
#define MEMORY_LOCATION_USER			0x60010000 /* Image Space: 256~ mB */
#define MEMORY_LOCATION_USER_HEAP		0x70000000 /* Heap Space: 2048 mB */
#define MEMORY_LOCATION_USER_SHM		0xF0000000 /* Shared Memory: 252 mB */
#define MEMORY_LOCATION_USER_SHM_END	0xFFC00000 /* Shared Memory: 252 mB */
#define MEMORY_LOCATION_USER_GUARD		0xFFC00000 /* Stack End */
#define MEMORY_LOCATION_USER_STACK		0xFFFFFFF0 /* Stack Space: 4 mB */

/* Architecture Locked Interrupts */
#define INTERRUPT_LAPIC					0xF0
#define INTERRUPT_DEVICE_BASE			0xC0
#define INTERRUPT_TIMER_BASE			0xA0		/* Allow up to 32 ints */

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
