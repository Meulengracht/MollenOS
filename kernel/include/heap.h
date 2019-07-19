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
    volatile size_t  NumberOfFreeObjects;
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
    volatile size_t  NumberOfFreeObjects;
    void           (*ObjectConstructor)(struct MemoryCache*, void*);
    void           (*ObjectDestructor)(struct MemoryCache*, void*);

    int              SlabOnSite;
    size_t           SlabStructureSize;
    Collection_t     FreeSlabs;
    Collection_t     PartialSlabs;
    Collection_t     FullSlabs;

    uintptr_t        AtomicCaches;
} MemoryCache_t;

// Debug options for caches
#define HEAP_DEBUG_USE_AFTER_FREE   0x1
#define HEAP_DEBUG_OVERRUN          0x2

// Configuration options for caches
#define HEAP_CACHE_DEFAULT          0x4 // Only set for fixed size caches
#define HEAP_SLAB_NO_ATOMIC_CACHE   0x8 // Set to disable smp optimizations

// MemoryCacheInitialize
// Initialize the default cache that is required for allocating new caches.
void MemoryCacheInitialize(void);

// MemoryCacheConstruct
// Create a new custom memory cache that can be used to allocate objects for. Can be customized
// both with alignment, flags and constructor/destructor functionality upon creation of objects.
KERNELAPI void KERNELABI
MemoryCacheConstruct(
    _In_ MemoryCache_t* Cache,
    _In_ const char*    Name,
    _In_ size_t         ObjectSize,
    _In_ size_t         ObjectAlignment,
    _In_ Flags_t        Flags,
    _In_ void(*ObjectConstructor)(struct MemoryCache*, void*),
    _In_ void(*ObjectDestructor)(struct MemoryCache*, void*));

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
