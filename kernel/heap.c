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
#define __MODULE "HEAP"
//#define __TRACE

#include <system/utils.h>
#include <memoryspace.h>
#include <machine.h>
#include <debug.h>
#include <heap.h>
#include <assert.h>
#include <string.h>

#define MEMORY_OVERRUN_PATTERN                      0xA5A5A5A5
#define MEMORY_SLAB_ONSITE_THRESHOLD                512
#define MEMORY_ATOMIC_CACHE(Cache, Core)            (MemoryAtomicCache_t*)(Cache->AtomicCaches + (Core * (sizeof(MemoryAtomicCache_t) + (Cache->ObjectCount * sizeof(void*)))))
#define MEMORY_ATOMIC_ELEMENT(AtomicCache, Element) ((uintptr_t**)((uintptr_t)AtomicCache + sizeof(MemoryAtomicCache_t) + (Element * sizeof(void*))))
#define MEMORY_SLAB_ELEMENT(Cache, Slab, Element)   (void*)((uintptr_t)Slab->Address + (Element * (Cache->ObjectSize + Cache->ObjectPadding)))

static MemoryCache_t InitialCache = { 0 };
static struct FixedCache {
    size_t         ObjectSize;
    const char*    Name;
    MemoryCache_t* Cache;
} DefaultCaches[] = {
    { 32,     "size32_cache",     NULL },
    { 64,     "size64_cache",     NULL },
    { 128,    "size128_cache",    NULL },
    { 256,    "size256_cache",    NULL },
    { 512,    "size512_cache",    NULL },
    { 1024,   "size1024_cache",   NULL },
    { 2048,   "size2048_cache",   NULL },
    { 4096,   "size4096_cache",   NULL },
    { 8192,   "size8192_cache",   NULL },
    { 16384,  "size16384_cache",  NULL },
    { 32768,  "size32768_cache",  NULL },
    { 65536,  "size65536_cache",  NULL },
    { 131072, "size131072_cache", NULL },
    { 0,      NULL,               NULL }
};

static uintptr_t AllocateVirtualMemory(size_t PageCount)
{
    uintptr_t  Address;
    size_t     PageSize = GetMemorySpacePageSize();
    OsStatus_t Status   = CreateMemorySpaceMapping(GetCurrentMemorySpace(), NULL, &Address, PageSize * PageCount, 
        MAPPING_COMMIT | MAPPING_DOMAIN, MAPPING_PHYSICAL_DEFAULT | MAPPING_VIRTUAL_GLOBAL, __MASK);
    if (Status != OsSuccess) {
        ERROR("Ran out of memory for allocation in the heap");
        return 0;
    }
    return Address;
}

static void FreeVirtualMemory(uintptr_t Address, size_t PageCount)
{
    size_t     PageSize = GetMemorySpacePageSize();
    OsStatus_t Status   = RemoveMemorySpaceMapping(GetCurrentMemorySpace(), Address, PageSize * PageCount);
    if (Status != OsSuccess) {
        ERROR("Failed to free allocation 0x%x of size 0x%x", Address, PageSize * PageCount);
    }
}

static int slab_allocate_index(MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    int i;
    for (i = 0; i < (int)Cache->ObjectCount; i++) {
        if (!(Slab->FreeBitmap[i / 8] & (1 << (i % 8)))) {
            Slab->FreeBitmap[i / 8] |= (1 << (i % 8));
            Slab->NumberOfFreeObjects--;
            return i;
        }
    }
    return -1;
}

static void slab_free_index(MemoryCache_t* Cache, MemorySlab_t* Slab, int Index)
{
    if (Index < (int)Cache->ObjectCount) {
        Slab->FreeBitmap[Index / 8] &= ~(1 << (Index % 8));
        Slab->NumberOfFreeObjects++;
    }
}

static int slab_contains_address(MemoryCache_t* Cache, MemorySlab_t* Slab, uintptr_t Address)
{
    uintptr_t Base = (uintptr_t)Slab->Address;
    uintptr_t End  = Base + (Cache->ObjectCount * (Cache->ObjectSize + Cache->ObjectPadding));
    if (Address >= Base && Address < End) {
        return (int)((Address - Base) / (Cache->ObjectSize + Cache->ObjectPadding));
    }
    return -1;
}

static void slab_initalize_objects(MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    uintptr_t Address = (uintptr_t)Slab->Address;
    size_t    i;

    for (i = 0; i < Cache->ObjectCount; i++) {
        if (Cache->ObjectConstructor) {
            Cache->ObjectConstructor(Cache, (void*)Address);
        }

        Address += Cache->ObjectSize;
        if (Cache->Flags & MEMORY_DEBUG_OVERRUN) {
            *((uint32_t*)Address) = MEMORY_OVERRUN_PATTERN;
        }
        Address += Cache->ObjectPadding;
    }
}

static void 
slab_destroy_objects(
    MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    uintptr_t Address = (uintptr_t)Slab->Address;
    size_t    i;

    for (i = 0; i < Cache->ObjectCount; i++) {
        if (Cache->ObjectDestructor) {
            Cache->ObjectDestructor(Cache, (void*)Address);
        }
        Address += Cache->ObjectSize + Cache->ObjectPadding;
    }
}

static MemorySlab_t* 
slab_create(
    _In_ MemoryCache_t* Cache)
{
    MemorySlab_t* Slab;
    uintptr_t     ObjectAddress;
    uintptr_t     DataAddress = AllocateVirtualMemory(Cache->PageCount);
    TRACE("slab_create(%s): 0x%x", Cache->Name, DataAddress);

    if (Cache->SlabOnSite) {
        Slab          = (MemorySlab_t*)DataAddress;
        ObjectAddress = DataAddress + Cache->SlabStructureSize;
        if (Cache->ObjectAlignment != 0 && (ObjectAddress % Cache->ObjectAlignment)) {
            ObjectAddress += Cache->ObjectAlignment - (ObjectAddress % Cache->ObjectAlignment);
        }
    }
    else {
        Slab          = (MemorySlab_t*)kmalloc(Cache->SlabStructureSize);
        ObjectAddress = DataAddress;
    }

    // Handle debug flags
    if (Cache->Flags & MEMORY_DEBUG_USE_AFTER_FREE) {
        memset((void*)DataAddress, MEMORY_OVERRUN_PATTERN, (Cache->PageCount * GetMemorySpacePageSize()));
    }
    memset(Slab, 0, Cache->SlabStructureSize);

    // Initialize slab
    Slab->NumberOfFreeObjects = Cache->ObjectCount;
    Slab->FreeBitmap          = (uint8_t*)((uintptr_t)Slab + sizeof(MemorySlab_t));
    Slab->Address             = (uintptr_t*)ObjectAddress;
    slab_initalize_objects(Cache, Slab);
    return Slab;
}

static void slab_destroy(MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    slab_destroy_objects(Cache, Slab);
    if (!Cache->SlabOnSite) {
        FreeVirtualMemory((uintptr_t)Slab->Address, Cache->PageCount);
        kfree(Slab);
    }
    else {
        FreeVirtualMemory((uintptr_t)Slab, Cache->PageCount);
    }
}

static int
cache_contains_address(
    _In_ MemoryCache_t* Cache,
    _In_ uintptr_t      Address)
{
    CollectionItem_t* Node;
    int               Found = 0;
    TRACE("cache_contains_address(%s, 0x%x)", Cache->Name, Address);

    // Check partials first
    _foreach(Node, &Cache->PartialSlabs) {
        int Index = slab_contains_address(Cache, (MemorySlab_t*)Node, Address);
        if (Index != -1) {
            Found = 1;
            break;
        }
    }

    // Otherwise move on to the full slabs
    if (!Found) {
        _foreach(Node, &Cache->FullSlabs) {
            int Index = slab_contains_address(Cache, (MemorySlab_t*)Node, Address);
            if (Index != -1) {
                Found = 1;
                break;
            }
        }
    }
    return Found;
}

static inline struct FixedCache*
cache_find_fixed_size(
    _In_ size_t Size)
{
    struct FixedCache* Selected = NULL;
    int                i = 0;

    // Find a cache to do the allocation in
    while (DefaultCaches[i].ObjectSize != 0) {
        if (Size <= DefaultCaches[i].ObjectSize) {
            Selected = &DefaultCaches[i];
            break;
        }
        i++;
    }
    return Selected;
}

static inline size_t
cache_calculate_slab_structure_size(
    _In_ size_t ObjectsPerSlab)
{
    size_t SlabStructure = sizeof(MemorySlab_t);
    // Calculate how many bytes the slab metadata will need
    SlabStructure += (ObjectsPerSlab / 8);
    if (ObjectsPerSlab % 8) {
        SlabStructure++;
    }
    return SlabStructure;
}

static size_t
cache_calculate_atomic_cache(
    _In_ MemoryCache_t* Cache)
{
    // number of cpu * cpu_cache_objects + number of cpu * objects per slab * pointer size
    size_t BytesRequired = 0;
    if (GetMachine()->NumberOfActiveCores > 1) {
        BytesRequired = (GetMachine()->NumberOfActiveCores * (sizeof(MemoryAtomicCache_t) + (Cache->ObjectCount * sizeof(void*))));
        if (cache_find_fixed_size(BytesRequired) == NULL) {
            BytesRequired = 0;
        }
    }
    return BytesRequired;
}

static void
cache_initialize_atomic_cache(
    _In_ MemoryCache_t* Cache)
{
    size_t AtomicCacheSize = cache_calculate_atomic_cache(Cache);
    int    i;
    if (AtomicCacheSize) {
        Cache->AtomicCaches = (uintptr_t)kmalloc(AtomicCacheSize);
        memset((void*)Cache->AtomicCaches, 0, AtomicCacheSize);
        for (i = 0; i < GetMachine()->NumberOfActiveCores; i++) {
            MemoryAtomicCache_t* AtomicCache = MEMORY_ATOMIC_CACHE(Cache, i);
            AtomicCache->Available = 0;
            AtomicCache->Limit = Cache->ObjectCount;
        }
    }
}

static void cache_drain_atomic_cache(MemoryCache_t* Cache)
{
    // Send out IPI to all cores to empty their caches and put them into the gloal
    // cache, this should be done when attempting to free up memory.
    // @todo
}

// Object size is the size of the actual object
// Object alignment is the required alignment before an object starts
// Object padding is a combined size of extra data after each object including alingment
static void
cache_calculate_slab_size(
    _In_ MemoryCache_t* Cache,
    _In_ size_t         ObjectSize,
    _In_ size_t         ObjectAlignment,
    _In_ size_t         ObjectPadding)
{
    // We only ever accept 1/8th of a page of wasted bytes
    size_t PageSize        = GetMemorySpacePageSize();
    size_t AcceptedWastage = (PageSize >> 4);
    size_t ReservedSpace   = 0;
    int    SlabOnSite      = 0;
    size_t PageCount       = 1;
    int    i               = 0;
    size_t ObjectsPerSlab;
    size_t Wastage;
    TRACE("cache_calculate_slab_size(%s, %u, %u, %u)",
        (Cache == NULL ? "null" : Cache->Name), ObjectSize, ObjectAlignment, ObjectPadding);

    if ((ObjectSize + ObjectPadding) < MEMORY_SLAB_ONSITE_THRESHOLD) {
        ObjectsPerSlab = PageSize / (ObjectSize + ObjectPadding);
        SlabOnSite     = 1;
        ReservedSpace  = cache_calculate_slab_structure_size(ObjectsPerSlab) + ObjectAlignment;
    }
    ObjectsPerSlab = (PageSize - ReservedSpace) / (ObjectSize + ObjectPadding);
    Wastage        = (PageSize - (ObjectsPerSlab * (ObjectSize + ObjectPadding))) - ReservedSpace;
    TRACE(" * %u Objects (%i), On %u Pages, %u Bytes of Waste (%u Bytes Reserved)", 
        ObjectsPerSlab, SlabOnSite, PageCount, Wastage, ReservedSpace);
    
    // Make sure we always have atleast 1 element
    while (ObjectsPerSlab == 0 || Wastage > AcceptedWastage) {
        assert(i != 9); // 8 = 256 pages, allow for no more
        i++;
        PageCount      = (1 << i);
        ObjectsPerSlab = (PageSize * PageCount) / (ObjectSize + ObjectPadding);
        if (SlabOnSite) {
            ReservedSpace = cache_calculate_slab_structure_size(ObjectsPerSlab) + ObjectAlignment;
        }
        ObjectsPerSlab = ((PageSize * PageCount) - ReservedSpace) / (ObjectSize + ObjectPadding);
        Wastage        = ((PageSize * PageCount) - (ObjectsPerSlab * (ObjectSize + ObjectPadding)) - ReservedSpace);
    }

    // We do, detect if there is enough room for us to keep the slab on site
    // and still provide proper alignment
    if (!SlabOnSite && (Wastage >= (cache_calculate_slab_structure_size(ObjectsPerSlab) + ObjectAlignment))) {
        SlabOnSite = 1;
    }

    // We have a special case, as this is an operating system memory allocator, and we use
    // page-sizes pretty frequently, we don't want just 1 page per slab, we instead increase this
    // to 16
    if (ObjectSize == PageSize) {
        ObjectsPerSlab = 16;
        PageCount      = 16;
        SlabOnSite     = 0;
        ReservedSpace  = 0;
    }

    // Make sure we calculate the size of the slab in the case it's not allocated on site
    if (ReservedSpace == 0) {
        ReservedSpace = cache_calculate_slab_structure_size(ObjectsPerSlab);
    }

    if (Cache != NULL) {
        Cache->ObjectCount       = ObjectsPerSlab;
        Cache->SlabOnSite        = SlabOnSite;
        Cache->SlabStructureSize = ReservedSpace;
        Cache->PageCount         = PageCount;
    }
    TRACE(" => %u Objects (%i), On %u Pages", ObjectsPerSlab, SlabOnSite, PageCount);
}

static void
cache_construct(
    _In_ MemoryCache_t* Cache,
    _In_ const char*    Name,
    _In_ size_t         ObjectSize,
    _In_ size_t         ObjectAlignment,
    _In_ Flags_t        Flags,
    _In_ void(*ObjectConstructor)(struct MemoryCache*, void*),
    _In_ void(*ObjectDestructor)(struct MemoryCache*, void*))
{
    size_t ObjectPadding = 0;

    // Calculate padding
    if (Flags & MEMORY_DEBUG_OVERRUN) {
        ObjectPadding += 4;
    }

    if (ObjectAlignment != 0 && ((ObjectSize + ObjectPadding) % ObjectAlignment)) {
        ObjectPadding += ObjectAlignment - ((ObjectSize + ObjectPadding) % ObjectAlignment);
    }

    Cache->Name              = Name;
    Cache->Flags             = Flags;
    Cache->ObjectSize        = ObjectSize;
    Cache->ObjectAlignment   = ObjectAlignment;
    Cache->ObjectPadding     = ObjectPadding;
    Cache->ObjectConstructor = ObjectConstructor;
    Cache->ObjectDestructor  = ObjectDestructor;
    cache_calculate_slab_size(Cache, ObjectSize, ObjectAlignment, ObjectPadding);
}

MemoryCache_t*
MemoryCacheCreate(
    _In_ const char* Name,
    _In_ size_t      ObjectSize,
    _In_ size_t      ObjectAlignment,
    _In_ Flags_t     Flags,
    _In_ void(*ObjectConstructor)(struct MemoryCache*, void*),
    _In_ void(*ObjectDestructor)(struct MemoryCache*, void*))
{
    MemoryCache_t* Cache = (MemoryCache_t*)MemoryCacheAllocate(&InitialCache);
    memset(Cache, 0, sizeof(MemoryCache_t));

    // Construct the instance, and then see if we can enable the per-cpu cache
    cache_construct(Cache, Name, ObjectSize, ObjectAlignment, Flags, ObjectConstructor, ObjectDestructor);
    cache_initialize_atomic_cache(Cache);
    return Cache;
}

static void
cache_destroy_list(
    _In_ MemoryCache_t* Cache,
    _In_ Collection_t*  List)
{
    MemorySlab_t* Slab;
    DataKey_t Key = { 0 };

    // Iterate all the slab lists, unlink the slab and then destroy it
    Slab = (MemorySlab_t*)CollectionGetNodeByKey(List, Key, 0);
    while (Slab != NULL) {
        CollectionRemoveByNode(List, &Slab->Header);
        slab_destroy(Cache, Slab);
    }
}

void
MemoryCacheDestroy(
    _In_ MemoryCache_t* Cache)
{
    // If there are any cpu caches, free them, there is no need to drain the cpu caches
    // here at this point as we assume that when destroying a cache we do it for good reason
    if (Cache->AtomicCaches != 0) {
        kfree((void*)Cache->AtomicCaches);
    }
    cache_destroy_list(Cache, &Cache->FreeSlabs);
    cache_destroy_list(Cache, &Cache->PartialSlabs);
    cache_destroy_list(Cache, &Cache->FullSlabs);
    MemoryCacheFree(&InitialCache, Cache);
}

void*
MemoryCacheAllocate(
    _In_ MemoryCache_t* Cache)
{
    MemorySlab_t* Slab;
    void*         Allocated = NULL;
    int           Index;
    TRACE("MemoryCacheAllocate(%s)", Cache->Name);

    // Can we allocate from cpu cache?
    if (Cache->AtomicCaches != 0) {
        MemoryAtomicCache_t* AtomicCache = MEMORY_ATOMIC_CACHE(Cache, ArchGetProcessorCoreId());
        if (AtomicCache->Available > 0) {
            Allocated = MEMORY_ATOMIC_ELEMENT(AtomicCache, (--AtomicCache->Available))[0];
            return Allocated;
        }
    }

    // Otherwise allocate from global cache
    dslock(&Cache->SyncObject);
    if (Cache->NumberOfFreeObjects != 0) {
        if (CollectionLength(&Cache->PartialSlabs) != 0) {
            Slab = (MemorySlab_t*)CollectionBegin(&Cache->PartialSlabs);
        }
        else {
            Slab = (MemorySlab_t*)CollectionPopFront(&Cache->FreeSlabs);
            CollectionAppend(&Cache->PartialSlabs, &Slab->Header);
        }
    }
    else {
        // allocate and build new slab, put it into partial list right away
        // as we are allocating a new object immediately
        Slab = slab_create(Cache);
        Cache->NumberOfFreeObjects += Cache->ObjectCount;
        CollectionAppend(&Cache->PartialSlabs, &Slab->Header);
    }
    Index = slab_allocate_index(Cache, Slab);
    if (!Slab->NumberOfFreeObjects) {
        // Last index, push to full
        CollectionPopFront(&Cache->PartialSlabs);
        CollectionAppend(&Cache->FullSlabs, &Slab->Header);
    }
    Allocated = MEMORY_SLAB_ELEMENT(Cache, Slab, Index);
    Cache->NumberOfFreeObjects--;
    dsunlock(&Cache->SyncObject);
    TRACE(" => 0x%x", Allocated);
    return Allocated;
}

void
MemoryCacheFree(
    _In_ MemoryCache_t* Cache,
    _In_ void*          Object)
{
    CollectionItem_t* Node;
    int CheckFull = 0;
    TRACE("MemoryCacheFree(%s, 0x%x)", Cache->Name, Object);

    // Handle debug flags
    if (Cache->Flags & MEMORY_DEBUG_USE_AFTER_FREE) {
        memset(Object, MEMORY_OVERRUN_PATTERN, Cache->ObjectSize);
    }

    // Can we push to cpu cache?
    if (Cache->AtomicCaches != 0) {
        MemoryAtomicCache_t* AtomicCache = MEMORY_ATOMIC_CACHE(Cache, ArchGetProcessorCoreId());
        if (AtomicCache->Available < AtomicCache->Limit) {
            MEMORY_ATOMIC_ELEMENT(AtomicCache, (AtomicCache->Available++))[0] = Object;
            return;
        }
    }

    dslock(&Cache->SyncObject);

    // Check partials first, and move to free if neccessary
    _foreach(Node, &Cache->PartialSlabs) {
        MemorySlab_t* Slab  = (MemorySlab_t*)Node;
        int           Index = slab_contains_address(Cache, Slab, (uintptr_t)Object);
        if (Index != -1) {
            slab_free_index(Cache, Slab, Index);
            if (Slab->NumberOfFreeObjects == Cache->ObjectCount) {
                CollectionRemoveByNode(&Cache->PartialSlabs, Node);
                CollectionAppend(&Cache->FreeSlabs, Node);
            }
            CheckFull = 1;
            break;
        }
    }

    // Otherwise move on to the full slabs, and move to partial if neccessary
    if (!CheckFull) {
        _foreach(Node, &Cache->FullSlabs) {
            MemorySlab_t* Slab = (MemorySlab_t*)Node;
            int           Index = slab_contains_address(Cache, Slab, (uintptr_t)Object);
            if (Index != -1) {
                slab_free_index(Cache, Slab, Index);
                CollectionRemoveByNode(&Cache->FullSlabs, Node);
                CollectionAppend(&Cache->PartialSlabs, Node);
                break;
            }
        }
    }
    dsunlock(&Cache->SyncObject);
}

int MemoryCacheReap(void)
{
    // Iterate the caches in the system and drain their cpu caches
    // Then start looking at the entirely free slabs, and free them.
    return 0;
}

void* kmalloc(size_t Size)
{
    TRACE("kmalloc(%u)", Size);
    struct FixedCache* Selected = cache_find_fixed_size(Size);
    assert(Selected != NULL);

    // If the cache does not exist, we must create it
    if (Selected->Cache == NULL) {
        Selected->Cache = MemoryCacheCreate(Selected->Name, Selected->ObjectSize, Selected->ObjectSize, 0, NULL, NULL);
    }
    return MemoryCacheAllocate(Selected->Cache);
}

void* kmalloc_p(size_t Size, uintptr_t* DmaOut)
{
    void* Allocation = kmalloc(Size);
    if (Allocation != NULL && DmaOut != NULL) {
        *DmaOut = GetMemorySpaceMapping(GetCurrentMemorySpace(), (VirtualAddress_t)Allocation);
    }
    return Allocation;
}

void kfree(void* Object)
{
    struct FixedCache* Selected = NULL;
    int                i = 0;

    // Find the cache that the allocation was done in
    while (DefaultCaches[i].ObjectSize != 0) {
        if (DefaultCaches[i].Cache != NULL) {
            if (cache_contains_address(DefaultCaches[i].Cache, (uintptr_t)Object)) {
                Selected = &DefaultCaches[i];
                break;
            }
        }
        i++;
    }
    assert(Selected != NULL);
    MemoryCacheFree(Selected->Cache, Object);
}

void MemoryCacheInitialize(void)
{
    cache_construct(&InitialCache, "cache_cache", sizeof(MemoryCache_t), 0, 0, NULL, NULL);
}
