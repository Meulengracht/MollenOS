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
#include <handle.h>
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

static struct __TestContext {
    SystemMachine_t  Machine;
    MemorySpace_t    MemorySpace;
    struct MSContext Context;

    // Function mocks
    struct __AllocatePhysicalMemory AllocatePhysicalMemory;
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

void TestSHMCreate_DEVICE(void** state)
{

}

void TestSHMCreate_IPC(void** state)
{

}

void TestSHMCreate_TRAP(void** state)
{

}

void TestSHMCreate_REGULAR(void** state)
{

}

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestSHMCreate_DEVICE, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_IPC, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_TRAP, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_REGULAR, SetupTest),
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

MemorySpace_t* GetCurrentMemorySpace(void) {
    return &g_testContext.MemorySpace;
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

void MutexConstruct(Mutex_t* mutex, unsigned int configuration) {
    assert_non_null(mutex);
}

void MutexLock(Mutex_t* mutex) {
    assert_non_null(mutex);
}

void MutexUnlock(Mutex_t* mutex) {
    assert_non_null(mutex);
}
void MutexDestruct(Mutex_t* mutex) {
    assert_non_null(mutex);
}

oserr_t MemorySpaceMap(
        _In_  MemorySpace_t*                memorySpace,
        _In_  struct MemorySpaceMapOptions* options,
        _Out_ vaddr_t*                      mappingOut)
{
    printf("MemorySpaceMap()\n");
    return OS_EOK;
}

oserr_t MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    printf("MemorySpaceUnmap()\n");
    return OS_EOK;
}

oserr_t MemorySpaceCommit(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ uintptr_t*     physicalAddressValues,
        _In_ size_t         size,
        _In_ size_t         pageMask,
        _In_ unsigned int   placementFlags)
{
    printf("MemorySpaceCommit()\n");
    return OS_EOK;
}

oserr_t GetMemorySpaceMapping(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t*     dmaVectorOut)
{
    printf("GetMemorySpaceMapping()\n");
    return OS_EOK;
}

oserr_t AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2)
{
    return OS_EOK;
}

uuid_t CreateHandle(
        _In_ HandleType_t       handleType,
        _In_ HandleDestructorFn destructor,
        _In_ void*              resource)
{

}

oserr_t AcquireHandleOfType(
        _In_  uuid_t       handleId,
        _In_  HandleType_t handleType,
        _Out_ void**       resourceOut)
{

}

void* LookupHandleOfType(
        _In_ uuid_t       handleId,
        _In_ HandleType_t handleType)
{

}

oserr_t ArchSHMTypeToPageMask(
        _In_  unsigned int dmaType,
        _Out_ size_t*      pageMaskOut)
{
    return OS_EOK;
}
