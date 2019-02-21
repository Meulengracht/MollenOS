/* MollenOS
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
#include <ds/collection.h>

 // Slab size is equal to a page size, and memory layout of a slab is as below
 // FreeBitmap | Object | Object | Object |
typedef struct {
    CollectionItem_t Header;
    size_t           NumberOfFreeObjects;
    uintptr_t*       Address;  // Points to first object
    uint8_t*         FreeBitmap;
} MemorySlab_t;

// Memory Atomic Cache is followed directly by the buffer area for pointers
// MemoryAtomicCache_t | Pointer | Pointer | Pointer | Pointer | MemoryAtomicCache_t ...
typedef struct {
    int Available;
    int Limit;
} MemoryAtomicCache_t;

typedef struct MemoryCache {
    const char*      Name;
    SafeMemoryLock_t SyncObject;
    Flags_t          Flags;

    size_t           ObjectSize;
    size_t           ObjectAlignment;
    size_t           ObjectPadding;
    size_t           ObjectCount;      // Count per slab
    size_t           PageCount;
    size_t           NumberOfFreeObjects;
    void           (*ObjectConstructor)(struct MemoryCache*, void*);
    void           (*ObjectDestructor)(struct MemoryCache*, void*);

    int              SlabOnSite;
    size_t           SlabStructureSize;
    Collection_t     FreeSlabs;
    Collection_t     PartialSlabs;
    Collection_t     FullSlabs;

    uintptr_t        AtomicCaches;
} MemoryCache_t;

#define MEMORY_DEBUG_USE_AFTER_FREE  0x1
#define MEMORY_DEBUG_OVERRUN         0x2

// MemoryCacheInitialize
// Initialize the default cache that is required for allocating new caches.
void MemoryCacheInitialize(void);

// MemoryCacheCreate
// Create a new custom memory cache that can be used to allocate objects for. Can be customized
// both with alignment, flags and constructor/destructor functionality upon creation of objects.
MemoryCache_t* MemoryCacheCreate(const char* Name, size_t ObjectSize, size_t ObjectAlignment,
    Flags_t Flags, void(*ObjectConstructor)(struct MemoryCache*, void*), void(*ObjectDestructor)(struct MemoryCache*, void*));

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

void* kmalloc_p(size_t Size, uintptr_t* DmaOut);
void* kmalloc(size_t Size);
void kfree(void* Object);

#endif //!__VALI_HEAP_H__
