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

static struct __TestContext {
    SystemMachine_t Machine;
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

    // Default to a page size of 4KB. If a test needs something else
    // the individual test can change this.
    g_testContext.Machine.MemoryGranularity = 0x1000;
    return 0;
}

void TestMemorySpaceMap_Happy(void** state)
{
    (void)state;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMemorySpaceMap_Happy, SetupTest),
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

}

void FreePhysicalMemory(
        _In_ int              pageCount,
        _In_ const uintptr_t* pages) {

}

// Mocks from arch/mmu
oserr_t ArchMmuReserveVirtualPages(
        _In_  MemorySpace_t*   memorySpace,
        _In_  vaddr_t startAddress,
        _In_  int              pageCount,
        _In_  unsigned int     attributes,
        _Out_ int*             pagesReservedOut) {

}

oserr_t ArchMmuSetVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddressValues,
        _In_  int            pageCount,
        _In_  unsigned int   attributes,
        _Out_ int*           pagesUpdatedOut) {

}

oserr_t ArchMmuSetContiguousVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  paddr_t        physicalStartAddress,
        _In_  int            pageCount,
        _In_  unsigned int   attributes,
        _Out_ int*           pagesUpdatedOut) {

}

oserr_t ArchMmuCommitVirtualPage(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddresses,
        _In_  int            pageCount,
        _Out_ int*           pagesComittedOut) {

}

oserr_t ArchMmuClearVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       freedAddresses,
        _Out_ int*           freedAddressesCountOut,
        _Out_ int*           pagesClearedOut) {

}

oserr_t ArchMmuVirtualToPhysical(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       physicalAddressValues,
        _Out_ int*           pagesRetrievedOut) {

}

// Mocks from ms_allocations
oserr_t MSAllocationCreate(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags) {

}

oserr_t MSAllocationFree(
        _In_  struct MSContext*     context,
        _In_  vaddr_t*              address,
        _In_  size_t*               size,
        _Out_ struct MSAllocation** clonedFrom) {

}

struct MSAllocation* MSAllocationAcquire(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address) {

}

struct MSAllocation* MSAllocationLookup(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address) {

}

oserr_t MSAllocationLink(
        _In_ struct MSContext*    context,
        _In_ vaddr_t              address,
        _In_ struct MSAllocation* link) {

}

// Mocks from dynamic_pool
uintptr_t DynamicMemoryPoolAllocate(
        _In_ DynamicMemoryPool_t* Pool,
        _In_ size_t               Length) {

}

void DynamicMemoryPoolFree(
        _In_ DynamicMemoryPool_t* Pool,
        _In_ uintptr_t            Address) {

}

uintptr_t StaticMemoryPoolAllocate(
        StaticMemoryPool_t* Pool,
        size_t              Length) {

}

void StaticMemoryPoolFree(
        _In_ StaticMemoryPool_t* Pool,
        _In_ uintptr_t           Address) {

}
