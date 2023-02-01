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
#include "private.h"
#include <string.h>

static struct __TestContext {
    int                  MSContextAddAllocationCalls;
    int                  MutexLockCalls;
    int                  MutexUnlockCalls;
    struct MSAllocation* Expected;
    struct MSContext     MSContext;
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
    list_construct(&g_testContext.MSContext.Allocations);
    return 0;
}

static void __CleanupAllocation(element_t* element, void* context) {
    (void)context;
    test_free((void*)element);
}

void TestMSAllocationCreate_Happy(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    oserr_t oserr;

    (void)state;

    // Setup expected element
    g_testContext.Expected = &(struct MSAllocation) {
        .MemorySpace = &memorySpace,
        .SHMTag = UUID_INVALID,
        .Address = 0x1000,
        .Length = 0x1000,
        .Flags = 0,
        .References = 1,
        .CloneOf = NULL
    };

    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MSContextAddAllocationCalls, 1);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    // cleanup allocations made
    list_clear(&g_testContext.MSContext.Allocations, __CleanupAllocation, NULL);
}

void TestMSAllocationCreate_MissingContext(void** state)
{
    (void)state;
    oserr_t oserr = MSAllocationCreate(
            &(struct MemorySpace) { },
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MSContextAddAllocationCalls, 0);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationCreate_InvalidMemorySpace(void** state)
{
    (void)state;
    oserr_t oserr = MSAllocationCreate(
            NULL,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MSContextAddAllocationCalls, 0);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationLookup_Happy(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation* allocation;
    oserr_t oserr;

    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // should return the allocation when providing the base address
    allocation = MSAllocationLookup(memorySpace.Context, 0x1000);
    assert_non_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);

    // should return if in range
    allocation = MSAllocationLookup(memorySpace.Context, 0x1400);
    assert_non_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 2);
    assert_int_equal(g_testContext.MutexUnlockCalls, 2);

    // should NOT return if out of range
    allocation = MSAllocationLookup(memorySpace.Context, 0x2000);
    assert_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 3);
    assert_int_equal(g_testContext.MutexUnlockCalls, 3);

    // cleanup allocations made
    list_clear(&g_testContext.MSContext.Allocations, __CleanupAllocation, NULL);
}

void TestMSAllocationLookup_InvalidContext(void** state)
{
    (void)state;
    struct MSAllocation* allocation;

    allocation = MSAllocationLookup(NULL, 0x1000);
    assert_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationAcquire_Happy(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation* allocation;
    oserr_t oserr;

    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // must increase ref count on the allocation
    allocation = MSAllocationAcquire(memorySpace.Context, 0x1000);
    assert_non_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(allocation->References, 2);

    // cleanup allocations made
    list_clear(&g_testContext.MSContext.Allocations, __CleanupAllocation, NULL);
}

void TestMSAllocationAcquire_InvalidContext(void** state)
{
    (void)state;
    struct MSAllocation* allocation;

    allocation = MSAllocationAcquire(NULL, 0x1000);
    assert_null(allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationFree_Simple(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation* clonedFrom;
    vaddr_t startAddress;
    size_t size;
    oserr_t oserr;

    (void)state;

    // Create the clonedFrom we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // Free the entire allocation
    startAddress = 0x1000;
    size = 0x1000;

    // Now let's do a simple free case
    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(size, 0); // 0 bytes left
    assert_int_equal(startAddress, 0x1000); // original mapping started here
    assert_null(clonedFrom);
}

void TestMSAllocationFree_PartialFree(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation* clonedFrom;
    vaddr_t startAddress;
    size_t size;
    oserr_t oserr;

    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x2000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // Free a part of the allocation, since partial frees will
    // always be rounded up by page size, actually 0x1000 will be freed.
    startAddress = 0x1000;
    size = 0xA00;

    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(size, 0x1000); // 0x1000 freed, 0x1000 bytes left
    assert_int_equal(startAddress, 0x1000); // original mapping started here
    assert_null(clonedFrom);

    // Free the remainder of the allocation, which size will be set
    // to by the last call
    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MutexLockCalls, 2);
    assert_int_equal(g_testContext.MutexUnlockCalls, 2);
    assert_int_equal(size, 0); // 0x1000 freed, 0 bytes left
    assert_int_equal(startAddress, 0x1000); // original mapping started here
    assert_null(clonedFrom);
}

void TestMSAllocationFree_MultipleReferencesSimple(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation* clonedFrom;
    vaddr_t startAddress;
    size_t size;
    oserr_t oserr;

    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // must increase ref count on the allocation
    (void)MSAllocationAcquire(memorySpace.Context, 0x1000);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);

    // Free half of allocation
    startAddress = 0x1000;
    size = 0x800;

    // Try to reduce the mapping first, this must fail when
    // there are multiple references
    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EPERMISSIONS);
    assert_int_equal(g_testContext.MutexLockCalls, 2);
    assert_int_equal(g_testContext.MutexUnlockCalls, 2);

    // Free the entire allocation
    startAddress = 0x1000;
    size = 0x1000;

    // Expect nothing really to happen, except that references are reduced
    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EINCOMPLETE);
    assert_int_equal(g_testContext.MutexLockCalls, 3);
    assert_int_equal(g_testContext.MutexUnlockCalls, 3);
    assert_int_equal(size, 0x1000); // all bytes left
    assert_int_equal(startAddress, 0x1000); // original mapping started here

    // Now expect this to actually free the allocation
    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MutexLockCalls, 4);
    assert_int_equal(g_testContext.MutexUnlockCalls, 4);
    assert_int_equal(size, 0); // 0 bytes left
    assert_int_equal(startAddress, 0x1000); // original mapping started here
}

void TestMSAllocationFree_InvalidParams(void** state)
{
    struct MSAllocation* clonedFrom;
    vaddr_t startAddress = 0;
    size_t size = 0;
    oserr_t oserr;

    (void)state;

    oserr = MSAllocationFree(
            NULL,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            NULL,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            NULL,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            NULL
    );
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationFree_InvalidAddress(void** state)
{
    struct MSAllocation* clonedFrom;
    vaddr_t startAddress = 0;
    size_t size = 0;
    oserr_t oserr;

    (void)state;

    oserr = MSAllocationFree(
            &g_testContext.MSContext,
            &startAddress,
            &size,
            &clonedFrom
    );
    assert_int_equal(oserr, OS_ENOENT);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
}

void TestMSAllocationLink_Happy(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    struct MSAllocation allocation;
    oserr_t oserr;

    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationCreate(
            &memorySpace,
            0x1000,
            0x1000,
            0
    );
    assert_int_equal(oserr, OS_EOK);

    // must increase ref count on the allocation
    oserr = MSAllocationLink(memorySpace.Context, 0x1000, &allocation);
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);

    // cleanup allocations made
    list_clear(&g_testContext.MSContext.Allocations, __CleanupAllocation, NULL);
}

void TestMSAllocationLink_InvalidContext(void** state)
{
    oserr_t oserr;
    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationLink(NULL, 0x1000, NULL);
    assert_int_equal(oserr, OS_EINVALPARAMS);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);
}

void TestMSAllocationLink_InvalidAddress(void** state)
{
    MemorySpace_t memorySpace = {
            .Context = &g_testContext.MSContext
    };
    oserr_t oserr;
    (void)state;

    // Create the allocation we will use for testing
    oserr = MSAllocationLink(memorySpace.Context, 0x1000, NULL);
    assert_int_equal(oserr, OS_ENOENT);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMSAllocationCreate_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationCreate_MissingContext, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationCreate_InvalidMemorySpace, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationLookup_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationLookup_InvalidContext, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationAcquire_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationAcquire_InvalidContext, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationFree_Simple, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationFree_PartialFree, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationFree_MultipleReferencesSimple, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationFree_InvalidParams, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationFree_InvalidAddress, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationLink_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationLink_InvalidContext, SetupTest),
            cmocka_unit_test_setup(TestMSAllocationLink_InvalidAddress, SetupTest),
    };
    return cmocka_run_group_tests(tests, Setup, Teardown);
}

// Mock this, called by MSAllocationCreate
void MSContextAddAllocation(struct MSContext* context, struct MSAllocation* allocation) {
    assert_non_null(context);
    assert_non_null(allocation);

    g_testContext.MSContextAddAllocationCalls++;
    list_append(&context->Allocations, &allocation->Header);

    if (g_testContext.Expected != NULL) {
        assert_ptr_equal(allocation->MemorySpace, g_testContext.Expected->MemorySpace);
        assert_int_equal(allocation->SHMTag, g_testContext.Expected->SHMTag);
        assert_int_equal(allocation->Address, g_testContext.Expected->Address);
        assert_int_equal(allocation->Length, g_testContext.Expected->Length);
        assert_int_equal(allocation->Flags, g_testContext.Expected->Flags);
        assert_int_equal(allocation->References, g_testContext.Expected->References);
        assert_ptr_equal(allocation->CloneOf, g_testContext.Expected->CloneOf);
    }
}

void MutexLock(Mutex_t* mutex) {
    assert_non_null(mutex);
    g_testContext.MutexLockCalls++;
}

void MutexUnlock(Mutex_t* mutex) {
    assert_non_null(mutex);
    g_testContext.MutexUnlockCalls++;
}

void* kmalloc(size_t size) {
    return test_malloc(size);
}

void kfree(void* memp) {
    return test_free(memp);
}

size_t GetMemorySpacePageSize(void) {
    return 0x1000;
}
