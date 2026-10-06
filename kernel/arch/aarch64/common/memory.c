/**
 * Copyright, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <arch/mmu.h>
#include <arch/aarch64/arch.h>
#include <arch/interrupts.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>
#include "private.h"

// Early translation must precede C atomics: with translation disabled the
// architecture does not provide the Normal-memory attributes they require.
// This arena belongs to the loaded kernel image and is already reserved.
static uint64_t     g_bootTables[ARM64_BOOT_TABLE_COUNT][ARM64_TABLE_ENTRIES] __attribute__((aligned(ARM64_PAGE_SIZE)));
static unsigned int g_bootTableCount;
static int          g_bootAllocating;
static uintptr_t    g_identityTable;
static uintptr_t    g_kernelTable;
static Spinlock_t   g_tableLock;

/**
 * @brief Writes modified translation tables out of this CPU's data cache.
 *
 * Follows child-table links, then cleans every cache line in the current table.
 * Does not clean the mapped data pages or wait for the whole operation to finish;
 * the caller must issue a completion barrier before releasing a secondary CPU.
 *
 * @param address Physical address of a table, also directly accessible by the kernel.
 * @param level Table level, from zero for a root to ARM64_TABLE_PAGE_LEVEL for pages.
 * @param lineSize Nonzero data-cache line size in bytes for the calling CPU.
 */
static void
__CleanTranslationTable(
    _In_ uintptr_t    address,
    _In_ unsigned int level,
    _In_ size_t       lineSize)
{
    uint64_t* table = (uint64_t*)address;

    // A secondary initially walks these tables with its caches disabled.
    // Publish every table to the point of coherency before releasing that CPU.
    if (level < ARM64_TABLE_PAGE_LEVEL) {
        for (unsigned int i = 0; i < ARM64_TABLE_ENTRIES; i++) {
            // At non-final levels, type 11 points to another table. A block
            // entry points to mapped memory instead and must not be traversed.
            if ((table[i] & TABLE_TYPE_MASK) == TABLE_NEXT) {
                __CleanTranslationTable(table[i] & TABLE_ADDRESS, level + 1, lineSize);
            }
        }
    }
    
    // DC CVAC writes dirty data to the point where other CPUs can observe it.
    // Step by cache line rather than by entry, covering the entire table page.
    for (size_t offset = 0; offset < ARM64_PAGE_SIZE; offset += lineSize) {
        __asm__ volatile("dc cvac, %0" :: "r"(address + offset) : "memory");
    }
}

/**
 * @brief Makes the boot memory tables visible to a CPU that is about to start.
 *
 * Cleans both the identity and shared-kernel table trees, then waits for that
 * work to complete. Called after boot translation setup and before secondary
 * startup, while the tables being published remain stable.
 */
void
Arm64PrepareSecondaryMemory(void)
{
    uint64_t cacheType;
    size_t   lineSize;

    // CTR_EL0.DminLine stores log2 of the number of four-byte words in a data
    // cache line. Convert it to bytes so no table cache line is skipped.
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(cacheType));
    
    lineSize = ARM64_CACHE_WORD_SIZE << ((cacheType >> ARM64_CTR_DATA_SHIFT) & ARM64_FEATURE_FIELD_MASK);
    __CleanTranslationTable(g_identityTable, 0, lineSize);
    __CleanTranslationTable(g_kernelTable, 0, lineSize);
    
    // DSB SY waits for all preceding cache cleans before firmware can release
    // a CPU that initially reads these tables with its caches disabled.
    __asm__ volatile("dsb sy" ::: "memory");
}

/**
 * @brief Discards cached translations after a page-table change.
 *
 * Publishes preceding table writes, invalidates EL1 translations across CPUs
 * in the inner-shareable domain, and waits for completion. No per-process
 * address-space IDs are assigned yet, so this invalidation covers every ID.
 * The caller must separately serialize table updates where required.
 */
static void
__SynchronizeTables(void)
{
    // Broadcast invalidation covers shared mappings and every ASID (ASIDs are
    // not assigned yet). Break-before-make uses this after clearing a live PTE.
    // DSB ISHST orders table stores before TLBI VMALLE1IS broadcasts the
    // invalidation. DSB ISH waits for it; ISB makes this CPU's later execution
    // use the updated translation state rather than previously fetched state.
    __asm__ volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb" ::: "memory");
}

/**
 * @brief Obtains one cleared, page-aligned translation table.
 *
 * During early boot, uses storage reserved inside the kernel image because
 * the physical allocator is not ready. Afterwards, allocates one physical page.
 * The returned physical address must also be directly accessible by the kernel.
 *
 * @return Address of the cleared table, or zero if no table storage is available.
 */
static uintptr_t
__AllocateTable(void)
{
    uintptr_t address;

    // Early allocation must not depend on the allocator or its atomic locks:
    // this storage is needed to enable the memory settings those locks require.
    if (g_bootAllocating) {
        if (g_bootTableCount == ARM64_BOOT_TABLE_COUNT) {
            return 0;
        }
        address = (uintptr_t)g_bootTables[g_bootTableCount++];
    } else {
        if (AllocatePhysicalMemory(UINT64_MAX, 1, &address) != OS_EOK) {
            return 0;
        }
    }
    
    // An empty table must contain only invalid entries. Old allocator contents
    // must never become unintended mappings when a parent starts referencing it.
    memset((void*)address, 0, ARM64_PAGE_SIZE);
    return address;
}

/**
 * @brief Finds the table entry covering an address, optionally creating child tables.
 *
 * Stops at the requested level or at an earlier block mapping. Finding an
 * entry does not imply that it is valid or backed by RAM. Callers needing a
 * final page entry must check levelOut and the entry's contents themselves.
 * Tables must be directly accessible and protected from concurrent changes
 * by the caller, except during single-CPU boot setup.
 *
 * @param root Physical address of the level-zero table.
 * @param address Virtual address to locate.
 * @param targetLevel Last level to visit, between zero and ARM64_TABLE_PAGE_LEVEL.
 * @param create Nonzero to allocate missing intermediate tables; zero for lookup only.
 * @param levelOut Receives the last level examined, including on a failed lookup.
 * @return Entry address, or NULL for a missing path or failed child-table allocation.
 */
static uint64_t*
__WalkTable(
    _In_  uintptr_t     root,
    _In_  uintptr_t     address,
    _In_  unsigned int  targetLevel,
    _In_  int           create,
    _Out_ unsigned int* levelOut)
{
    uint64_t* table = (uint64_t*)root;

    // Each level consumes nine virtual-address bits to select one of 512
    // entries. Start at bits 47:39 and move down toward the final page index.
    for (unsigned int level = 0; level <= targetLevel; level++) {
        unsigned int shift = ARM64_TABLE_ROOT_SHIFT - level * ARM64_TABLE_LEVEL_BITS;
        uint64_t* entry = &table[(address >> shift) & ARM64_TABLE_INDEX_MASK];
        uint64_t value = *entry;
        uintptr_t child;

        *levelOut = level;
        
        // Return the requested slot even if it is empty; mapping callers need
        // the slot address so they can install a new descriptor there.
        if (level == targetLevel) {
            return entry;
        }
        
        // A valid type-01 entry maps a whole block. It is not a child-table
        // pointer, so continuing through it would interpret mapped data as tables.
        if ((value & TABLE_TYPE_MASK) == TABLE_VALID) {
            return entry;
        }
        
        if (!(value & TABLE_VALID)) {
            // A lookup must not allocate tables or change the address space.
            if (!create) {
                return NULL;
            }
            
            child = __AllocateTable();
            if (!child) {
                return NULL;
            }
            
            // Zeroed child storage must be visible before publishing the link.
            __asm__ volatile("dsb ishst" ::: "memory");
            *entry = child | TABLE_NEXT;
            value = *entry;
        }
        
        // Remove permission/type bits before treating the descriptor's physical
        // address as the next table pointer. Tables are accessible by identity.
        table = (uint64_t*)(uintptr_t)(value & TABLE_ADDRESS);
    }
    return NULL;
}

/**
 * @brief Selects the root table for a virtual address in a memory space.
 *
 * Upper addresses use the shared kernel table (TTBR1). Lower addresses use
 * the memory space's private root (TTBR0), including its boot identity branch.
 * The caller supplies a valid address in the configured virtual layout.
 *
 * @param space Memory space whose root addresses are already initialized.
 * @param address Virtual address whose mapping is being accessed.
 * @return Physical address of the appropriate level-zero table.
 */
static uintptr_t
__RootForAddress(
    _In_ MemorySpace_t* space,
    _In_ uintptr_t      address)
{
    if (address >> ARM64_ADDRESS_HIGH_BIT) {
        return space->PlatformData.KernelTablePhysical;
    }
    return space->PlatformData.TablePhysical;
}

/**
 * @brief Converts kernel mapping flags into page descriptor attributes.
 *
 * Encodes permissions and memory type, plus software-owned reservation,
 * lifetime, and trap markers. Does not include a physical address. Uncommitted
 * entries retain their requested settings but stay invalid to the hardware.
 *
 * @param flags MAPPING_* flags describing the requested page.
 * @return Descriptor bits to combine with a page-aligned physical address.
 */
static uint64_t
__EncodeAttributes(
    _In_ unsigned int flags)
{
    // Mark access as already permitted so the first use does not raise an
    // access-flag fault. Inner-shareable memory supports coherent use by CPUs.
    uint64_t value = PAGE_ACCESS | PAGE_INNER_SHAREABLE;

    // At the final level, type 11 is a valid page. A reservation instead carries
    // only a software marker, so accessing it still faults until RAM is attached.
    if (flags & MAPPING_COMMIT) {
        value |= TABLE_NEXT;
    } else {
        value |= PAGE_RESERVED;
    }
    
    // A user mapping permits EL0 data access but never kernel instruction
    // execution. A kernel mapping never permits userspace instruction execution.
    if (flags & MAPPING_USERSPACE) {
        value |= PAGE_USER | PAGE_PXN;
    } else {
        value |= PAGE_UXN;
    }
    
    // Without an explicit executable request, deny instruction fetches at both
    // privilege levels, even when data reads/writes are permitted.
    if (!(flags & MAPPING_EXECUTABLE)) {
        value |= PAGE_PXN | PAGE_UXN;
    }
    
    // AP's read-only bit removes writes without removing the existing read access.
    if (flags & MAPPING_READONLY) {
        value |= PAGE_READONLY;
    }
    
    // AttrIndx selects a MAIR slot, not an address. Device registers use slot 1
    // and are never executable; ordinary cacheable RAM uses the default slot 0.
    if (flags & MAPPING_DEVICE) {
        value |= PAGE_DEVICE | PAGE_PXN | PAGE_UXN;
    } else if (flags & MAPPING_NOCACHE) {
        // DMA buffers are RAM. Device memory would fault on the unaligned
        // accesses used by ordinary C structure copies and memory routines.
        value |= PAGE_NORMAL_NOCACHE;
    }
    
    // These markers are for kernel ownership and fault handling, not hardware
    // permissions. Persistent backing is excluded from ordinary page freeing.
    if (flags & MAPPING_PERSISTENT) {
        value |= PAGE_PERSISTENT;
    }
    if (flags & MAPPING_TRAPPAGE) {
        value |= PAGE_TRAP;
    }
    return value;
}

/**
 * @brief Converts a hardware page descriptor back into kernel mapping flags.
 *
 * Recognizes both committed pages and software reservations. Writable pages
 * are reported as dirty because this port does not track hardware dirty bits.
 * This reports permissions and memory type, not the physical backing address.
 *
 * @param value Descriptor value to inspect.
 * @return MAPPING_* flags, or zero if the entry describes neither a page nor a reservation.
 */
static unsigned int
__DecodeAttributes(
    _In_ uint64_t value)
{
    unsigned int flags = 0;

    // An absent entry has no mapping attributes. Reservations must still report
    // their retained permissions so demand-fault checks can validate access.
    if (!(value & (TABLE_VALID | PAGE_RESERVED))) {
        return 0;
    }
    if (value & TABLE_VALID) {
        flags |= MAPPING_COMMIT;
    }
    
    // Select the execute-never bit for the mapping's intended privilege level:
    // UXN describes user execution; PXN describes kernel execution.
    if (value & PAGE_USER) {
        flags |= MAPPING_USERSPACE;
        if (!(value & PAGE_UXN)) {
            flags |= MAPPING_EXECUTABLE;
        }
    } else if (!(value & PAGE_PXN)) {
        flags |= MAPPING_EXECUTABLE;
    }
    if (value & PAGE_READONLY) {
        flags |= MAPPING_READONLY;
    } else {
        // ARMv8.0 has no mandatory hardware dirty-bit management. A writable
        // page is conservatively dirty rather than silently losing file writes.
        flags |= MAPPING_ISDIRTY;
    }
    
    // Decode the MAIR index. Non-cacheable RAM is not Device memory and must
    // not acquire its stricter access rules merely because caches are bypassed.
    if ((value & PAGE_MEMORY_TYPE) == PAGE_DEVICE) {
        flags |= MAPPING_DEVICE | MAPPING_NOCACHE;
    } else if ((value & PAGE_MEMORY_TYPE) == PAGE_NORMAL_NOCACHE) {
        flags |= MAPPING_NOCACHE;
    }
    if (value & PAGE_PERSISTENT) {
        flags |= MAPPING_PERSISTENT;
    }
    if (value & PAGE_TRAP) {
        flags |= MAPPING_TRAPPAGE;
    }
    return flags;
}

/**
 * @brief Replaces a table entry without leaving its old valid translation cached.
 *
 * Clears a valid old entry and invalidates cached translations before writing
 * the replacement. The caller owns the table lock and must synchronize again
 * after publishing the new value. Existing physical backing is not freed here.
 *
 * @param entry Table slot to replace.
 * @param value Complete replacement descriptor, including address and attributes.
 */
static void
__ReplaceEntry(
    _In_ uint64_t* entry,
    _In_ uint64_t  value)
{
    // ARM requires an old live mapping to be removed before incompatible new
    // settings are installed. This clear/invalidate/write sequence is called
    // break-before-make; a direct overwrite can leave CPUs using old settings.
    if (*entry & TABLE_VALID) {
        *entry = 0;
        __SynchronizeTables();
    }
    *entry = value;
}

/**
 * @brief Installs a page-aligned boot mapping without allocating its backing RAM.
 *
 * Uses 4 KiB pages, or aligned 2 MiB blocks when requested. Marks backing as
 * persistent so later address-space cleanup does not free boot-owned memory.
 * Used during controlled boot setup, not as a replacement for live mappings.
 * Earlier mappings and new tables remain installed if a later step fails.
 *
 * @param root Physical address of the root table to populate.
 * @param physical Page-aligned physical start address of existing backing memory.
 * @param virtual Page-aligned virtual start address for the mapping.
 * @param length Bytes to map, in whole pages.
 * @param flags Requested mapping permissions and memory type.
 * @param blocks Nonzero to permit aligned 2 MiB block entries.
 * @return OS_EOK on success, or OS_EOOM if a table cannot be created or the
 *         walker encounters a mapping at a different level than requested.
 */
static oserr_t
__MapBootRange(
    _In_ uintptr_t    root,
    _In_ uintptr_t    physical,
    _In_ uintptr_t    virtual,
    _In_ size_t       length,
    _In_ unsigned int flags,
    _In_ int          blocks)
{
    uint64_t attributes = __EncodeAttributes(
        flags | MAPPING_COMMIT | MAPPING_PERSISTENT
    );

    while (length) {
        unsigned int targetLevel = ARM64_TABLE_PAGE_LEVEL;
        unsigned int level;
        size_t       step = ARM64_PAGE_SIZE;
        uint64_t*    entry;

        // A block replaces 512 page entries, saving scarce early table storage.
        // Both addresses must be block-aligned, and the entire block must fit
        // inside the requested range; otherwise keep the smaller page mapping.
        if (blocks && !((physical | virtual) & (ARM64_BLOCK_SIZE - 1)) && length >= ARM64_BLOCK_SIZE) {
            targetLevel = ARM64_TABLE_BLOCK_LEVEL;
            step = ARM64_BLOCK_SIZE;
        }
        
        entry = __WalkTable(root, virtual, targetLevel, 1, &level);
        // Do not overwrite an earlier-level block as if it were a page slot.
        // This boot helper cannot split a conflicting mapping into smaller ones.
        if (!entry || level != targetLevel) {
            return OS_EOOM;
        }
        
        *entry = physical | attributes;
        // Attributes are encoded for final-level pages (type 11). At level two,
        // clear bit one to make type 01, which maps a block rather than a table.
        if (targetLevel == ARM64_TABLE_BLOCK_LEVEL) {
            *entry &= ~TABLE_PAGE_BIT;
        }
        
        physical += step;
        virtual += step;
        length -= step;
    }
    return OS_EOK;
}

/**
 * @brief Builds identity mappings for the loader's mappable physical memory.
 *
 * Rounds each range to page boundaries, skips ranges marked NO_MAP, and keeps
 * identity mappings below the userspace code region. Boot table allocation and
 * the identity root must already be initialized. Failure does not roll back
 * ranges that were mapped earlier in the loop.
 *
 * @param boot Loader-provided physical memory entries.
 * @return OS_EOK on success, OS_ENOTSUPPORTED for a range outside the supported
 *         identity window or a wrapped range, or an error from boot-range mapping.
 */
static oserr_t
__MapBootMemory(
    _In_ struct VBoot* boot)
{
    struct VBootMemoryEntry* entries = (void*)(uintptr_t)boot->Memory.Entries;
    oserr_t                  status;

    for (uint32_t i = 0; i < boot->Memory.NumberOfEntries; i++) {
        uintptr_t base = entries[i].PhysicalBase & ~(ARM64_PAGE_SIZE - 1ULL);
        uintptr_t end = (entries[i].PhysicalBase + entries[i].Length + ARM64_PAGE_SIZE - 1) &
            ~(ARM64_PAGE_SIZE - 1ULL);

        // Firmware can reserve ranges that the kernel must not access through
        // a normal RAM mapping. Keep such ranges absent from the identity map.
        if (entries[i].Attributes & VBOOT_MEMORY_NO_MAP) {
            continue;
        }

        // Identity storage is confined to TTBR0[0], independently of userspace.
        // Reject overlap with the process region, and reject an end address
        // that wrapped below its base rather than mapping an unintended range.
        if (end > MEMORY_LOCATION_RING3_CODE || end < base) {
            return OS_ENOTSUPPORTED;
        }
        
        status = __MapBootRange(
            g_identityTable,
            base,
            base,
            end - base,
            MAPPING_EXECUTABLE,
            1
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

/**
 * @brief Installs the prepared roots and enables the boot CPU's MMU and caches.
 *
 * Runs at EL1 after the identity map covers the currently executing code and
 * stack. Selects 48-bit virtual addresses, 4 KiB pages, and the CPU's physical
 * address width. Preserves unrelated SCTLR control bits when enabling memory.
 *
 * @param features Value read from ID_AA64MMFR0_EL1, including its physical-width field.
 */
static void
__EnableBootTranslation(
    _In_ uint64_t features)
{
    uint64_t tcr;
    uint64_t sctlr;

    // Use a 48-bit VA, 4 KiB granule, inner-shareable WBWA table walks and the
    // implemented PA width. MAIR[0] is Normal WBWA, MAIR[1] Device-nGnRnE,
    // and MAIR[2] Normal non-cacheable RAM.
    tcr = ARM64_TCR_VA_SIZE | ARM64_TCR_SHAREABLE | ARM64_TCR_WALK_CACHE |
        ARM64_TCR_GRANULE | ((features & ARM64_MMFR0_PA_MASK) << ARM64_TCR_PA_SHIFT);

    // MAIR defines the memory types referenced by page AttrIndx fields. TCR
    // defines address sizes and how hardware reads tables. TTBR0 selects the
    // lower identity/process root; TTBR1 selects the shared upper kernel root.
    // Complete table writes before installing them, apply the register changes,
    // discard this CPU's old translations, then wait before enabling the MMU.
    __asm__ volatile(
        "dsb sy\n"
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "msr ttbr1_el1, %3\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb"
        :: "r"(ARM64_MAIR), "r"(tcr), "r"(g_identityTable), "r"(g_kernelTable) : "memory");
    
    // SCTLR.M enables translation, C enables the data cache, and I enables the
    // instruction cache. ISB makes this CPU execute under the new settings.
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= ARM64_SCTLR_MMU | ARM64_SCTLR_DATA_CACHE | ARM64_SCTLR_INSTRUCTION_CACHE;
    __asm__ volatile("msr sctlr_el1, %0\nisb" :: "r"(sctlr) : "memory");
}

/**
 * @brief Prepares the boot memory tables before shared kernel initialization.
 *
 * Uses the kernel's reserved table arena to create the identity and shared
 * roots, maps loader memory, and enables translation and caches. Initializes
 * the table lock only after normal RAM attributes are available for atomics.
 * Called once on the boot CPU; failure stops boot rather than undoing setup.
 *
 * @param boot Loader-provided boot information containing the physical memory map.
 * @return OS_EOK on success, OS_ENOTSUPPORTED for unsupported memory settings
 *         or ranges, or an error from constructing the identity map.
 */
oserr_t
Arm64BootMemoryInitialize(
    _In_ struct VBoot* boot)
{
    uint64_t features;
    oserr_t  status;

    // The whole table layout assumes 4 KiB pages. Check TGran4 before creating
    // mappings; this port has no alternate table format for other page sizes.
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(features));
    if (((features >> ARM64_MMFR0_GRANULE_SHIFT) & ARM64_FEATURE_FIELD_MASK) == ARM64_MMFR0_NO_GRANULE) {
        return OS_ENOTSUPPORTED;
    }
    
    // The general allocator is not available at this point. Both initial roots
    // come from the image's reserved arena, and the kernel root starts empty.
    g_bootAllocating = 1;
    g_identityTable = __AllocateTable();
    g_kernelTable = __AllocateTable();
    
    status = __MapBootMemory(boot);
    if (status != OS_EOK) {
        return status;
    }
    
    // Only after translation supplies normal RAM settings can later table
    // allocation use the physical allocator and its synchronization safely.
    __EnableBootTranslation(features);
    g_bootAllocating = 0;

    SpinlockConstruct(&g_tableLock);
    return OS_EOK;
}

/**
 * @brief Reports the virtual-address layout and physical allocation limits.
 *
 * Supplies shared kernel, process code/heap, and thread-local ranges, the
 * 4 KiB page size, and physical limits for low, 32-bit, and unrestricted RAM.
 *
 * @param configuration Receives the complete platform memory configuration.
 */
void
MmuGetMemoryConfiguration(
    _Out_ PlatformMemoryConfiguration_t* configuration)
{
    memset(configuration, 0, sizeof(*configuration));
    configuration->PageSize = ARM64_PAGE_SIZE;
    configuration->MemoryMap.Shared.Start = MEMORY_LOCATION_SHARED_START;
    configuration->MemoryMap.Shared.Length = MEMORY_LOCATION_SHARED_END - MEMORY_LOCATION_SHARED_START;
    configuration->MemoryMap.UserCode.Start = MEMORY_LOCATION_RING3_CODE;
    configuration->MemoryMap.UserCode.Length = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;
    configuration->MemoryMap.UserHeap.Start = MEMORY_LOCATION_RING3_HEAP;
    configuration->MemoryMap.UserHeap.Length = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;
    configuration->MemoryMap.ThreadLocal.Start = MEMORY_LOCATION_RING3_THREAD_START;
    configuration->MemoryMap.ThreadLocal.Length = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START - 1;
    // These masks constrain physical allocation, not access permissions. A
    // device's address limit should not change the process's virtual layout.
    configuration->MemoryMaskCount = 3;
    configuration->MemoryMasks[0] = ARM64_MEMORY_MASK_LOW;
    configuration->MemoryMasks[1] = ARM64_MEMORY_MASK_32;
    configuration->MemoryMasks[2] = UINT64_MAX;
}

/**
 * @brief Attaches the boot roots to the kernel space and adds shared mappings.
 *
 * Reuses roots prepared by Arm64BootMemoryInitialize and adds persistent
 * kernel mappings. Entries added before a failure remain installed. This is
 * boot setup, not an update that rolls back all changes on failure.
 *
 * @param space Kernel memory space whose root addresses will be initialized.
 * @param boot Shared API boot information; unused after early boot setup.
 * @param mappings Ranges ending with a zero Length entry. Starts must be
 *                 page-aligned; lengths are rounded up to whole pages.
 * @return OS_EOK on success, or an error from installing a listed mapping.
 */
oserr_t
MmuLoadKernel(
    _InOut_ MemorySpace_t*           space,
    _In_    struct VBoot*            boot,
    _In_    PlatformMemoryMapping_t* mappings)
{
    oserr_t status;

    (void)boot;
    // Keep the lower boot identity window and the common upper kernel tree.
    // Replacing either root here would discard the mappings prepared before C.
    space->PlatformData.TablePhysical = g_identityTable;
    space->PlatformData.KernelTablePhysical = g_kernelTable;
    
    for (unsigned int i = 0; mappings[i].Length; i++) {
        status = __MapBootRange(
            g_kernelTable,
            mappings[i].PhysicalBase,
            mappings[i].VirtualBase,
            (mappings[i].Length + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL),
            0,
            0
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    
    // Publish added mappings and discard translations or cached faults left
    // from before these shared addresses had backing.
    __SynchronizeTables();
    return OS_EOK;
}

/**
 * @brief Selects a memory space's lower-address mappings on the current CPU.
 *
 * Changes TTBR0 while keeping the common kernel root in TTBR1. Flushes cached
 * translations because this port does not assign separate address-space IDs.
 *
 * @param space Initialized memory space to activate on the calling CPU.
 */
void
ArchMmuSwitchMemorySpace(
    MemorySpace_t* space)
{
    // Finish earlier accesses before changing TTBR0. ISB applies the new root
    // to later execution; the helper then removes translations from the old
    // space. TTBR1 stays unchanged so common kernel addresses remain usable.
    __asm__ volatile(
        "dsb ish\n"
        "msr ttbr0_el1, %0\n"
        "isb"
        :: "r"(space->PlatformData.TablePhysical) : "memory");
    __SynchronizeTables();
}

/**
 * @brief Installs page entries for a supplied list of physical addresses.
 *
 * Creates missing tables, but never allocates data pages or overwrites existing
 * mappings or reservations. Earlier pages remain installed after a later error.
 *
 * @param space Memory space to modify; upper addresses select its kernel root.
 * @param address Page-aligned first virtual address.
 * @param physical Array of at least count page-aligned physical addresses.
 * @param count Nonnegative number of pages to install.
 * @param flags Common MAPPING_* attributes for all requested pages.
 * @param updatedOut Receives the number installed, including on failure.
 * @return OS_EOK for full completion, OS_EOOM for table allocation failure,
 *         or OS_EEXISTS for an occupied slot or an earlier block mapping.
 */
oserr_t
ArchMmuSetVirtualPages(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  const paddr_t* physical,
    _In_  int            count,
    _In_  unsigned int   flags,
    _Out_ int*           updatedOut)
{
    oserr_t status = OS_EOK;

    *updatedOut = 0;
    
    // Different CPUs may create tables or update entries in the same tree.
    // Serialize that work and mask local IRQs so a handler cannot interrupt
    // the lock owner and then wait for that same lock.
    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            1,
            &level
        );

        if (!entry) {
            status = OS_EOOM;
            break;
        }
        
        // A software reservation owns its slot even while invalid to hardware.
        // Do not overwrite it, an existing page, or an earlier large block.
        if (level != ARM64_TABLE_PAGE_LEVEL || *entry) {
            status = OS_EEXISTS;
            break;
        }
        
        *entry = physical[i] | __EncodeAttributes(flags);
        (*updatedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    
    // Synchronize even a partially installed prefix before releasing the lock.
    // updatedOut tells the caller which pages remain installed after an error.
    __SynchronizeTables();
    SpinlockReleaseIrq(&g_tableLock);
    return status;
}

/**
 * @brief Maps a consecutive physical range to consecutive virtual pages.
 *
 * Installs one page at a time through the ordinary page installer without
 * allocating backing RAM. Earlier pages remain after a later error. The range
 * is not processed under one lock acquisition.
 *
 * @param space Memory space to modify.
 * @param address Page-aligned first virtual address.
 * @param physical Page-aligned first physical address; advances by a page per entry.
 * @param count Nonnegative number of pages to install.
 * @param flags Common MAPPING_* attributes for the range.
 * @param updatedOut Receives the successfully installed page count.
 * @return OS_EOK on full completion, or the first error from page installation.
 */
oserr_t
ArchMmuSetContiguousVirtualPages(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  paddr_t        physical,
    _In_  int            count,
    _In_  unsigned int   flags,
    _Out_ int*           updatedOut)
{
    oserr_t status = OS_EOK;

    *updatedOut = 0;
    for (int i = 0; i < count; i++) {
        int updated;

        // Reuse normal collision checks and synchronization for each page;
        // contiguous RAM does not need a second descriptor-writing implementation.
        status = ArchMmuSetVirtualPages(
            space,
            address,
            &physical,
            1,
            flags,
            &updated
        );
        if (status != OS_EOK) {
            break;
        }
        
        (*updatedOut)++;
        address += ARM64_PAGE_SIZE;
        physical += ARM64_PAGE_SIZE;
    }
    return status;
}

/**
 * @brief Reserves virtual pages without making them accessible to the CPU.
 *
 * Retains requested settings in invalid, software-marked entries without
 * allocating backing RAM. A later commit attaches RAM and makes them valid.
 * Earlier reservations remain installed if a later page fails.
 *
 * @param space Memory space to modify.
 * @param address Page-aligned first virtual address to reserve.
 * @param count Nonnegative number of pages to reserve.
 * @param flags Desired MAPPING_* settings; MAPPING_COMMIT is explicitly removed.
 * @param reservedOut Receives the number reserved, including on failure.
 * @return OS_EOK on full completion, or the first error from page installation.
 */
oserr_t
ArchMmuReserveVirtualPages(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t address,
    _In_  int count,
    _In_  unsigned int flags,
    _Out_ int* reservedOut)
{
    oserr_t status = OS_EOK;
    paddr_t physical = 0;

    *reservedOut = 0;
    for (int i = 0; i < count; i++) {
        int updated;

        // Zero is only a placeholder for backing that has not been supplied.
        // Removing COMMIT keeps this invalid, rather than mapping physical page zero.
        status = ArchMmuSetVirtualPages(
            space,
            address,
            &physical,
            1,
            flags & ~MAPPING_COMMIT,
            &updated
        );
        if (status != OS_EOK) {
            break;
        }
        
        (*reservedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    return status;
}

/**
 * @brief Attaches supplied physical pages to existing virtual reservations.
 *
 * Despite its singular name, processes count consecutive pages. Preserves
 * each reservation's settings without allocating RAM or missing tables.
 * A committed prefix remains installed if a later entry is not a reservation.
 *
 * @param space Memory space containing the reservations.
 * @param address Page-aligned first reserved virtual address.
 * @param physical Array of at least count page-aligned backing addresses.
 * @param count Nonnegative number of pages to commit.
 * @param committedOut Receives the number committed, including on failure.
 * @return OS_EOK on full completion, or OS_ENOENT for a missing entry,
 *         an earlier block mapping, or an entry not marked as reserved.
 */
oserr_t
ArchMmuCommitVirtualPage(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  const paddr_t* physical,
    _In_  int            count,
    _Out_ int*           committedOut)
{
    oserr_t status = OS_EOK;

    *committedOut = 0;

    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            0,
            &level
        );

        // Commit consumes an existing reservation. It must not create an
        // unreserved mapping, overwrite backing, or split an earlier block.
        if (!entry || level != ARM64_TABLE_PAGE_LEVEL || !(*entry & PAGE_RESERVED)) {
            status = OS_ENOENT;
            break;
        }
        
        // Replace the placeholder address and reservation marker, retaining
        // permissions and lifetime flags. Type 11 makes this a hardware-valid page.
        *entry = (*entry & ~(TABLE_ADDRESS | PAGE_RESERVED)) | physical[i] | TABLE_NEXT;
        (*committedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    __SynchronizeTables();
    SpinlockReleaseIrq(&g_tableLock);
    return status;
}

/**
 * @brief Reads mapping attributes for a sequence of virtual pages.
 *
 * Reports zero for absent mappings rather than failing. Reservations retain
 * their settings; an earlier block supplies its settings for covered addresses.
 * Does not allocate tables or invalidate cached translations.
 *
 * @param space Memory space to inspect.
 * @param address First virtual address, advancing by a page for each result.
 * @param count Nonnegative number of pages to inspect.
 * @param attributes Output array with room for at least count flag values.
 * @param retrievedOut Receives the inspected count, including absent pages.
 * @return OS_EOK after filling the requested results.
 */
oserr_t
ArchMmuGetPageAttributes(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  int            count,
    _Out_ unsigned int*  attributes,
    _Out_ int*           retrievedOut)
{
    *retrievedOut = 0;

    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            0,
            &level
        );

        // Absence is a result for this API, not an error. Count it so each
        // output slot still corresponds to the same requested virtual address.
        attributes[i] = entry ? __DecodeAttributes(*entry) : 0;
        (*retrievedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    SpinlockReleaseIrq(&g_tableLock);
    return OS_EOK;
}

/**
 * @brief Replaces mapping attributes while retaining physical backing addresses.
 *
 * Applies one requested flag set to existing final-level entries. Removes
 * live old translations before replacing them. Earlier updates are not undone
 * if a later entry cannot be changed.
 *
 * @param space Memory space to modify.
 * @param address Page-aligned first virtual address.
 * @param count Nonnegative number of pages to update.
 * @param attributes Input flags for all pages. After the first successful update,
 *                   receives that page's previous flags instead.
 * @param updatedOut Receives the successfully updated page count.
 * @return OS_EOK on full completion, or OS_ENOENT for a missing or non-page entry.
 */
oserr_t
ArchMmuUpdatePageAttributes(
    _In_    MemorySpace_t* space,
    _In_    vaddr_t        address,
    _In_    int            count,
    _InOut_ unsigned int*  attributes,
    _Out_   int*           updatedOut)
{
    // Preserve the input before attributes is reused to report old settings.
    // Otherwise pages after the first would receive its previous permissions.
    unsigned int flags = *attributes;
    oserr_t      status = OS_EOK;

    *updatedOut = 0;

    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry;
        uint64_t     value;

        entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            0,
            &level
        );
        if (!entry || level != ARM64_TABLE_PAGE_LEVEL || !*entry) {
            status = OS_ENOENT;
            break;
        }
        
        // Lookup must find an existing page or reservation; this path neither
        // creates missing entries nor changes an entire earlier-level block.
        value = *entry;
        if (!i) {
            // The API returns only the first page's previous flags. The saved
            // input above remains the requested setting for every page.
            *attributes = __DecodeAttributes(value);
        }
        
        // Keep the backing address but rebuild attributes from the request.
        // The replacement helper removes a live old mapping before changing it.
        __ReplaceEntry(entry, (value & TABLE_ADDRESS) | __EncodeAttributes(flags));
        
        (*updatedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    __SynchronizeTables();
    SpinlockReleaseIrq(&g_tableLock);
    return status;
}

/**
 * @brief Removes page entries and reports backing addresses that may be reclaimed.
 *
 * Does not free physical RAM directly. Only valid, non-persistent entries add
 * an address to freed. Reservations and persistent mappings count as cleared
 * without returning backing. Earlier removals remain after a later error.
 *
 * @param space Memory space to modify.
 * @param address Page-aligned first virtual address to clear.
 * @param count Nonnegative number of entries to clear.
 * @param freed Array with room for count physical addresses for the caller to reclaim.
 * @param freedCountOut Receives the number of valid addresses written to freed.
 * @param clearedOut Receives the number cleared, including on failure.
 * @return OS_EOK on full completion, or OS_ENOENT for a missing or non-page entry.
 */
oserr_t
ArchMmuClearVirtualPages(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  int            count,
    _In_  paddr_t*       freed,
    _Out_ int*           freedCountOut,
    _Out_ int*           clearedOut)
{
    oserr_t status = OS_EOK;

    *freedCountOut = 0;
    *clearedOut = 0;

    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            0,
            &level
        );

        if (!entry || level != ARM64_TABLE_PAGE_LEVEL || !*entry) {
            status = OS_ENOENT;
            break;
        }

        // Reservations have no RAM to return. Persistent backing is owned
        // elsewhere even when this particular virtual mapping is removed.
        if ((*entry & TABLE_VALID) && !(*entry & PAGE_PERSISTENT)) {
            freed[(*freedCountOut)++] = *entry & TABLE_ADDRESS;
        }

        // Remove software ownership markers as well as hardware validity so
        // later mapping or reservation requests can reuse the empty slot.
        *entry = 0;

        (*clearedOut)++;
        address += ARM64_PAGE_SIZE;
    }
    // Discard cached access to removed pages before returning their addresses.
    // The caller can then reclaim backing without CPUs using the stale mapping.
    __SynchronizeTables();
    SpinlockReleaseIrq(&g_tableLock);
    return status;
}

/**
 * @brief Resolves virtual addresses to physical addresses without changing mappings.
 *
 * Supports pages and earlier-level blocks. Preserves the first address's offset
 * within a page; following lookups start at each next page boundary. Reserved
 * but uncommitted entries are not valid physical translations.
 *
 * @param space Memory space to inspect.
 * @param address First virtual address, possibly including an offset within a page.
 * @param count Nonnegative number of page addresses to resolve.
 * @param physical Output array with room for count resolved addresses.
 * @param retrievedOut Receives the successfully resolved prefix length.
 * @return OS_EOK on full completion, or OS_ENOENT for the first absent or invalid mapping.
 */
oserr_t
ArchMmuVirtualToPhysical(
    _In_  MemorySpace_t* space,
    _In_  vaddr_t        address,
    _In_  int            count,
    _Out_ paddr_t*       physical,
    _Out_ int*           retrievedOut)
{
    oserr_t status = OS_EOK;

    *retrievedOut = 0;

    SpinlockAcquireIrq(&g_tableLock);
    for (int i = 0; i < count; i++) {
        unsigned int level;
        uint64_t*    entry;
        uint64_t     mask;

        entry = __WalkTable(
            __RootForAddress(space, address),
            address,
            ARM64_TABLE_PAGE_LEVEL,
            0,
            &level
        );
        if (!entry || !(*entry & TABLE_VALID)) {
            status = OS_ENOENT;
            break;
        }
        
        // The stopping level determines the offset inside this mapping: twelve
        // bits for a page, more for a block. Combine that virtual offset with
        // the descriptor's aligned physical base rather than adding flag bits.
        mask = (1ULL << (ARM64_TABLE_ROOT_SHIFT - level * ARM64_TABLE_LEVEL_BITS)) - 1;
        physical[i] = (*entry & TABLE_ADDRESS & ~mask) | (address & mask);
        
        (*retrievedOut)++;
        
        // Preserve an offset only in the first result; following results refer
        // to page starts, as required by the shared translation API.
        address = (address & ~(ARM64_PAGE_SIZE - 1ULL)) + ARM64_PAGE_SIZE;
    }
    SpinlockReleaseIrq(&g_tableLock);
    return status;
}

/**
 * @brief Frees an owned table tree and its non-persistent page backing.
 *
 * Skips invalid entries and inherited branches, which are not owned here.
 * Owned non-final entries must point to tables; boot blocks remain beneath
 * the inherited identity branch. The caller must make this tree inactive
 * and prevent concurrent access before destruction.
 *
 * @param physical Address of an owned, allocator-backed table accessible by the kernel.
 * @param level Table level, from zero for a root to ARM64_TABLE_PAGE_LEVEL for pages.
 */
static void
__DestroyTable(
    _In_ uintptr_t    physical,
    _In_ unsigned int level)
{
    uint64_t* table = (uint64_t*)physical;

    for (unsigned int i = 0; i < ARM64_TABLE_ENTRIES; i++) {
        uint64_t  value = table[i];
        uintptr_t child = value & TABLE_ADDRESS;

        // Reservations have no physical backing, and inherited branches are
        // still owned elsewhere. Traversing or freeing them would affect siblings.
        if (!(value & TABLE_VALID) || (value & TABLE_INHERITED)) {
            continue;
        }
        
        // Free owned child tables before their parent. At the final level,
        // entries instead name data pages; leave persistent backing allocated.
        if (level < ARM64_TABLE_PAGE_LEVEL) {
            __DestroyTable(child, level + 1);
        } else if (!(value & PAGE_PERSISTENT)) {
            FreePhysicalMemory(1, &child);
        }
    }
    
    // The table page itself is ours, even if all its entries were shared or
    // persistent. Only allocator-backed private table pages reach this helper.
    FreePhysicalMemory(1, &physical);
}

/**
 * @brief Creates a private root while sharing required existing mapping branches.
 *
 * Shares the identity branch and common kernel root. With inheritance, copies
 * process-branch links, not tables or RAM, and marks them as owned elsewhere.
 * Leaves the thread-local root slot empty. Ensures a process branch exists
 * before sibling threads can share it.
 *
 * @param parent Optional source space; used only when inherit is nonzero.
 * @param child Destination space to initialize. It must not be used on failure.
 * @param inherit Nonzero to share parent process branches; zero for a fresh space.
 * @return OS_EOK on success, or OS_EOOM if a required table cannot be allocated.
 */
oserr_t
MmuCloneVirtualSpace(
    _In_ MemorySpace_t* parent,
    _In_ MemorySpace_t* child,
    _In_ int            inherit)
{
    uint64_t* root;
    uintptr_t physical = __AllocateTable();
    uintptr_t userTable;

    if (!physical) {
        return OS_EOOM;
    }
    
    root = (uint64_t*)physical;
    // Each private root keeps access to the boot identity window without owning
    // it. The common upper kernel tree is also shared, not copied or freed here.
    root[0] = ((uint64_t*)g_identityTable)[0] | TABLE_INHERITED;
    child->PlatformData.TablePhysical = physical;
    child->PlatformData.KernelTablePhysical = g_kernelTable;
    
    // Share links rather than duplicate tables or RAM. Later changes under
    // these branches are visible to siblings; INHERITED records that this child
    // must not free them. This operation does not implement copy-on-write.
    if (parent && inherit) {
        uint64_t* source = (uint64_t*)parent->PlatformData.TablePhysical;

        // Slot zero is the boot identity window; the final slot is thread-local.
        for (unsigned int i = 1; i < ARM64_TABLE_INDEX_MASK; i++) {
            if (source[i]) {
                root[i] = source[i] | TABLE_INHERITED;
            }
        }
    }
    
    // Create the process branch before cloning threads, so later mappings are
    // immediately visible through the shared branch in every sibling's root.
    if (!root[1]) {
        userTable = __AllocateTable();
        if (!userTable) {
            // Reclaim the new root only. Its inherited branches still belong
            // to their original owner and have not become this child's resources.
            FreePhysicalMemory(1, &physical);
            return OS_EOOM;
        }
        root[1] = userTable | TABLE_NEXT;
    }
    return OS_EOK;
}

/**
 * @brief Releases a memory space's private lower-root tree.
 *
 * Frees owned tables and non-persistent data pages, leaving inherited branches
 * and the shared kernel root intact. The space must no longer be active on any
 * CPU and must have no concurrent users of its owned mappings. Does not flush
 * translations or clear the memory space's root-address fields.
 *
 * @param space Retired memory space whose private tables are being destroyed.
 * @return OS_EOK after releasing the owned tree.
 */
oserr_t
MmuDestroyVirtualSpace(
    _In_ MemorySpace_t* space)
{
    // Destroy only the private lower root. KernelTablePhysical names the shared
    // upper tree, whose lifetime extends beyond any one process or thread.
    __DestroyTable(space->PlatformData.TablePhysical, 0);
    return OS_EOK;
}
