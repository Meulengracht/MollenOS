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
* MollenOS Heap Manager
* Basic Heap Manager
* No contraction for now, for simplicity.
*/

#ifndef _MCORE_HEAP_H_
#define _MCORE_HEAP_H_

/* Includes */
#include <stdint.h>
#include <crtdefs.h>

/***************************
Heap Management
***************************/
#define KERNEL_HEAP_STRUCT	0x10000000
#define KERNEL_HEAP_START	0x10200000
#define KERNEL_HEAP_END		0x20000000
#define KERNEL_HEAP_SIZE	(KERNEL_HEAP_END - KERNEL_HEAP_START)

#define KERNEL_HEAP_INC		0x10000
#define HEAP_LARGE_BLOCK	0x1000

#define KERNEL_HEAP_SINC	0x4000
#define HEAP_SMALL_BLOCK	0x20

/* 12 bytes overhead */
typedef struct hmm_header
{
	/* Address */
	uint32_t addr;

	/* Link */
	struct hmmheader_t *next;

	/* Status */
	uint32_t allocated : 1;

	/* Length */
	uint32_t length : 31;

} hmmheader_t;

#define NODE_4KB_FLAG	0x1
#define NODE_IS_CUSTOM	0x2
#define AddrIsAligned(x) ((x & 0xFFF) == 0)

/* A node descriptor, contains a 
 * memory range */
typedef struct hmm_node
{
	/* Start - End Address */
	uint32_t addr_start;
	uint32_t addr_end;

	/* Node Flags */
	uint32_t flags;

	/* Stats */
	uint32_t free_header_count;

	/* Spinlock */
	uint32_t plock;

	/* Next in Linked List */
	struct hmmnode_t *next;

	/* Head of Header List */
	struct hmmheader_t *head;

} hmmnode_t;

//4 bytes
typedef struct hmm_area
{
	/* Head of Node List */
	struct hmmnode_t *head;

} hmmarea_t;

//Init
extern void heap_init(void);
extern uint32_t heap_get_count(void);
extern void heap_print_stats(void);

//kMalloc Align and phys return
extern void *kmalloc_ap(uint32_t l, uint32_t *p);

//kMalloc return phys
extern void *kmalloc_p(uint32_t l, uint32_t *p);

//kMalloc align
extern void *kmalloc_a(uint32_t l);

//kMalloc
extern void *kmalloc(uint32_t l);

//kFree
extern void kfree(void *p);

//krealloc / reallocate
extern void *kcalloc(size_t nmemb, size_t size);
extern void *krealloc(void *ptr, size_t size);

#endif