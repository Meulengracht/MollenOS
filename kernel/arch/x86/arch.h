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
#include <revision.h>
#include <crtdefs.h>
#include <multiboot.h>

/* Architecture Definitions */
#define ARCHITECTURE_NAME		"x86-32"
#define STD_VIDEO_MEMORY		0xB8000

/* Architecture typedefs */
typedef volatile unsigned long spinlock_t;

typedef unsigned int physaddr_t;
typedef unsigned int virtaddr_t;
typedef unsigned int addr_t;
typedef signed int saddr_t;

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

} registers_t;

/* Architecture Prototypes, you should define 
 * as many as these as possible */

/* Components */

/* Video */
_CRT_EXTERN void video_init(multiboot_info_t *bootinfo);
_CRT_EXTERN int video_putchar(int character);

/* Spinlock */
_CRT_EXTERN void spinlock_reset(spinlock_t *spinlock);
_CRT_EXTERN int spinlock_acquire(spinlock_t *spinlock);
_CRT_EXTERN void spinlock_release(spinlock_t *spinlock);

/* Memory */
#define PAGE_SIZE 0x1000

/* Physical Memory */
_CRT_EXTERN void physmem_init(multiboot_info_t *bootinfo, uint32_t img_size);
_CRT_EXTERN physaddr_t physmem_alloc_block(void);
_CRT_EXTERN void physmem_free_block(physaddr_t addr);

/* Virtual Memory */
_CRT_EXTERN void virtmem_init(void);
_CRT_EXTERN void virtmem_map(void);
_CRT_EXTERN void virtmem_unmap(void);
_CRT_EXTERN physaddr_t virtmem_getmapping(void);

/* Driver Interface */

#endif // !_MCORE_X86_ARCH_
