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
    g_testContext.MemorySpace.Context = &g_testContext.Context;
    g_testContext.MemorySpace.Flags   = MEMORY_SPACE_APPLICATION;

    // Default to a page size of 4KB. If a test needs something else
    // the individual test can change this.
    g_testContext.Machine.MemoryGranularity = 0x1000;
    return 0;
}

void TestMemorySpaceMap_USERSPACE(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_USERSPACE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_USERSPACE. MAPPING_USERSPACE
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values
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
                // Important to use _FIXED flags here, as those will not implicitly
                // imply USERSPACE flag
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_NOCACHE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_NOCACHE. MAPPING_NOCACHE
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_READONLY | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_READONLY. MAPPING_READONLY
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_EXECUTABLE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_EXECUTABLE. MAPPING_EXECUTABLE
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values
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
    // Important to note for this test, is that we are just verifying behaviour of
    // providing the flag. ISDIRTY doesn't actually do anything when mapping, but
    // we still pass it to the underlying platform code.
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_ISDIRTY | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_ISDIRTY. MAPPING_ISDIRTY
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_PERSISTENT | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_PERSISTENT. MAPPING_PERSISTENT
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values.
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
    // OK, so MAPPING_DOMAIN is actually not implemented or supported at this moment,
    // we only provided the flag to see if we could somehow keep this in mind while designing
    // the systems that use memory spaces. To be honest it makes no sense to test this, but we
    // have the test for completeness.
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_DOMAIN | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_DOMAIN. MAPPING_DOMAIN should have an effect
    // on the memory that is allocated for the mapping, and is heavily influenced by the memory
    // setup done in SystemMachine_t. Currently, do a pass-through test, but once this is implemented
    // we will have to do a proper test setup for this.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // 3. ArchMmuReserveVirtualPages
    g_testContext.ArchMmuReserveVirtualPages.ExpectedAttributes = 0;
    g_testContext.ArchMmuReserveVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuReserveVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_COMMIT. When set, we expect ArchMmuSetVirtualPages
    // to be called, and when not set, we expect ArchMmuReserveVirtualPages to be called.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_CLEAN | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_CLEAN. To test this we will pre-allocate
    // the underlying memory, fill it with a pattern, and then test that it got expected'd.
    // MAPPING_CLEAN only triggers when actually commit memory, otherwise it will be stored
    // and evaluated when the memory is lazily allocated. (TODO test this)
    data = test_malloc(GetMemorySpacePageSize()*2);
    assert_non_null(data);
    expected = test_malloc(GetMemorySpacePageSize());
    assert_non_null(data);

    // Align up to next multiple of GetMemorySpacePageSize()
    dataAligned = (void*)((uintptr_t)data + (GetMemorySpacePageSize() - ((uintptr_t)data & (GetMemorySpacePageSize() - 1))));

    // Fill the buffers with expected data
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

    // Expect memory to be expected'd
    assert_memory_equal(dataAligned, expected, GetMemorySpacePageSize());

    // Expected function calls
    assert_int_equal(g_testContext.MSAllocationLookup.Calls, 1);
    assert_int_equal(g_testContext.ArchMmuSetVirtualPages.Calls, 1);

    // Cleanup the allocated buffers so tests are happy
    test_free(data);
    test_free(expected);
}

void TestMemorySpaceMap_STACK(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAddress    = 0x1000000 + GetMemorySpacePageSize();
    g_testContext.ArchMmuSetVirtualPages.CheckAddress       = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedPageCount  = 1;
    g_testContext.ArchMmuSetVirtualPages.CheckPageCount     = true;
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_STACK | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // 3. DynamicMemoryPoolAllocate, let it return a value for the mapping. The mapping
    //    should be 2 pages, as we are only allocating one, but the memory system will
    //    account for an extra due to the STACK flag
    g_testContext.StaticMemoryPoolAllocate.ExpectedLength = GetMemorySpacePageSize() * 2;
    g_testContext.StaticMemoryPoolAllocate.CheckLength    = true;
    g_testContext.StaticMemoryPoolAllocate.ReturnValue    = 0x1000000;

    // Test the basic usage of the MAPPING_STACK. MAPPING_STACK modifies the mapping
    // like the following.
    // 1. When requesting a mapping of a specific length, the mapping actually made will
    //    be one page larger (the initial page).
    // 2. The returned mapping will point to the mapping + length, as mappings go from
    //    top to down.
    // Limitations: When used with MAPPING_VIRTUAL_FIXED the length is adjusted of the
    //              resulting mapping by a memory page, to account for a guard page. That
    //              means the mapping that is to be created *must* be at-least 2 pages long
    //              when using MAPPING_VIRTUAL_FIXED.

    // Test with standard allocation. This should work with just a single page. Let's use
    // fixed physical allocation as we are not testing that here.
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

    // Test with MAPPING_VIRTUAL_FIXED. We need to make sure it errors when mapping is
    // less or equal to a page size, and that it correctly returns the right values when
    // parameters are valid.
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

    // Test with MAPPING_VIRTUAL_FIXED again. But now with correct length, so we
    // get a proper mapping. The trick here is to verify the parameters that ArchMmuSetVirtualPages
    // is invoked with, so we can correctly verify that the mapping *starts* from beyond the
    // guard page
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_TRAPPAGE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_TRAPPAGE. MAPPING_TRAPPAGE
    // is a pass-through flag, which means we just want to verify that if
    // it is explicitly provided, it must be passed to the Arch* calls. When
    // testing specific flags, let's be as minimal invasive as possible, so
    // call it with fixed values
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

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

    // Test that providing no flags for physical placement, will assume default
    // allocation of physical pages.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

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

    // Test the basic usage of the MAPPING_PHYSICAL_FIXED. MAPPING_PHYSICAL_FIXED
    // will pass the provided physical pages (.Pages) to the underlying system. This
    // test ensures that ArchMmuSetVirtualPages is invoked with the expected values.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

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

    // Test the basic usage of the MAPPING_PHYSICAL_CONTIGUOUS. MAPPING_PHYSICAL_CONTIGUOUS
    // will pass the provided physical pages (.Pages) to the underlying system. This
    // test ensures that ArchMmuSetContiguousVirtualPages is invoked with the expected values.
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

    // Expected calls to happen:
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

    // Test the basic usage of the MAPPING_VIRTUAL_GLOBAL. MAPPING_VIRTUAL_GLOBAL
    // will allocate memory from the global kernel memory. We use fixed physical addresses
    // simplify the mocking process.
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

    // Expected calls to happen:
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

    // Test the basic usage of the MAPPING_VIRTUAL_PROCESS. MAPPING_VIRTUAL_PROCESS
    // will allocate memory from process-specific memory. We use fixed physical addresses
    // simplify the mocking process.
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

    // Expected calls to happen:
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

    // Test the basic usage of the MAPPING_VIRTUAL_THREAD. MAPPING_VIRTUAL_THREAD
    // will allocate memory from the local thread memory. We use fixed physical addresses
    // simplify the mocking process.
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

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

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

    // Test the basic usage of the MAPPING_VIRTUAL_FIXED. MAPPING_VIRTUAL_FIXED
    // will use the provided address, assuming it is valid. We also provide fixed
    // physical addreses to simplify mocking

    // Test the zero case where the provided value is 0, we don't allow this
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

    // Test the case where placement flags for virtual is missing, should
    // fail
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
// 5. MemorySpaceQuery (test that we can check we hit guard page)

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
        // Only check this in combination with page count
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
        // only check this in combination with page count
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
