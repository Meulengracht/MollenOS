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
#define STD_VIDEO_MEMORY		0xB8000

/* Architecture typedefs */
typedef uint32_t IntStatus_t;
typedef int(*IrqHandler_t)(void*);

/* Irq Return Codes */
#define X86_IRQ_NOT_HANDLED			(int)0x0
#define X86_IRQ_HANDLED				(int)0x1

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
typedef unsigned int TId_t;
typedef void(*ThreadEntry_t)(void*);

/* OsStatus Return Codes */
#define OS_STATUS_OK			0
#define OS_STATUS_FAIL			-1

/* X86-32 Interrupt Entry */
typedef struct _IrqEntry
{
	/* The Irq function */
	IrqHandler_t Function;

	/* Associated Data */
	void *Data;

	/* Whether it's installed or not */
	uint32_t Installed;

	/* Pin */
	uint32_t Gsi;

} IrqEntry_t;

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
typedef struct _Thread
{
	/* Name */
	char *Name;

	/* Information */
	uint32_t Flags;
	uint32_t TimeSlice;
	int32_t Priority;
	Addr_t *SleepResource;

	/* Ids */
	TId_t ThreadId;
	TId_t ParentId;
	Cpu_t CpuId;

	/* Context(s) */
	Registers_t *Context;
	Registers_t *UserContext;

	/* Math Buffer */
	Addr_t *FpuBuffer;

	/* Memory Space */
	Addr_t Cr3;
	void *PageDirectory;

	/* Entry point */
	ThreadEntry_t Func;
	void *Args;

} Thread_t;

/* X86-32 Threading Flags */
#define THREADING_USERMODE		0x1
#define THREADING_CPUBOUND		0x2
#define THREADING_SYSTEMTHREAD	0x4

/* Architecture Prototypes, you should define 
 * as many as these as possible */

/* Components */

/* Port IO */
_CRT_EXTERN uint8_t __CRTDECL inb(uint16_t port);
_CRT_EXTERN uint16_t __CRTDECL inw(uint16_t port);
_CRT_EXTERN uint32_t __CRTDECL inl(uint16_t port);

_CRT_EXTERN void __CRTDECL outb(uint16_t port, uint8_t data);
_CRT_EXTERN void __CRTDECL outw(uint16_t port, uint16_t data);
_CRT_EXTERN void __CRTDECL outl(uint16_t port, uint32_t data);

/* Video */
_CRT_EXTERN OsStatus_t VideoInit(void *BootInfo);
_CRT_EXTERN int VideoPutChar(int Character);

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
_CRT_EXTERN OsStatus_t MmPhyiscalInit(void *BootInfo, uint32_t KernelSize);
_CRT_EXTERN PhysAddr_t MmPhysicalAllocateBlock(void);
_CRT_EXTERN void MmPhysicalFreeBlock(PhysAddr_t Addr);

/* Virtual Memory */
_CRT_EXTERN void MmVirtualInit(void);
_CRT_EXTERN void MmVirtualMap(void *PageDirectory, PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, uint32_t Flags);
_CRT_EXTERN void MmVirtualUnmap(void *PageDirectory, VirtAddr_t VirtualAddr);
_CRT_EXTERN PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr);

/* Interrupt Interface */
_CRT_EXTERN void InterruptInit(void);
_CRT_EXTERN OsStatus_t InterruptAllocateISA(uint32_t Irq);
_CRT_EXTERN void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
_CRT_EXTERN void InterruptInstallIdtOnly(uint32_t Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
_CRT_EXTERN void InterruptInstallShared(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
_CRT_EXTERN uint32_t InterruptAllocatePCI(uint32_t Irqs[], uint32_t Count);

_CRT_EXTERN IntStatus_t InterruptDisable(void);
_CRT_EXTERN IntStatus_t InterruptEnable(void);
_CRT_EXTERN IntStatus_t InterruptSaveState(void);
_CRT_EXTERN IntStatus_t InterruptRestoreState(IntStatus_t state);

/* Utils */
_CRT_EXTERN Cpu_t ApicGetCpu(void);
_CRT_EXTERN void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector);
_CRT_EXTERN void Idle(void);

/* Threading - Flags -> Look above for flags  */
_CRT_EXTERN TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags);
_CRT_EXTERN void *ThreadingEnterSleep(void);
_CRT_EXTERN void threading_kill_thread(TId_t thread_id);
_CRT_EXTERN int ThreadingYield(void *Args);
_CRT_EXTERN TId_t ThreadingGetCurrentThreadId(void);
_CRT_EXTERN Thread_t *ThreadingGetCurrentThread(Cpu_t cpu);

/* Driver Interface */
_CRT_EXTERN void DriverManagerInit(void *Args);

/* Utils Definitions */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Architecture Memory Layout, this
 * gives you an idea how memory layout
 * is on the x86-32 platform in MollenOS */
#define MEMORY_LOCATION_KERNEL			0x100000 /* Kernel Image Space: 256 kB */
#define MEMORY_LOCATION_BITMAP			0x140000

#define MEMORY_LOCATION_HEAP			0x400000
#define MEMORY_LOCATION_HEAP_END		0x4000000

#define MEMORY_LOCATION_VIDEO			0x4000000

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
