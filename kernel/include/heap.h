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
#include <arch.h>
#include <stdint.h>
#include <crtdefs.h>

/***************************
Heap Management
***************************/
#define MEMORY_STATIC_OFFSET	0x20000 /* Reserved Header Space */
#define HEAP_NORMAL_BLOCK		0x1000
#define HEAP_LARGE_BLOCK		0x10000

/* 12 bytes overhead */
typedef struct heap_node_descriptor
{
	/* Address */
	addr_t addr;

	/* Link */
	struct heap_node_descriptor *link;

	/* Status */
	uint32_t allocated : 1;

	/* Length */
	size_t length : 31;

} heap_node_t;

#define BLOCK_NORMAL			0x0
#define BLOCK_LARGE				0x1
#define BLOCK_VERY_LARGE		0x2
#define AddrIsAligned(x) ((x & 0xFFF) == 0)

#define ALLOCATION_NORMAL		0x0
#define ALLOCATION_ALIGNED		0x1
#define ALLOCATION_SPECIAL		0x2

/* A block descriptor, contains a 
 * memory range */
typedef struct heap_block_descriptor
{
	/* Start - End Address */
	addr_t addr_start;
	addr_t addr_end;

	/* Node Flags */
	int flags;

	/* Stats */
	size_t bytes_free;

	/* Spinlock */
	spinlock_t plock;

	/* Next in Linked List */
	struct heap_block_descriptor *link;

	/* Head of Header List */
	struct heap_node_descriptor *nodes;

} heap_block_t;

/* A heap */
typedef struct heap_area
{
	/* Head of Node List */
	struct heap_block_descriptor *blocks;

} heap_t;

/* Initializer & Maintience */
extern void heap_init(void);
extern uint32_t heap_get_count(void);
extern void heap_print_stats(void);
extern void heap_reap(void);

//kMalloc Align and phys return
extern void *kmalloc_ap(size_t sz, addr_t *p);

//kMalloc return phys
extern void *kmalloc_p(size_t sz, addr_t *p);

//kMalloc align
extern void *kmalloc_a(size_t sz);

//kMalloc
extern void *kmalloc(size_t sz);

//kFree
extern void kfree(void *p);

//krealloc / reallocate
extern void *kcalloc(size_t nmemb, size_t size);
extern void *krealloc(void *ptr, size_t size);

#endif