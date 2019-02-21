// slab.c : This file contains the 'main' function. Program execution begins and ends there.
//
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static uintptr_t AllocateVirtualMemory(size_t PageCount)
{
    // @todo implement
    return (uintptr_t)malloc(4096 * PageCount);
}

static void FreeVirtualMemory(uintptr_t Address, size_t PageCount)
{
    free((void*)Address);
}

static int GetCpuCount(void)
{
    return 1;
}

static int GetCpuId(void)
{
    return 0;
}

// slab.h
// Types and definitions for implementing the slab allocator.
typedef void* Spinlock_t;
typedef void* Collection_t;
typedef void* CollectionItem_t;
typedef unsigned long Flags_t;

// Slab size is equal to a page size, and memory layout of a slab is as below
// FreeBitmap | Object | Object | Object |
typedef struct {
    CollectionItem_t Header;
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
    const char*  Name;
    Spinlock_t   SyncObject;
    Flags_t      Flags;

    size_t       ObjectSize;
    size_t       ObjectAlignment;
    size_t       ObjectPadding;
    size_t       ObjectCount;      // Count per slab
    size_t       PageCount;
    size_t       NumberOfFreeObjects;
    void(*ObjectConstructor)(struct MemoryCache*, void*);
    void(*ObjectDestructor)(struct MemoryCache*, void*);

    int          SlabOnSite;
    size_t       SlabStructureSize;
    Collection_t FreeSlabs;
    Collection_t PartialSlabs;
    Collection_t FullSlabs;

    uintptr_t    AtomicCaches;
} MemoryCache_t;

#define MEMORY_DEBUG_USE_AFTER_FREE  0x1
#define MEMORY_DEBUG_OVERRUN         0x2

void  MemoryCacheInitialize(void);
MemoryCache_t* MemoryCacheCreate(const char* Name, size_t ObjectSize, size_t ObjectAlignment,
    Flags_t Flags, void(*ObjectConstructor)(struct MemoryCache*, void*), void(*ObjectDestructor)(struct MemoryCache*, void*));
void* MemoryCacheAllocate(MemoryCache_t* Cache);
void  MemoryCacheFree(MemoryCache_t* Cache, void* Object);
int   MemoryCacheReap(void);
void  MemoryCacheDestroy(MemoryCache_t* Cache);
void* kmalloc(size_t Size);
void  kfree(void* Object);

// slab.c
#include <assert.h>
#include <string.h>

#define MEMORY_OVERRUN_PATTERN                      0xA5A5A5A5
#define MEMORY_SLAB_ONSITE_THRESHOLD                512
#define MEMORY_ATOMIC_CACHE(Cache, Core)            (MemoryAtomicCache_t*)(Cache->AtomicCaches + (Core * (sizeof(MemoryAtomicCache_t) + (Cache->ObjectCount * sizeof(void*)))))
#define MEMORY_ATOMIC_ELEMENT(AtomicCache, Element) ((uintptr_t**)((uintptr_t)AtomicCache + sizeof(MemoryAtomicCache_t) + (Element * sizeof(void*))))

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

static int slab_allocate_index(MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    int i;
    for (i = 0; i < (int)Cache->ObjectCount; i++) {
        if (!(Slab->FreeBitmap[i / 8] & (1 << (i % 8)))) {
            Slab->FreeBitmap[i / 8] |= (1 << (i % 8));
            return i;
        }
    }
    return -1;
}

static void slab_free_index(MemoryCache_t* Cache, MemorySlab_t* Slab, int Index)
{
    if (Index < (int)Cache->ObjectCount) {
        Slab->FreeBitmap[Index / 8] &= ~(1 << (Index % 8));
    }
}

static int slab_contains_address(MemoryCache_t* Cache, MemorySlab_t* Slab, uintptr_t Address)
{
    uintptr_t Base = (uintptr_t)Slab->Address;
    uintptr_t End = Base + (Cache->ObjectCount * (Cache->ObjectSize + Cache->ObjectPadding));
    if (Address >= Base && Address < End) {
        return 1;
    }
    return 0;
}

static void slab_initalize_objects(MemoryCache_t* Cache, MemorySlab_t* Slab)
{
    uintptr_t Address = (uintptr_t)Slab->Address;
    size_t    i;

    for (i = 0; i < Cache->ObjectCount; i++) {
        printf("slab: Object at 0x%x\n", Address);
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

static void slab_destroy_objects(MemoryCache_t* Cache, MemorySlab_t* Slab)
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

static MemorySlab_t* slab_create(MemoryCache_t* Cache)
{
    MemorySlab_t* Slab;
    uintptr_t     ObjectAddress;
    uintptr_t     DataAddress = AllocateVirtualMemory(Cache->PageCount);
    if (Cache->SlabOnSite) {
        Slab = (MemorySlab_t*)DataAddress;
        ObjectAddress = DataAddress + Cache->SlabStructureSize;
        if (Cache->ObjectAlignment != 0 && (ObjectAddress % Cache->ObjectAlignment)) {
            ObjectAddress += Cache->ObjectAlignment - (ObjectAddress % Cache->ObjectAlignment);
        }
    }
    else {
        Slab = (MemorySlab_t*)kmalloc(Cache->SlabStructureSize);
        ObjectAddress = DataAddress;
    }

    // Handle debug flags
    if (Cache->Flags & MEMORY_DEBUG_USE_AFTER_FREE) {
        memset((void*)DataAddress, MEMORY_OVERRUN_PATTERN, (Cache->PageCount * 4096));
    }
    memset(Slab, 0, Cache->SlabStructureSize);

    // Initialize slab
    Slab->FreeBitmap = (uint8_t*)((uintptr_t)Slab + sizeof(MemorySlab_t));
    Slab->Address = (uintptr_t*)ObjectAddress;
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

static int cache_contains_address(MemoryCache_t* Cache, uintptr_t Address)
{
    int Found = 0;
    // Iterate full

    // Iterate partials
    return Found;
}

static inline struct FixedCache* cache_find_fixed_size(size_t Size)
{
    struct FixedCache* Selected = NULL;
    int                i = 0;

    // Find a cache to do the allocation in
    while (DefaultCaches[i].ObjectSize != 0) {
        if (Size <= DefaultCaches[i].ObjectSize) {
            Selected = &DefaultCaches[i];
            break;
        }
    }
    return Selected;
}

static inline size_t cache_calculate_slab_structure_size(size_t ObjectsPerSlab)
{
    size_t SlabStructure = sizeof(MemorySlab_t);
    // Calculate how many bytes the slab metadata will need
    SlabStructure += (ObjectsPerSlab / 8);
    if (ObjectsPerSlab % 8) {
        SlabStructure++;
    }
    return SlabStructure;
}

static size_t cache_calculate_atomic_cache(MemoryCache_t* Cache)
{
    // number of cpu * cpu_cache_objects + number of cpu * objects per slab * pointer size
    size_t BytesRequired = 0;
    if (GetCpuCount() > 1) {
        BytesRequired = (GetCpuCount() * (sizeof(MemoryAtomicCache_t) + (Cache->ObjectCount * sizeof(void*))));
        if (cache_find_fixed_size(BytesRequired) == NULL) {
            BytesRequired = 0;
        }
    }
    return BytesRequired;
}

static void cache_initialize_atomic_cache(MemoryCache_t* Cache)
{
    size_t AtomicCacheSize = cache_calculate_atomic_cache(Cache);
    int    i;
    if (AtomicCacheSize) {
        Cache->AtomicCaches = (uintptr_t)kmalloc(AtomicCacheSize);
        memset((void*)Cache->AtomicCaches, 0, AtomicCacheSize);
        for (i = 0; i < GetCpuCount(); i++) {
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
}

// Object size is the size of the actual object
// Object alignment is the required alignment before an object starts
// Object padding is a combined size of extra data after each object including alingment
static void cache_calculate_slab_size(MemoryCache_t* Cache, size_t ObjectSize, size_t ObjectAlignment, size_t ObjectPadding)
{
    // We only ever accept 1/8th of a page of wasted bytes
    size_t PageSize = 4096;
    size_t AcceptedWastage = (PageSize >> 4);
    size_t ReservedSpace = 0;
    int    SlabOnSite = 0;
    size_t PageCount = 1;
    int    i = 0;
    size_t ObjectsPerSlab;
    size_t Wastage;

    if ((ObjectSize + ObjectPadding) < MEMORY_SLAB_ONSITE_THRESHOLD) {
        ObjectsPerSlab = PageSize / (ObjectSize + ObjectPadding);
        SlabOnSite = 1;
        ReservedSpace = cache_calculate_slab_structure_size(ObjectsPerSlab) + ObjectAlignment;
    }
    ObjectsPerSlab = (PageSize - ReservedSpace) / (ObjectSize + ObjectPadding);
    Wastage = PageSize - (ObjectsPerSlab * (ObjectSize + ObjectPadding));

    // Make sure we always have atleast 1 element
    while (ObjectsPerSlab == 0 || Wastage > AcceptedWastage || Wastage < ReservedSpace) {
        assert(i != 31);
        i++;
        PageCount = (1 << i);
        ObjectsPerSlab = ((PageSize * PageCount) - ReservedSpace) / (ObjectSize + ObjectPadding);
        Wastage = (PageSize * PageCount) - (ObjectsPerSlab * (ObjectSize + ObjectPadding));
        if (SlabOnSite) {
            ReservedSpace = cache_calculate_slab_structure_size(ObjectsPerSlab) + ObjectAlignment;
        }
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
        PageCount = 16;
        SlabOnSite = 0;
        ReservedSpace = 0;
    }

    // Make sure we calculate the size of the slab in the case it's not allocated on site
    if (ReservedSpace == 0) {
        ReservedSpace = cache_calculate_slab_structure_size(ObjectsPerSlab);
    }

    if (Cache != NULL) {
        Cache->ObjectCount = ObjectsPerSlab;
        Cache->SlabOnSite = SlabOnSite;
        Cache->SlabStructureSize = ReservedSpace;
        Cache->PageCount = PageCount;
    }

    printf("Object(%u, %u): %u Page(s), %u Objects, %u Bytes Wasted, Slab %i, %u\n", ObjectSize, ObjectPadding,
        PageCount, ObjectsPerSlab, Wastage, SlabOnSite, ReservedSpace);
}

static void cache_construct(MemoryCache_t* Cache, const char* Name, size_t ObjectSize, size_t ObjectAlignment,
    Flags_t Flags, void(*ObjectConstructor)(struct MemoryCache*, void*), void(*ObjectDestructor)(struct MemoryCache*, void*))
{
    size_t ObjectPadding = 0;

    // Calculate padding
    if (Flags & MEMORY_DEBUG_OVERRUN) {
        ObjectPadding += 4;
    }

    if (ObjectAlignment != 0 && ((ObjectSize + ObjectPadding) % ObjectAlignment)) {
        ObjectPadding += ObjectAlignment - ((ObjectSize + ObjectPadding) % ObjectAlignment);
    }

    Cache->Name = Name;
    Cache->Flags = Flags;
    Cache->ObjectSize = ObjectSize;
    Cache->ObjectAlignment = ObjectAlignment;
    Cache->ObjectPadding = ObjectPadding;
    Cache->ObjectConstructor = ObjectConstructor;
    Cache->ObjectDestructor = ObjectDestructor;
    cache_calculate_slab_size(Cache, ObjectSize, ObjectAlignment, ObjectPadding);
}

MemoryCache_t* MemoryCacheCreate(const char* Name, size_t ObjectSize, size_t ObjectAlignment,
    Flags_t Flags, void(*ObjectConstructor)(struct MemoryCache*, void*), void(*ObjectDestructor)(struct MemoryCache*, void*))
{
    MemoryCache_t* Cache = (MemoryCache_t*)MemoryCacheAllocate(&InitialCache);
    memset(Cache, 0, sizeof(MemoryCache_t));

    // Initialize lists and spinlock

    // Construct the instance, and then see if we can enable the per-cpu cache
    cache_construct(Cache, Name, ObjectSize, ObjectAlignment, Flags, ObjectConstructor, ObjectDestructor);
    cache_initialize_atomic_cache(Cache);
    return Cache;
}

void MemoryCacheDestroy(MemoryCache_t* Cache)
{
    // If there are any cpu caches, free them, there is no need to drain the cpu caches
    // here at this point as we assume that when destroying a cache we do it for good reason
    if (Cache->AtomicCaches != 0) {
        kfree((void*)Cache->AtomicCaches);
    }

    // Iterate all the slab lists, unlink the slab and then destroy it


    MemoryCacheFree(&InitialCache, Cache);
}

void* MemoryCacheAllocate(MemoryCache_t* Cache)
{
    void* Allocated = NULL;
    // Can we allocate from cpu cache?
    if (Cache->AtomicCaches != 0) {
        MemoryAtomicCache_t* AtomicCache = MEMORY_ATOMIC_CACHE(Cache, GetCpuId());
        if (AtomicCache->Available > 0) {
            Allocated = MEMORY_ATOMIC_ELEMENT(AtomicCache, (--AtomicCache->Available))[0];
            return Allocated;
        }
    }

    // Otherwise allocate from global cache
    // Acquire lock
    // Check for partial/free
    // if length(partial) != 0, etc
    // allocate and build new slab if none exists
    // get object
    // unlock
    return Allocated;
}

void MemoryCacheFree(MemoryCache_t* Cache, void* Object)
{
    // Handle debug flags
    if (Cache->Flags & MEMORY_DEBUG_USE_AFTER_FREE) {
        memset(Object, MEMORY_OVERRUN_PATTERN, Cache->ObjectSize);
    }

    // Can we push to cpu cache?
    if (Cache->AtomicCaches != 0) {
        MemoryAtomicCache_t* AtomicCache = MEMORY_ATOMIC_CACHE(Cache, GetCpuId());
        if (AtomicCache->Available < AtomicCache->Limit) {
            MEMORY_ATOMIC_ELEMENT(AtomicCache, (AtomicCache->Available++))[0] = Object;
            return;
        }
    }

    // Acquire lock
    // No, find the slab that has this object and free it
    // unlock
}

int MemoryCacheReap(void)
{
    // Iterate the caches in the system and drain their cpu caches
    // Then start looking at the entirely free slabs, and free them.
    return 0;
}

void* kmalloc(size_t Size)
{
    struct FixedCache* Selected = cache_find_fixed_size(Size);
    assert(Selected != NULL);

    // If the cache does not exist, we must create it
    if (Selected->Cache == NULL) {
        Selected->Cache = MemoryCacheCreate(Selected->Name, Selected->ObjectSize, Selected->ObjectSize, 0, NULL, NULL);
    }
    return MemoryCacheAllocate(Selected->Cache);
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
    }
    assert(Selected != NULL);
    MemoryCacheFree(Selected->Cache, Object);
}

void MemoryCacheInitialize(void)
{
    cache_construct(&InitialCache, "cache_cache", sizeof(MemoryCache_t), 32, 0, NULL, NULL);
}

// main.c
int main()
{
    printf("Slab metrics calculation test\n");
    cache_calculate_slab_size(NULL, 32, 8, 8);
    cache_calculate_slab_size(NULL, 64, 8, 8);
    cache_calculate_slab_size(NULL, 128, 8, 8);
    cache_calculate_slab_size(NULL, 256, 8, 8);
    cache_calculate_slab_size(NULL, 512, 512, 0);
    cache_calculate_slab_size(NULL, 1024, 1024, 0);
    cache_calculate_slab_size(NULL, 44, 8, 8);
    cache_calculate_slab_size(NULL, 4096, 4096, 0);

    printf("\nSlab creation test in initial cache\n");
    MemoryCacheInitialize();
    slab_destroy(&InitialCache, slab_create(&InitialCache));
    return 0;
}
