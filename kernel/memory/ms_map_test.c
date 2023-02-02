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

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <machine.h>
#include "private.h"
#include <string.h>
#include <stdio.h>

struct __ArchMmuSetVirtualPages {
    // Parameters
    unsigned int ExpectedAttributes;
    bool         CheckAttributes;

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

static struct __TestContext {
    SystemMachine_t           Machine;
    MemorySpace_t             MemorySpace;
    struct MSContext          Context;

    // Function mocks
    struct __ArchMmuSetVirtualPages ArchMmuSetVirtualPages;
    struct __ArchMmuReserveVirtualPages ArchMmuReserveVirtualPages;
    struct __MSAllocationLookup MSAllocationLookup;
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

    // Default to a page size of 4KB. If a test needs something else
    // the individual test can change this.
    g_testContext.Machine.MemoryGranularity = 0x1000;
    return 0;
}

void TestMemorySpaceMap_UserspaceExplicit(void** state)
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

void TestMemorySpaceMap_NoCache(void** state)
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

void TestMemorySpaceMap_ReadOnly(void** state)
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

void TestMemorySpaceMap_Executable(void** state)
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

void TestMemorySpaceMap_IsDirty(void** state)
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

void TestMemorySpaceMap_Persistant(void** state)
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

void TestMemorySpaceMap_Domain(void** state)
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

void TestMemorySpaceMap_Commit(void** state)
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

void TestMemorySpaceMap_GuardPage(void** state)
{
    oserr_t oserr;
    vaddr_t mapping;
    paddr_t page;
    (void)state;

    // Expected calls to happen:
    // 1. MSAllocationLookup, let it return NULL to indicate no existing mapping,
    //    and no parameter mocking neccessary

    // 2. ArchMmuSetVirtualPages.
    g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes = MAPPING_GUARDPAGE | MAPPING_COMMIT;
    g_testContext.ArchMmuSetVirtualPages.CheckAttributes    = true;
    g_testContext.ArchMmuSetVirtualPages.ReturnValue        = OS_EOK;

    // Test the basic usage of the MAPPING_GUARDPAGE.
    mapping = 0x1000000;
    page = 0x10000;
    oserr = MemorySpaceMap(
            &g_testContext.MemorySpace,
            &(struct MemorySpaceMapOptions) {
                    .VirtualStart = mapping,
                    .Pages = &page,
                    .Length = GetMemorySpacePageSize(),
                    .Mask = __MASK,
                    .Flags = MAPPING_GUARDPAGE | MAPPING_COMMIT,
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

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMemorySpaceMap_UserspaceExplicit, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_NoCache, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_ReadOnly, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_Executable, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_IsDirty, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_Persistant, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_Domain, SetupTest),
            cmocka_unit_test_setup(TestMemorySpaceMap_Commit, SetupTest),
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
    assert_int_not_equal(pageMask, 0);
    assert_int_not_equal(pageCount, 0);
    assert_non_null(pages);
    return OS_EOK;
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
    assert_int_not_equal(startAddress, 0);
    assert_non_null(physicalAddressValues);
    assert_int_not_equal(pageCount, 0);
    if (g_testContext.ArchMmuSetVirtualPages.CheckAttributes) {
        assert_int_equal(attributes, g_testContext.ArchMmuSetVirtualPages.ExpectedAttributes);
    }
    assert_non_null(pagesUpdatedOut);
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
    assert_int_not_equal(startAddress, 0);
    assert_int_not_equal(physicalStartAddress, 0);
    assert_int_not_equal(pageCount, 0);
    assert_int_not_equal(attributes, 0);
    assert_non_null(pagesUpdatedOut);
    return OS_EOK;
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
        _In_  vaddr_t*              address,
        _In_  size_t*               size,
        _Out_ struct MSAllocation** clonedFrom) {
    printf("MSAllocationFree()\n");
    assert_non_null(context);
    assert_non_null(address);
    assert_non_null(size);
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
        _In_ DynamicMemoryPool_t* Pool,
        _In_ size_t               Length) {
    printf("DynamicMemoryPoolAllocate()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Length, 0);
    return 0;
}

void DynamicMemoryPoolFree(
        _In_ DynamicMemoryPool_t* Pool,
        _In_ uintptr_t            Address) {
    printf("DynamicMemoryPoolFree()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Address, 0);
}

uintptr_t StaticMemoryPoolAllocate(
        StaticMemoryPool_t* Pool,
        size_t              Length) {
    printf("StaticMemoryPoolAllocate()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Length, 0);
    return 0;
}

void StaticMemoryPoolFree(
        _In_ StaticMemoryPool_t* Pool,
        _In_ uintptr_t           Address) {
    printf("StaticMemoryPoolFree()\n");
    assert_non_null(Pool);
    assert_int_not_equal(Address, 0);
}
