/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * System Memory Allocator (Based on SLAB design)
 */

#ifndef __VALI_HEAP_H__
#define __VALI_HEAP_H__

#include <os/osdefs.h>

typedef struct MemoryCache MemoryCache_t;

// Debug options for caches
#define HEAP_DEBUG_USE_AFTER_FREE 0x01U
#define HEAP_DEBUG_OVERRUN        0x02U

// Configuration options for caches
#define HEAP_CACHE_DEFAULT        0x04U // Only set for fixed size caches
#define HEAP_SLAB_NO_ATOMIC_CACHE 0x08U // Set to disable smp optimizations
#define HEAP_INITIAL_SLAB         0x10U // Set to allocate the initial slab
#define HEAP_SINGLE_SLAB          0x20U // Set to disable multiple slabs
#define HEAP_CACHE_USERSPACE      0x40U // Set to allow the pages to accessed by userspace

// MemoryCacheInitialize
// Initialize the default cache that is required for allocating new caches.
void MemoryCacheInitialize(void);

// MemoryCacheCreate
// Create a new custom memory cache that can be used to allocate objects for. Can be customized
// both with alignment, flags and constructor/destructor functionality upon creation of objects.
KERNELAPI MemoryCache_t* KERNELABI
MemoryCacheCreate(
    _In_ const char*  Name,
    _In_ size_t       ObjectSize,
    _In_ size_t       ObjectAlignment,
    _In_ int          ObjectMinCount,
    _In_ unsigned int Flags,
    _In_ void        (*ObjectConstructor)(struct MemoryCache*, void*),
    _In_ void        (*ObjectDestructor)(struct MemoryCache*, void*));

// MemoryCacheAllocate
// Allocates a new object from the cache.
void* MemoryCacheAllocate(MemoryCache_t* Cache);

// MemoryCacheFree
// Frees the given object in the cache.
void MemoryCacheFree(MemoryCache_t* Cache, void* Object);

// MemoryCacheDestroy
// Destroys the entire cache and cleans up all allocated memory. Any objects in use
// are freed as well and invalidated.
void MemoryCacheDestroy(MemoryCache_t* Cache);

// MemoryCacheReap
// Performs memory cleanup on all system caches, also shrinks them if possible
// to free up memory. Returns number of pages freed.
int MemoryCacheReap(void);

// MemoryCacheDump
// Dumps information about the cache and the slabs allocated for it.
// If NULL is passed the fixed size caches will be dumped.
void MemoryCacheDump(MemoryCache_t* Cache);

void* kmalloc_p(size_t Size, uintptr_t* DmaOut);
void* kmalloc(size_t Size);
void kfree(void* Object);

#endif //!__VALI_HEAP_H__
