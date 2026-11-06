/**
 * Copyright 2023, Philip Meulengracht
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
 */

#include <testbase.h>
#include <machine.h>
#include "private.h"
#include <string.h>
#include <stdio.h>

struct __AllocatePhysicalMemory {
    size_t     ExpectedMask;
    bool       CheckMask;
    int        ExpectedPageCount;
    bool       CheckPageCount;
    uintptr_t* PageValues;
    bool       PageValuesProvided;

    oserr_t ReturnValue;
    int     Calls;
};

struct __ArchMmuSetVirtualPages {
    // Parameters
    vaddr_t      ExpectedAddress;
    bool         CheckAddress;
    int          ExpectedPageCount;
    bool         CheckPageCount;
    unsigned int ExpectedAttributes;
    bool         CheckAttributes;
    paddr_t*     ExpectedPageValues;
    bool         CheckPageValues;

    // Out(s)
    int  PagesUpdated;
    bool PagesUpdatedProvided;

    oserr_t ReturnValue;
    int     Calls;
};

struct __ArchMmuSetContiguousVirtualPages {
    // Parameters
    vaddr_t      ExpectedVirtualAddress;
    bool         CheckVirtualAddress;
    paddr_t      ExpectedPhysicalAddress;
    bool         CheckPhysicalAddress;
    int          ExpectedPageCount;
    bool         CheckPageCount;
    unsigned int ExpectedAttributes;
    bool         CheckAttributes;

    // Out(s)
    int  PagesUpdated;
    bool PagesUpdatedProvided;

    oserr_t ReturnValue;
    int     Calls;
};

struct __ArchMmuReserveVirtualPages {
    // Parameters
    unsigned int ExpectedAttributes;
    bool         CheckAttributes;

    oserr_t ReturnValue;
    int     Calls;
};

struct __MSAllocationLookup {
    vaddr_t ExpectedAddress;
    bool    CheckAddress;

    struct MSAllocation* ReturnValue;
    int                  Calls;
};

struct __StaticMemoryPoolAllocate {
    size_t ExpectedLength;
    bool   CheckLength;

    uintptr_t ReturnValue;
    int       Calls;
};

struct __DynamicMemoryPoolAllocate {
    size_t ExpectedLength;
    bool   CheckLength;

    uintptr_t ReturnValue;
    int       Calls;
};

static struct __TestContext {
    SystemMachine_t  Machine;
    MemorySpace_t    MemorySpace;
    struct MSContext Context;

    // Function mocks
    struct __AllocatePhysicalMemory AllocatePhysicalMemory;
    struct __ArchMmuSetVirtualPages ArchMmuSetVirtualPages;
    struct __ArchMmuSetContiguousVirtualPages ArchMmuSetContiguousVirtualPages;
    struct __ArchMmuReserveVirtualPages ArchMmuReserveVirtualPages;
    struct __MSAllocationLookup MSAllocationLookup;
    struct __StaticMemoryPoolAllocate StaticMemoryPoolAllocate;
    struct __DynamicMemoryPoolAllocate DynamicMemoryPoolAllocate;
} g_testContext;

void*
LookupGlobalHandle(
    _In_ uuid_t       globalHandleID,
    _In_ HandleType_t type)
{
    assert_int_equal(globalHandleID, 1);
    assert_int_equal(type, HandleTypeMSContext);
    return &g_testContext.Context;
}

int Setup(void** state) {
    (void)state;
    return 0;
}

int Teardown(void** state) {
    (void)state;
    return 0;
}

int SetupTest(void** state) {
    (void)state;
    memset(&g_testContext, 0, sizeof(struct __TestContext));

    // Initialize the memory space
    g_testContext.MemorySpace.GroupGlobalHandleID = 1;
    g_testContext.MemorySpace.Flags   = MEMORY_SPACE_APPLICATION;

    // Default to 4KB pages. Tests can change this.
    g_testContext.Machine.MemoryGranularity = 0x1000;
    return 0;
}

void TestMemorySpaceMap_USERSPACE(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_USERSPACE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_USERSPACE must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                .VirtualStart = mapping,
                .Pages = &page,
                .Length = GetMemorySpacePageSize(),
                .Mask = __MASK,
                .Flags = MAPPING_USERSPACE | MAPPING_COMMIT,
                // Fixed placement does not implicitly add MAPPING_USERSPACE.
                .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_NOCACHE(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_NOCACHE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_NOCACHE must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_NOCACHE | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_READONLY(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_READONLY | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_READONLY must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_READONLY | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_EXECUTABLE(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_EXECUTABLE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_EXECUTABLE must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_EXECUTABLE | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_ISDIRTY(void** state)
{
    // MAPPING_ISDIRTY has no effect when mapping. The flag is still passed
    // to the platform code.
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_ISDIRTY | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_ISDIRTY must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_ISDIRTY | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_PERSISTANT(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_PERSISTENT | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_PERSISTENT must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_PERSISTENT | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_DOMAIN(void** state)
{
    // MAPPING_DOMAIN is reserved for future support.
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_DOMAIN | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_DOMAIN should select memory using the setup in SystemMachine_t.
    // Until implemented, check that the flag reaches ArchMmuSetVirtualPages.
    // TODO: implement unit tests for this once support for MAPPING_DOMAIN is done.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_DOMAIN | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_COMMIT(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // 3. ArchMmuReserveVirtualPages
    g_testContext.ArchMmuReserveVirtualPages.ExpectedAttributes = 0;
    g_testContext.ArchMmuReserveVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuReserveVirtualPages.ReturnValue        = OS_EOK;

    // With MAPPING_COMMIT, MemorySpaceMap must call ArchMmuSetVirtualPages.
    // Without the flag, it must call ArchMmuReserveVirtualPages.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);

    // Make the same call without MAPPING_COMMIT
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = 0,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 2);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuReserveVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_CLEAN_COMMIT(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    void*   data;
    void*   dataAligned;
    void*   expected;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_CLEAN | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Fill the allocated buffer with a pattern and check that MAPPING_CLEAN
    // zeroes the mapped page on commit. Without MAPPING_COMMIT, the flag is
    // stored until the memory is lazily allocated. (TODO test this)
    data = test_malloc(GetMemorySpacePageSize()*2);
    assert_non_null(data);
    expected = test_malloc(GetMemorySpacePageSize());
    assert_non_null(data);

    // Align up to next multiple of GetMemorySpacePageSize()
    dataAligned = (void*)((uintptr_t)data + (GetMemorySpacePageSize() - ((uintptr_t)data & (GetMemorySpacePageSize() - 1))));

    // Fill the allocated buffer with a pattern and zero the comparison buffer.
    memset(data, 0xFF, GetMemorySpacePageSize());
    memset(expected, 0, GetMemorySpacePageSize());

    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = (vaddr_t)dataAligned,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_CLEAN | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, (uintptr_t)dataAligned);

    // The mapped page must be zeroed.
    assert_memory_equal(dataAligned, expected, GetMemorySpacePageSize());

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);

    // Free the test buffers.
    test_free(data);
    test_free(expected);
}

void TestMemorySpaceMap_STACK(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000 + GetMemorySpacePageSize();
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_STACK | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // 3. StaticMemoryPoolAllocate returns space for 2 pages. MAPPING_STACK
    //    adds a guard page to the single requested page.
    g_testContext.StaticMemoryPoolAllocate.ExpectedLength = GetMemorySpacePageSize() * 2;
    g_testContext.StaticMemoryPoolAllocate.CheckLength    = true;
    g_testContext.StaticMemoryPoolAllocate.ReturnValue    = 0x1000000;

    // MAPPING_STACK changes the mapping:
    // 1. Virtual allocation adds a guard page before the requested stack pages.
    // 2. The returned address is the end of the range, since stacks grow downward.
    // Limitations: With MAPPING_VIRTUAL_FIXED, the first page of the supplied range
    //              is a guard page. The length must cover at least 2 pages.

    // Test a single page with virtual allocation. Use fixed physical pages
    // to keep physical allocation out of the test.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_STACK | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000 + (GetMemorySpacePageSize() * 2));

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 0);
    assert_int_equal(g_testContext.StaticMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);

    mapping = 0x2000000;
    page = 0x10000;

    // MAPPING_VIRTUAL_FIXED must reject lengths of one page or less.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_STACK | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.StaticMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);

    // With a 2-page range, ArchMmuSetVirtualPages must start after the guard
    // page and map only the stack page.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress   = 0x2000000 + GetMemorySpacePageSize();
    g_testContext.ArchMmuSetVirtualPages.CheckAddress      = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount    = true;

    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize() * 2,
                    .Mask = __MASK,
                    .Flags = MAPPING_STACK | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x2000000 + (GetMemorySpacePageSize() * 2));

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 2);
    assert_int_equal(g_testContext.StaticMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 2);
}

void TestMemorySpaceMap_TRAPPAGE(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_TRAPPAGE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_TRAPPAGE must reach ArchMmuSetVirtualPages unchanged.
    // Use fixed addresses to isolate the flag check.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_TRAPPAGE | MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_PhysicalSimple(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    paddr_t pageValues = 0x4000;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. AllocatePhysicalMemory.
    g_testContext.AllocatePhysicalMemory.ExpectedMask       = __MASK;
    g_testContext.AllocatePhysicalMemory.CheckMask          = true;
    g_testContext.AllocatePhysicalMemory.ExpectedPageCount  = 1;
    g_testContext.AllocatePhysicalMemory.CheckPageCount     = true;
    g_testContext.AllocatePhysicalMemory.PageValues         = &pageValues;
    g_testContext.AllocatePhysicalMemory.PageValuesProvided = true;
    g_testContext.AllocatePhysicalMemory.ReturnValue        = OS_EOK;

    // 3. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Without physical placement flags, allocate physical pages.
    mapping = 0x1000000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);
    assert_int_equal(page, pageValues);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.AllocatePhysicalMemory.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_PHYSICAL_FIXED(void** state)
{
    oserr_t oserr;
    vaddr_t mapping = 0x1000000;
    paddr_t page = 0x10000;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues = &page;
    g_testContext.ArchMmuSetVirtualPages.CheckPageValues    = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_PHYSICAL_FIXED passes .Pages to ArchMmuSetVirtualPages.
    // Check the supplied physical addresses.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_PHYSICAL_CONTIGUOUS(void** state)
{
    oserr_t oserr;
    vaddr_t mapping = 0x1000000;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetContiguousVirtualPages.
    g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedVirtualAddress  = 0x1000000;
    g_testContext.ArchMmuSetContiguousVirtualPages.CheckVirtualAddress     = true;
    g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedPhysicalAddress = 0x180000;
    g_testContext.ArchMmuSetContiguousVirtualPages.CheckPhysicalAddress    = true;
    g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedPageCount       = 1;
    g_testContext.ArchMmuSetContiguousVirtualPages.CheckPageCount          = true;
    g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedAttributes      = MAPPING_COMMIT;
    g_testContext.ArchMmuSetContiguousVirtualPages.CheckAttributes         = true;
    g_testContext.ArchMmuSetContiguousVirtualPages.ReturnValue             = OS_EOK;

    // MAPPING_PHYSICAL_CONTIGUOUS passes .PhysicalStart to
    // ArchMmuSetContiguousVirtualPages. Check the starting physical address.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .PhysicalStart = 0x180000,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_CONTIGUOUS
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetContiguousVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_VIRTUAL_GLOBAL(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page = 0x10000;
    (void)state;

    // Expected calls:
    // 1. StaticMemoryPoolAllocate.
    g_testContext.StaticMemoryPoolAllocate.ExpectedLength = GetMemorySpacePageSize();
    g_testContext.StaticMemoryPoolAllocate.CheckLength    = true;
    g_testContext.StaticMemoryPoolAllocate.ReturnValue    = 0x1000000;

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues = &page;
    g_testContext.ArchMmuSetVirtualPages.CheckPageValues    = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_VIRTUAL_GLOBAL allocates from global kernel memory.
    // Use fixed physical addresses to keep the mocks simple.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.StaticMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_VIRTUAL_PROCESS(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page = 0x10000;
    (void)state;

    // Expected calls:
    // 1. DynamicMemoryPoolAllocate.
    g_testContext.DynamicMemoryPoolAllocate.ExpectedLength = GetMemorySpacePageSize();
    g_testContext.DynamicMemoryPoolAllocate.CheckLength    = true;
    g_testContext.DynamicMemoryPoolAllocate.ReturnValue    = 0x10000000;

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x10000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues = &page;
    g_testContext.ArchMmuSetVirtualPages.CheckPageValues    = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_VIRTUAL_PROCESS allocates from process-specific memory.
    // Use fixed physical addresses to keep the mocks simple.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x10000000);

    // Expected function calls
    assert_int_equal(g_testContext.DynamicMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_VIRTUAL_THREAD(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page = 0x10000;
    (void)state;

    // Expected calls:
    // 1. DynamicMemoryPoolAllocate.
    g_testContext.DynamicMemoryPoolAllocate.ExpectedLength = GetMemorySpacePageSize();
    g_testContext.DynamicMemoryPoolAllocate.CheckLength    = true;
    g_testContext.DynamicMemoryPoolAllocate.ReturnValue    = 0x40000000;

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x40000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues = &page;
    g_testContext.ArchMmuSetVirtualPages.CheckPageValues    = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_VIRTUAL_THREAD allocates from thread-local memory.
    // Use fixed physical addresses to keep the mocks simple.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_THREAD | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x40000000);

    // Expected function calls
    assert_int_equal(g_testContext.DynamicMemoryPoolAllocate.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_VIRTUAL_FIXED(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page = 0x10000;
    (void)state;

    // Expected calls:
    // 1. MSAllocationLookup returns NULL (no existing mapping).
    //    No parameter checks are needed.

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000;
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues = &page;
    g_testContext.ArchMmuSetVirtualPages.CheckPageValues    = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // MAPPING_VIRTUAL_FIXED uses the supplied virtual address when valid.
    // Use fixed physical addresses to keep the mocks simple.

    // A fixed virtual address of 0 must be rejected.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = 0,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 0);

    // Test a valid case
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = 0x1000000,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_COMMIT,
                    .PlacementFlags = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(mapping, 0x1000000);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 2);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);
}

void TestMemorySpaceMap_VirtualMissing(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page = 0x10000;
    (void)state;

    // Reject mappings without virtual placement flags.
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = 0,
                    .PlacementFlags = 0
            },
            &mapping
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 0);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 0);
}

// TODO:
// 1. Modification of existing allocations
// 2. Error recovery
// 3. MemorySpaceCommit Tests
// 4. MemorySpaceCloneMapping Tests
// 5. MemorySpaceQuery (check guard page detection)

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMemorySpaceMap_USERSPACE, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_NOCACHE, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_READONLY, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_EXECUTABLE, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_ISDIRTY, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_PERSISTANT, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_DOMAIN, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_COMMIT, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_CLEAN_COMMIT, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_STACK, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_TRAPPAGE, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_PhysicalSimple, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_PHYSICAL_FIXED, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_PHYSICAL_CONTIGUOUS, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_VIRTUAL_GLOBAL, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_VIRTUAL_PROCESS, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_VIRTUAL_THREAD, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_VIRTUAL_FIXED, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_VirtualMissing, SetupTest),
    };
    return cmocka_run_group_tests(tests, Setup, Teardown);
}

void* kmalloc(size_t size) {
    return test_malloc(size);
}

void kfree(void* memp) {
    return test_free(memp);
}

size_t GetMemorySpacePageSize(void) {
    return g_testContext.Machine.MemoryGranularity;
}

SystemMachine_t* GetMachine(void) {
    return &g_testContext.Machine;
}

// Mocks for MemorySpaceCopyUser, which is not exercised by these tests
MemorySpace_t* GetCurrentMemorySpace(void) {
    return NULL;
}

MemorySpace_t* GetDomainMemorySpace(void) {
    return NULL;
}

oserr_t GetMemorySpaceAttributes(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int*  attributesArray) {
    return OS_ENOTSUPPORTED;
}

oserr_t GetMemorySpaceMapping(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t*     dmaVectorOut) {
    return OS_ENOTSUPPORTED;
}

oserr_t MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size) {
    return OS_ENOTSUPPORTED;
}

void MutexLock(
        _In_ Mutex_t* mutex) {
}

void MutexUnlock(
        _In_ Mutex_t* mutex) {
}

// System mocks
oserr_t AllocatePhysicalMemory(
        _In_ size_t     pageMask,
        _In_ int        pageCount,
        _In_ uintptr_t* pages) {
    printf("AllocatePhysicalMemory()\n");
    if (g_testContext.AllocatePhysicalMemory.CheckMask) {
        assert_int_equal(pageMask, g_testContext.AllocatePhysicalMemory.ExpectedMask);
    }
    if (g_testContext.AllocatePhysicalMemory.CheckPageCount) {
        assert_int_equal(pageCount, g_testContext.AllocatePhysicalMemory.ExpectedPageCount);
        // Copy page values only after checking pageCount.
        if (g_testContext.AllocatePhysicalMemory.PageValuesProvided) {
            for (int i = 0; i < pageCount; i++) {
                pages[i] = g_testContext.AllocatePhysicalMemory.PageValues[i];
            }
        }
    }
    g_testContext.AllocatePhysicalMemory.Calls++;
    return g_testContext.AllocatePhysicalMemory.ReturnValue;
}

void FreePhysicalMemory(
        _In_ int              pageCount,
        _In_ const uintptr_t* pages) {
    printf("FreePhysicalMemory()\n");
    assert_int_not_equal(pageCount, 0);
    assert_non_null(pages);
}

// Mocks from arch/mmu
oserr_t ArchMmuReserveVirtualPages(
        _In_  MemorySpace_t*   memorySpace,
        _In_  vaddr_t          startAddress,
        _In_  int              pageCount,
        _In_  unsigned int     attributes,
        _Out_ int*             pagesReservedOut) {
    printf("ArchMmuReserveVirtualPages()\n");
    assert_non_null(memorySpace);
    assert_int_not_equal(startAddress, 0);
    assert_int_not_equal(pageCount, 0);
    if (g_testContext.ArchMmuReserveVirtualPages.CheckAttributes) {
        assert_int_equal(attributes, g_testContext.ArchMmuReserveVirtualPages.ExpectedAttributes);
    }
    assert_non_null(pagesReservedOut);
    g_testContext.ArchMmuReserveVirtualPages.Calls++;
    return g_testContext.ArchMmuReserveVirtualPages.ReturnValue;
}

oserr_t ArchMmuSetVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddressValues,
        _In_  int            pageCount,
        _In_  unsigned int   attributes,
        _Out_ int*           pagesUpdatedOut) {
    printf("ArchMmuSetVirtualPages()\n");
    assert_non_null(memorySpace);
    assert_non_null(physicalAddressValues);

    if (g_testContext.ArchMmuSetVirtualPages.CheckAddress) {
        assert_int_equal(startAddress, g_testContext.ArchMmuSetVirtualPages.ExpectedAddress);
    }
    if (g_testContext.ArchMmuSetVirtualPages.CheckPageCount) {
        assert_int_equal(pageCount, g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount);
        // Check page values only after checking pageCount.
        if (g_testContext.ArchMmuSetVirtualPages.CheckPageValues) {
            assert_memory_equal(
                    physicalAddressValues,
                    g_testContext.ArchMmuSetVirtualPages.ExpectedPageValues,
                    sizeof(paddr_t) * pageCount
            );
        }
    }
    if (g_testContext.ArchMmuSetVirtualPages.CheckAttributes) {
        assert_int_equal(attributes, g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes);
    }

    assert_non_null(pagesUpdatedOut);
    if (g_testContext.ArchMmuSetVirtualPages.PagesUpdatedProvided) {
        *pagesUpdatedOut = g_testContext.ArchMmuSetVirtualPages.PagesUpdated;
    } else {
        *pagesUpdatedOut = pageCount;
    }
    g_testContext.ArchMmuSetVirtualPages.Calls++;
    return g_testContext.ArchMmuSetVirtualPages.ReturnValue;
}

oserr_t ArchMmuSetContiguousVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  paddr_t        physicalStartAddress,
        _In_  int            pageCount,
        _In_  unsigned int   attributes,
        _Out_ int*           pagesUpdatedOut) {
    printf("ArchMmuSetContiguousVirtualPages()\n");
    assert_non_null(memorySpace);
    if (g_testContext.ArchMmuSetContiguousVirtualPages.CheckVirtualAddress) {
        assert_int_equal(startAddress, g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedVirtualAddress);
    }
    if (g_testContext.ArchMmuSetContiguousVirtualPages.CheckPhysicalAddress) {
        assert_int_equal(physicalStartAddress, g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedPhysicalAddress);
    }
    if (g_testContext.ArchMmuSetContiguousVirtualPages.CheckPageCount) {
        assert_int_equal(pageCount, g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedPageCount);
    }
    if (g_testContext.ArchMmuSetContiguousVirtualPages.CheckAttributes) {
        assert_int_equal(attributes, g_testContext.ArchMmuSetContiguousVirtualPages.ExpectedAttributes);
    }

    assert_non_null(pagesUpdatedOut);
    if (g_testContext.ArchMmuSetContiguousVirtualPages.PagesUpdatedProvided) {
        *pagesUpdatedOut = g_testContext.ArchMmuSetContiguousVirtualPages.PagesUpdated;
    } else {
        *pagesUpdatedOut = pageCount;
    }
    g_testContext.ArchMmuSetContiguousVirtualPages.Calls++;
    return g_testContext.ArchMmuSetContiguousVirtualPages.ReturnValue;
}

oserr_t ArchMmuCommitVirtualPage(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddresses,
        _In_  int            pageCount,
        _Out_ int*           pagesComittedOut) {
    printf("ArchMmuCommitVirtualPage()\n");
    assert_non_null(memorySpace);
    assert_int_not_equal(startAddress, 0);
    assert_non_null(physicalAddresses);
    assert_int_not_equal(pageCount, 0);
    assert_non_null(pagesComittedOut);
    return OS_EOK;
}

oserr_t ArchMmuClearVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       freedAddresses,
        _Out_ int*           freedAddressesCountOut,
        _Out_ int*           pagesClearedOut) {
    printf("ArchMmuClearVirtualPages()\n");
    assert_non_null(memorySpace);
    assert_int_not_equal(startAddress, 0);
    assert_non_null(freedAddresses);
    assert_int_not_equal(pageCount, 0);
    assert_non_null(freedAddressesCountOut);
    assert_non_null(pagesClearedOut);
    return OS_EOK;
}

oserr_t ArchMmuVirtualToPhysical(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       physicalAddressValues,
        _Out_ int*           pagesRetrievedOut) {
    printf("ArchMmuVirtualToPhysical()\n");
    assert_non_null(memorySpace);
    assert_int_not_equal(startAddress, 0);
    assert_int_not_equal(pageCount, 0);
    assert_non_null(physicalAddressValues);
    assert_non_null(pagesRetrievedOut);
    return OS_EOK;
}

// Mocks from ms_allocations
oserr_t MSAllocationCreate(
        _In_ MemorySpace_t* memorySpace,
        _In_ uuid_t         shmTag,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags) {
    printf("MSAllocationCreate()\n");
    assert_non_null(memorySpace);
    assert_int_not_equal(address, 0);
    assert_int_not_equal(length, 0);
    assert_int_not_equal(flags, 0);
    return OS_EOK;
}

oserr_t MSAllocationFree(
        _In_  struct MSContext*     context,
        _In_  vaddr_t               address,
        _In_  size_t                length,
        _Out_ struct MSAllocation** clonedFrom) {
    printf("MSAllocationFree()\n");
    assert_non_null(context);
    assert_int_not_equal(address, 0);
    assert_int_not_equal(length, 0);
    assert_non_null(clonedFrom);
    return OS_EOK;
}

struct MSAllocation* MSAllocationAcquire(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address) {
    printf("MSAllocationAcquire()\n");
    assert_non_null(context);
    assert_int_not_equal(address, 0);
    return NULL;
}

oserr_t MSAllocationRelease(
        _In_ struct MSContext*    context,
        _In_ struct MSAllocation* allocation) {
    return OS_EOK;
}

struct MSAllocation* MSAllocationLookup(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address) {
    printf("MSAllocationLookup()\n");
    assert_non_null(context);
    if (g_testContext.MSAllocationLookup.CheckAddress) {
        assert_int_equal(address, g_testContext.MSAllocationLookup.ExpectedAddress);
    }
    g_testContext.MSAllocationLookup.Calls++;
    return g_testContext.MSAllocationLookup.ReturnValue;
}

oserr_t MSAllocationLink(
        _In_ struct MSContext*    context,
        _In_ vaddr_t              address,
        _In_ struct MSAllocation* link) {
    printf("MSAllocationLink()\n");
    assert_non_null(context);
    assert_int_not_equal(address, 0);
    assert_non_null(link);
    return OS_EOK;
}

// Mocks from dynamic_pool
uintptr_t DynamicMemoryPoolAllocate(
        _In_ DynamicMemoryPool_t* pool,
        _In_ size_t               length) {
    printf("DynamicMemoryPoolAllocate()\n");
    assert_non_null(pool);
    if (g_testContext.DynamicMemoryPoolAllocate.CheckLength) {
        assert_int_equal(length, g_testContext.DynamicMemoryPoolAllocate.ExpectedLength);
    }
    g_testContext.DynamicMemoryPoolAllocate.Calls++;
    return g_testContext.DynamicMemoryPoolAllocate.ReturnValue;
}

void DynamicMemoryPoolFree(
        _In_ DynamicMemoryPool_t* Pool,
        _In_ uintptr_t            Address) {
    printf("DynamicMemoryPoolFree()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Address, 0);
}

uintptr_t StaticMemoryPoolAllocate(
        StaticMemoryPool_t* pool,
        size_t              length) {
    printf("StaticMemoryPoolAllocate()\n");
    assert_non_null(pool);
    if (g_testContext.StaticMemoryPoolAllocate.CheckLength) {
        assert_int_equal(length, g_testContext.StaticMemoryPoolAllocate.ExpectedLength);
    }
    g_testContext.StaticMemoryPoolAllocate.Calls++;
    return g_testContext.StaticMemoryPoolAllocate.ReturnValue;
}

void StaticMemoryPoolFree(
        _In_ StaticMemoryPool_t* Pool,
        _In_ uintptr_t           Address) {
    printf("StaticMemoryPoolFree()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Address, 0);
}
