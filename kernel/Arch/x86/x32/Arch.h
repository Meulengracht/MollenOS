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

} Spinlock_t;

/* OS Typedefs */
typedef int OsStatus_t;
typedef unsigned int PhysAddr_t;
typedef unsigned int VirtAddr_t;
typedef unsigned int Addr_t;
typedef signed int SAddr_t;

typedef unsigned int Cpu_t;

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

	/* Memory Space */
	Addr_t Cr3;
	void *PageDirectory;

} x86Thread_t;

/* Architecture Prototypes, you should define 
 * as many as these as possible */
#include "../Cpu.h"
#include "../Video.h"
#include "../Interrupts.h"

/* Components */

/* Threading */
_CRT_EXTERN x86Thread_t *_ThreadInitBoot(void);
_CRT_EXTERN x86Thread_t *_ThreadInitAp(Cpu_t Cpu);
_CRT_EXTERN x86Thread_t *_ThreadInit(Addr_t EntryPoint);
_CRT_EXTERN void _ThreadWakeUpCpu(Cpu_t Cpu);
_CRT_EXTERN void _ThreadYield(void);

/* Port IO */
_CRT_EXTERN uint8_t __CRTDECL inb(uint16_t port);
_CRT_EXTERN uint16_t __CRTDECL inw(uint16_t port);
_CRT_EXTERN uint32_t __CRTDECL inl(uint16_t port);

_CRT_EXTERN void __CRTDECL outb(uint16_t port, uint8_t data);
_CRT_EXTERN void __CRTDECL outw(uint16_t port, uint16_t data);
_CRT_EXTERN void __CRTDECL outl(uint16_t port, uint32_t data);

/* Spinlock */
_CRT_EXTERN void SpinlockReset(Spinlock_t *Spinlock);
_CRT_EXTERN OsStatus_t SpinlockAcquire(Spinlock_t *Spinlock);
_CRT_EXTERN OsStatus_t SpinlockAcquireNoInt(Spinlock_t *Spinlock);
_CRT_EXTERN void SpinlockRelease(Spinlock_t *Spinlock);
_CRT_EXTERN void SpinlockReleaseNoInt(Spinlock_t *Spinlock);

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

/* Physical Memory */
_CRT_EXTERN PhysAddr_t MmPhysicalAllocateBlock(void);
_CRT_EXTERN void MmPhysicalFreeBlock(PhysAddr_t Addr);

/* Virtual Memory */
_CRT_EXTERN void MmVirtualMap(void *PageDirectory, PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, uint32_t Flags);
_CRT_EXTERN void MmVirtualUnmap(void *PageDirectory, VirtAddr_t VirtualAddr);
_CRT_EXTERN PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr);

/* Utils */
_CRT_EXTERN Cpu_t ApicGetCpu(void);
_CRT_EXTERN void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector);
_CRT_EXTERN void Idle(void);

/* Initialises all available timers in system */
_CRT_EXTERN void DevicesInitTimers(void);

/* Initialises all available devices in system */
_CRT_EXTERN void DevicesInit(void *Args);

/* Utils Definitions */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Architecture Memory Layout, this
 * gives you an idea how memory layout
 * is on the x86-32 platform in MollenOS */
#define MEMORY_LOCATION_KERNEL			0x100000 /* Kernel Image Space: 1024 kB */
#define MEMORY_LOCATION_RAMDISK			0x200000 /* RamDisk Image Space: 1024 kB */
#define MEMORY_LOCATION_BITMAP			0x300000 /* Bitmap Space: 12 mB */

#define MEMORY_LOCATION_HEAP			0x1000000
#define MEMORY_LOCATION_HEAP_END		0x4000000

#define MEMORY_LOCATION_VIDEO			0x4000000
#define MEMORY_LOCATION_MODULES			0x5000000

#define MEMORY_LOCATION_SHM				0x9000000
#define MEMORY_LOCATION_SHM_END			0x30000000

#define MEMORY_LOCATION_RESERVED		0xA0000000

/* Architecture Locked Interrupts */
#define INTERRUPT_TIMER					0xF0
#define INTERRUPT_HPET_TIMERS			0xEC
#define INTERRUPT_RTC					0xEC
#define INTERRUPT_PIT					0xEC
#define INTERRUPT_PCI_PIN_3				0xE8
#define INTERRUPT_PCI_PIN_2				0xE4
#define INTERRUPT_PCI_PIN_1				0xE0
#define INTERRUPT_PCI_PIN_0				0xDC

#define INTERRUPT_PS2_PORT2				0xD4
#define INTERRUPT_PS2_PORT1				0xD0

#define INTERRUPT_SPURIOUS7				0x27
#define INTERRUPT_SPURIOUS				0x7F
#define INTERRUPT_SYSCALL				0x80
#define INTERRUPT_YIELD					0x81
#define INTERRUPT_LVTERROR				0x82

/* Free ISA interrupts */
#define INTERRUPT_FREE0					0x3
#define INTERRUPT_FREE1					0x4
#define INTERRUPT_FREE2					0x5


/* Time Stuff */
#define FSEC_PER_NSEC   1000000L
#define NSEC_PER_MSEC   1000L
#define MSEC_PER_SEC    1000L
#define NSEC_PER_SEC    1000000000L
#define FSEC_PER_SEC    1000000000000000LL

#endif // !_MCORE_X86_ARCH_
