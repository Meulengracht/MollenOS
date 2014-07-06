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
typedef volatile unsigned long spinlock_t;

typedef uint32_t interrupt_status_t;
typedef void(*irq_handler_t)(void*);

typedef unsigned int physaddr_t;
typedef unsigned int virtaddr_t;
typedef unsigned int addr_t;
typedef signed int saddr_t;

typedef unsigned int cpu_t;
typedef unsigned int tid_t;
typedef void(*thread_entry)(void*);

/* X86-32 Interrupt Entry */
typedef struct irq_entry
{
	irq_handler_t function;
	void *data;
} irq_entry_t;

/* X86-32 Context */
typedef struct registers
{
	/* General Registers */
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	
	/* Segments */
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	/* Stuff */
	uint32_t irq;
	uint32_t error_code;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;

	/* User Stuff */
	uint32_t user_esp;
	uint32_t user_ss;
	uint32_t user_arg;

} registers_t;

/* X86-32 Thread */
typedef struct _thread
{
	/* Name */
	char *name;

	/* Information */
	uint32_t flags;
	uint32_t time_slice;
	int32_t priority;

	/* Ids */
	tid_t thread_id;
	tid_t parent_id;
	cpu_t cpu_id;

	/* Context(s) */
	registers_t *context;
	registers_t *user_context;

	/* Math Buffer */
	addr_t *fpu_buffer;

	/* Memory Space */
	addr_t cr3;
	void *page_dir;

	/* Entry point */
	thread_entry func;
	void *args;

} thread_t;

/* X86-32 Threading Flags */
#define THREADING_USERMODE		0x1
#define THREADING_CPUBOUND		0x2
#define THREADING_SYSTEMTHREAD	0x4

/* Architecture Prototypes, you should define 
 * as many as these as possible */

/* Components */

/* Port IO */
_CRT_EXTERN uint8_t inb(uint16_t port);
_CRT_EXTERN uint16_t inw(uint16_t port);
_CRT_EXTERN uint32_t inl(uint16_t port);

_CRT_EXTERN void outb(uint16_t port, uint8_t data);
_CRT_EXTERN void outw(uint16_t port, uint16_t data);
_CRT_EXTERN void outl(uint16_t port, uint32_t data);

/* Video */
_CRT_EXTERN void video_init(void *bootinfo);
_CRT_EXTERN int video_putchar(int character);

/* Spinlock */
_CRT_EXTERN void spinlock_reset(spinlock_t *spinlock);
_CRT_EXTERN int spinlock_acquire(spinlock_t *spinlock);
_CRT_EXTERN void spinlock_release(spinlock_t *spinlock);

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
_CRT_EXTERN void physmem_init(void *bootinfo, uint32_t img_size);
_CRT_EXTERN physaddr_t physmem_alloc_block(void);
_CRT_EXTERN void physmem_free_block(physaddr_t addr);

/* Virtual Memory */
_CRT_EXTERN void virtmem_init(void);
_CRT_EXTERN void memory_map(void *page_dir, physaddr_t phys, virtaddr_t virt, uint32_t flags);
_CRT_EXTERN void memory_unmap(void *page_dir, virtaddr_t virt);
_CRT_EXTERN physaddr_t memory_getmap(void *page_dir, virtaddr_t virt);

/* Interrupt Interface */
_CRT_EXTERN void interrupt_init(void);
_CRT_EXTERN void interrupt_install(uint32_t irq, uint32_t idt_entry, irq_handler_t callback, void *args);
_CRT_EXTERN void interrupt_install_broadcast(uint32_t irq, uint32_t idt_entry, irq_handler_t callback, void *args);
_CRT_EXTERN void interrupt_install_soft(uint32_t idt_entry, irq_handler_t callback, void *args);

_CRT_EXTERN interrupt_status_t interrupt_disable(void);
_CRT_EXTERN interrupt_status_t interrupt_enable(void);
_CRT_EXTERN interrupt_status_t interrupt_get_state(void);
_CRT_EXTERN interrupt_status_t interrupt_set_state(interrupt_status_t state);

/* Utils */
_CRT_EXTERN cpu_t get_cpu(void);
_CRT_EXTERN void stall_ms(size_t ms);
_CRT_EXTERN void clock_stall(time_t ms);
_CRT_EXTERN void idle(void);

/* Debug */
_CRT_EXTERN char *get_instructions_at_mem(addr_t address);

/* Threading - Flags -> Look above for flags  */
_CRT_EXTERN tid_t threading_create_thread(char *name, thread_entry function, void *args, int flags);
_CRT_EXTERN void threading_kill_thread(tid_t thread_id);
_CRT_EXTERN void threading_yield(void *args);

/* Driver Interface */
_CRT_EXTERN void drivers_init(void);



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
#define INTERRUPT_RTC					0xEC
#define INTERRUPT_USB_OHCI				0xE8

#define INTERRUPT_SPURIOUS				0x7F
#define INTERRUPT_SYSCALL				0x80
#define INTERRUPT_YIELD					0x81

#endif // !_MCORE_X86_ARCH_
