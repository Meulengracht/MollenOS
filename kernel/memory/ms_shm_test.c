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
#include <os/types/memory.h>
#include "private.h"
#include <handle.h>
#include <shm.h>
#include <string.h>
#include <stdio.h>

struct __AllocatePhysicalMemory {
    MOCK_STRUCT_INPUT(size_t, Mask);
    MOCK_STRUCT_INPUT(int, PageCount);
    MOCK_STRUCT_OUTPUT(uintptr_t*, PageValues);
    MOCK_STRUCT_RETURN(oserr_t);
};

struct __CreateHandleCall {
    void* CreatedResource;
    uuid_t ReturnedID;
    bool   ReturnedIDProvided;
};

struct __CreateHandle {
    struct __CreateHandleCall Calls[2];
    int                       CallCount;
};

struct __ArchSHMTypeToPageMask {
    MOCK_STRUCT_INPUT(enum OSMemoryConformity, Conformity);
    MOCK_STRUCT_OUTPUT(size_t, PageMask);
    MOCK_STRUCT_RETURN(oserr_t);
};

struct __MemorySpaceMapCalls {
    MOCK_STRUCT_INPUT(uuid_t, SHMTag);
    MOCK_STRUCT_INPUT(vaddr_t, VirtualStart);
    MOCK_STRUCT_INPUT(paddr_t, PhysicalStart);
    MOCK_STRUCT_INPUT(size_t, Length);
    MOCK_STRUCT_INPUT(size_t, Mask);
    MOCK_STRUCT_INPUT(unsigned int, Flags);
    MOCK_STRUCT_INPUT(unsigned int, Placement);
    MOCK_STRUCT_INPUT(paddr_t*, Pages);
    MOCK_STRUCT_OUTPUT(uintptr_t*, Pages);

    vaddr_t ReturnedMapping;
    bool    ReturnedMappingProvided;

    oserr_t ReturnValue;
};

struct __MemorySpaceMap {
    struct __MemorySpaceMapCalls Calls[16];
    int                          CallCount;
};

struct __MemorySpaceUnmapCall {
    MOCK_STRUCT_INPUT(vaddr_t, Address);
    MOCK_STRUCT_INPUT(vaddr_t, Size);
    oserr_t ReturnValue;
};

struct __MemorySpaceUnmap {
    struct __MemorySpaceUnmapCall Calls[16];
    int                           CallCount;
};

struct __GetMemorySpaceMapping {
    MOCK_STRUCT_INPUT(vaddr_t, Address);
    MOCK_STRUCT_INPUT(int, PageCount);
    MOCK_STRUCT_OUTPUT(uintptr_t*, PageValues);
    MOCK_STRUCT_RETURN(oserr_t);
};

struct __AreMemorySpacesRelated {
    MOCK_STRUCT_RETURN(oserr_t);
};

struct __AcquireHandleOfType {
    uuid_t       ExpectedID;
    bool         CheckID;
    HandleType_t ExpectedType;
    bool         CheckType;
    void*        Resource;
    bool         ResourceProvided;
    int          Calls;
};

struct __LookupHandleOfType {
    uuid_t       ExpectedID;
    bool         CheckID;
    HandleType_t ExpectedType;
    bool         CheckType;
    MOCK_STRUCT_RETURN(void*);
};

DEFINE_TEST_CONTEXT({
    SystemMachine_t  Machine;
    MemorySpace_t    MemorySpace;
    struct MSContext Context;
    void*            TestBuffers[16];
    int              TestBufferCount;

    MOCK_STRUCT_FUNC(AllocatePhysicalMemory);
    MOCK_STRUCT_FUNC(CreateHandle);
    MOCK_STRUCT_FUNC(ArchSHMTypeToPageMask);
    MOCK_STRUCT_FUNC(MemorySpaceMap);
    MOCK_STRUCT_FUNC(MemorySpaceUnmap);
    MOCK_STRUCT_FUNC(GetMemorySpaceMapping);
    MOCK_STRUCT_FUNC(AreMemorySpacesRelated);
    MOCK_STRUCT_FUNC(AcquireHandleOfType);
    MOCK_STRUCT_FUNC(LookupHandleOfType);
});

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

void TeardownTest(void** state) {
    (void)state;

    // Cleanup the allocated resources manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    for (int i = 0; i < g_testContext.CreateHandle.CallCount; i++) {
        test_free(g_testContext.CreateHandle.Calls[i].CreatedResource);
    }

    for (int i = 0; i < g_testContext.TestBufferCount; i++) {
        test_free(g_testContext.TestBuffers[i]);
    }
}

void* __TestAllocPage(size_t length)
{
    size_t    pageSize = GetMemorySpacePageSize();
    void*     buffer;
    uintptr_t address;

    buffer = test_malloc(length + pageSize);
    address = (uintptr_t)buffer;
    if (address & (pageSize - 1)) {
        address += pageSize - (address & (pageSize - 1));
    }
    g_testContext.TestBuffers[g_testContext.TestBufferCount++] = buffer;
    return (void*)address;
}

void TestSHMCreate_DEVICE(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    (void)state;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. ArchSHMTypeToPageMask, this makes a bit more sense to check, that
    //    we indeed have the expected type, it should be passed through
    g_testContext.ArchSHMTypeToPageMask.ExpectedConformity = OSMEMORYCONFORMITY_LEGACY;
    g_testContext.ArchSHMTypeToPageMask.CheckConformity    = true;
    g_testContext.ArchSHMTypeToPageMask.PageMask           = 0xFFFFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided   = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue        = OS_EOK;

    // 3. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_NOCACHE | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                .Flags = SHM_DEVICE,
                .Conformity = OSMEMORYCONFORMITY_LEGACY,
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_IPC(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    void*       kernelMapping;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap x2, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region

    // Userspace mapping
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x1000000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // Kernel mapping
    g_testContext.MemorySpaceMap.Calls[1].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[1].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[1].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT;
    g_testContext.MemorySpaceMap.Calls[1].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedPlacement       = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL;
    g_testContext.MemorySpaceMap.Calls[1].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMapping         = 0x40000;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_IPC,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x1000000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // Before calling SHMKernelMapping, we must set up the LookupHandleOfType call
    g_testContext.LookupHandleOfType.ExpectedID   = 1;
    g_testContext.LookupHandleOfType.CheckID      = true;
    g_testContext.LookupHandleOfType.ExpectedType = HandleTypeSHM;
    g_testContext.LookupHandleOfType.CheckType    = true;
    g_testContext.LookupHandleOfType.ReturnValue  = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // Verify the kernel mapping
    oserr = SHMKernelMapping(handle.ID, &kernelMapping);
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(kernelMapping, 0x40000);
    TeardownTest(state);
}

void TestSHMCreate_TRAP(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_TRAPPAGE | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_TRAP,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_REGULAR(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_COMMIT(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags  = SHM_COMMIT,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_CLEAN(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_CLEAN | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags  = SHM_CLEAN,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_PRIVATE(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // 3. AreMemorySpacesRelated
    g_testContext.AreMemorySpacesRelated.ReturnValue = OS_EUNKNOWN;

    // 4. AcquireHandleOfType
    //    set resource after SHMCreate

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags  = SHM_PRIVATE,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // Update the resource that should be returned by AcquireHandleOfType & LookupHandleOfType
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // When stuff is private, we expect some functions to return an error
    // when invoked.
    oserr = SHMAttach(handle.ID, &otherHandle);
    assert_int_equal(oserr, OS_EPERMISSIONS);
    TeardownTest(state);
}

void TestSHMCreate_CONFORM(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping

    // 2. ArchSHMTypeToPageMask, this makes a bit more sense to check, that
    //    we indeed have the expected type, it should be passed through
    g_testContext.ArchSHMTypeToPageMask.ExpectedConformity = OSMEMORYCONFORMITY_BITS32;
    g_testContext.ArchSHMTypeToPageMask.CheckConformity    = true;
    g_testContext.ArchSHMTypeToPageMask.PageMask           = 0xFFFFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided   = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue        = OS_EOK;

    // 3. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedMask            = 0xFFFFFFFF;
    g_testContext.MemorySpaceMap.Calls[0].CheckMask               = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_COMMIT | SHM_CONFORM,
                    .Conformity = OSMEMORYCONFORMITY_BITS32,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(handle.Buffer, 0x10000);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMExport_Simple(void** state)
{
    oserr_t           oserr;
    SHMHandle_t       handle;
    paddr_t           page = 0x10000;
    struct SHMBuffer* buffer;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1000000;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = 1;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page;
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1000000,
            0x1000,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1000000);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // Verify the SHM buffer structure, that it looks like we expect
    buffer = g_testContext.CreateHandle.Calls[0].CreatedResource;
    assert_non_null(buffer);
    assert_true(buffer->Exported);
    assert_int_equal(buffer->Offset, 0);
    assert_int_equal(buffer->PageCount, 1);
    assert_int_equal(buffer->Pages[0], page);
    TeardownTest(state);
}

void TestSHMExport_NotPageAligned(void** state)
{
    oserr_t           oserr;
    SHMHandle_t       handle;
    int               pageCount = 9;
    paddr_t           page[9] = {
            0x10856,
            0x14000,
            0x15000,
            0x16000,
            0x17000,
            0x1C000,
            0x1A000,
            0x20000,
            0x2A000,
    };
    struct SHMBuffer* buffer;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // Verify the SHM buffer structure, that it looks like we expect
    buffer = g_testContext.CreateHandle.Calls[0].CreatedResource;
    assert_non_null(buffer);
    assert_true(buffer->Exported);
    assert_int_equal(buffer->Offset, 0x856);
    assert_int_equal(buffer->PageCount, 9);
    for (int i = 0; i < pageCount; i++) {
        assert_int_equal(buffer->Pages[i], page[i] & 0xFFFFF000);
    }
    TeardownTest(state);
}

void TestSHMExport_PRIVATE(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;
    paddr_t     page = 0x10000;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1000000;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = 1;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page;
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1000000,
            0x1000,
            SHM_PRIVATE,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1000000);
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // 3. AcquireHandleOfType
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 4. AreMemorySpacesRelated
    g_testContext.AreMemorySpacesRelated.ReturnValue = OS_EUNKNOWN;

    oserr = SHMAttach(handle.ID, &otherHandle);
    assert_int_equal(oserr, OS_EPERMISSIONS);
    TeardownTest(state);
}

void TestSHMConform_IsConformed(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t conformedHandle;
    int         pageCount = 9;
    paddr_t     page[9] = {
            0x10856,
            0x14000,
            0x15000,
            0x16000,
            0x17000,
            0x1C000,
            0x1A000,
            0x20000,
            0x2A000,
    };

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 1);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // 3. AcquireHandleOfType. We will be acquiring the original buffer created.
    // This will be called twice in this case. Once during SG creation, and once
    // when we decide to use the original buffer.
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 4. LookupHandleOfType
    g_testContext.LookupHandleOfType.ExpectedID   = 1;
    g_testContext.LookupHandleOfType.CheckID      = true;
    g_testContext.LookupHandleOfType.ExpectedType = HandleTypeSHM;
    g_testContext.LookupHandleOfType.CheckType    = true;
    g_testContext.LookupHandleOfType.ReturnValue  = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 5. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 6. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = (0x8400 + 0x856);
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE | MAPPING_READONLY;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x40000000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // We fake that the buffer is conformed. It does not matter which conformity
    // we test with, as we mock the translation call. In this case the PageMask
    // will be 0xFFFFF, which will cover our pages.
    oserr = SHMConform(
            handle.ID,
            &(SHMConformityOptions_t)  {
                .BufferAlignment = 0,
                .Conformity = OSMEMORYCONFORMITY_LOW,
            },
            0,
            SHM_ACCESS_READ,
            0,
            handle.Length,
            &conformedHandle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(conformedHandle.ID, 1);
    assert_int_equal(conformedHandle.SourceID, UUID_INVALID);
    assert_int_equal(conformedHandle.SourceFlags, 0);
    assert_ptr_equal(conformedHandle.Buffer, 0x40000856);
    assert_int_equal(conformedHandle.Capacity, 0x8400);
    assert_int_equal(conformedHandle.Length, 0x8400);
    assert_int_equal(conformedHandle.Offset, 0);

    // When a handle is already conformed, SHMConform will just attach
    // and map. So verify this happened as we expected
    // Ensure the right number of calls were made.
    assert_int_equal(g_testContext.CreateHandle.CallCount, 1);
    assert_int_equal(g_testContext.ArchSHMTypeToPageMask.Calls, 1);
    assert_int_equal(g_testContext.AcquireHandleOfType.Calls, 2);
    assert_int_equal(g_testContext.LookupHandleOfType.Calls, 1);
    assert_int_equal(g_testContext.GetMemorySpaceMapping.Calls, 1);
    assert_int_equal(g_testContext.MemorySpaceMap.CallCount, 1);
    TeardownTest(state);
}

void TestSHMConform_NotAlignmentConformed(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t conformedHandle;
    int         pageCount = 9;
    paddr_t     page[9] = {
            0x1000856,
            0x1400000,
            0x1500000,
            0x1600000,
            0x1700000,
            0x1C00000,
            0x1A00000,
            0x2000000,
            0x2A00000,
    };

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[0].ReturnedID         = 0x10;
    g_testContext.CreateHandle.Calls[0].ReturnedIDProvided = true;

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 0x10);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // 3. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[1].ReturnedID         = 0x20;
    g_testContext.CreateHandle.Calls[1].ReturnedIDProvided = true;

    // 4. AcquireHandleOfType. We will be acquiring the original buffer created.
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 5. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 6. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 0x20;
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x83C0; // 0x8400-64
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x40000000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // We fake that the buffer is conformed. It does not matter which conformity
    // we test with, as we mock the translation call. In this case the PageMask
    // will be 0xFFFFF, which will cover our pages.
    oserr = SHMConform(
            handle.ID,
            &(SHMConformityOptions_t)  {
                    .BufferAlignment = 128,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
            },
            0,
            SHM_ACCESS_READ,
            64,
            handle.Length,
            &conformedHandle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(conformedHandle.ID, 0x20);
    assert_int_equal(conformedHandle.SourceID, 0x10); // This is now cloned
    assert_int_equal(conformedHandle.SourceFlags, 0);
    assert_ptr_equal(conformedHandle.Buffer, 0x40000000);
    assert_int_equal(conformedHandle.Capacity, 0x83C0);
    assert_int_equal(conformedHandle.Length, 0x83C0);
    assert_int_equal(conformedHandle.Offset, 64);

    // When a handle is already conformed, SHMConform will just attach
    // and map. So verify this happened as we expected
    // Ensure the right number of calls were made.
    assert_int_equal(g_testContext.CreateHandle.CallCount, 2);
    assert_int_equal(g_testContext.ArchSHMTypeToPageMask.Calls, 1);
    assert_int_equal(g_testContext.AcquireHandleOfType.Calls, 1);
    assert_int_equal(g_testContext.GetMemorySpaceMapping.Calls, 1);
    assert_int_equal(g_testContext.MemorySpaceMap.CallCount, 1);
    TeardownTest(state);
}

void TestSHMConform_NotMemoryConformed(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t conformedHandle;
    int         pageCount = 9;
    paddr_t     page[9] = {
            0x1000856,
            0x1400000,
            0x1500000,
            0x1600000,
            0x1700000,
            0x1C00000,
            0x1A00000,
            0x2000000,
            0x2A00000,
    };

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[0].ReturnedID         = 0x10;
    g_testContext.CreateHandle.Calls[0].ReturnedIDProvided = true;

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 0x10);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // 3. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[1].ReturnedID         = 0x20;
    g_testContext.CreateHandle.Calls[1].ReturnedIDProvided = true;

    // 4. AcquireHandleOfType. We will be acquiring the original buffer created.
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 5. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 6. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 0x20;
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x8400;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x40000000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // We fake that the buffer is conformed. It does not matter which conformity
    // we test with, as we mock the translation call. In this case the PageMask
    // will be 0xFFFFF, which will cover our pages.
    oserr = SHMConform(
            handle.ID,
            &(SHMConformityOptions_t)  {
                    .BufferAlignment = 0,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
            },
            0,
            SHM_ACCESS_READ,
            0,
            handle.Length,
            &conformedHandle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(conformedHandle.ID, 0x20);
    assert_int_equal(conformedHandle.SourceID, 0x10); // This is now cloned
    assert_int_equal(conformedHandle.SourceFlags, 0);
    assert_ptr_equal(conformedHandle.Buffer, 0x40000000);
    assert_int_equal(conformedHandle.Capacity, 0x8400);
    assert_int_equal(conformedHandle.Length, 0x8400);
    assert_int_equal(conformedHandle.Offset, 0);

    // When a handle is already conformed, SHMConform will just attach
    // and map. So verify this happened as we expected
    // Ensure the right number of calls were made.
    assert_int_equal(g_testContext.CreateHandle.CallCount, 2);
    assert_int_equal(g_testContext.ArchSHMTypeToPageMask.Calls, 2);
    assert_int_equal(g_testContext.AcquireHandleOfType.Calls, 1);
    assert_int_equal(g_testContext.GetMemorySpaceMapping.Calls, 1);
    assert_int_equal(g_testContext.MemorySpaceMap.CallCount, 1);
    TeardownTest(state);
}

void TestSHMConform_NotConformedFilledOnCreation(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t conformedHandle;
    int         pageCount = 9;
    paddr_t     page[9] = {
            0x1000856,
            0x1400000,
            0x1401000,
            0x1402000,
            0x1403000,
            0x1404000,
            0x1405000,
            0x2000000,
            0x2001000,
    };

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[0].ReturnedID         = 0x10;
    g_testContext.CreateHandle.Calls[0].ReturnedIDProvided = true;

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 0x10);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // 3. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[1].ReturnedID         = 0x20;
    g_testContext.CreateHandle.Calls[1].ReturnedIDProvided = true;

    // 4. AcquireHandleOfType. We will be acquiring the original buffer created.
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 5. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 6. MemorySpaceMap, the first call is to allocate the entire cloned buffer. Because
    //    we want to verify the contents, we allocate a buffer which will be used for filling
    //    data into by SHM_CONFORM_FILL_ON_CREATION.
    void* conformedData = __TestAllocPage(0x8400);
    assert_non_null(conformedData);
    memset(conformedData, 0, 0x8400);

    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 0x20;
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x8400;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = (vaddr_t)conformedData;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // 7-9. MemorySpaceMap+MemorySpaceUnmap, the next 3 calls is to individually map SG segments
    //      and copying data to these buffers, then unmapping them
    void*  buffers[3] = { NULL };
    size_t lengths[3] = { 0x1000, 0x6000, 0x2000 };
    size_t offsets[3] = { 0x856, 0, 0 };
    for (int i = 0; i < 3; i++) {
        buffers[i] = __TestAllocPage(lengths[i]);
        assert_non_null(buffers[i]);

        memset(buffers[i], 0x1 + (0x1 * i), lengths[i]);

        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedSHMTag          = 0;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckSHMTag             = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedLength          = (lengths[i] - offsets[i]);
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckLength             = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_READONLY;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckFlags              = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedPlacement       = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_CONTIGUOUS;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckPlacement          = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnedMapping         = (vaddr_t)buffers[i];
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnedMappingProvided = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnValue             = OS_EOK;

        g_testContext.MemorySpaceUnmap.Calls[i].ExpectedAddress = (vaddr_t)buffers[i];
        g_testContext.MemorySpaceUnmap.Calls[i].CheckAddress = true;
        g_testContext.MemorySpaceUnmap.Calls[i].ExpectedSize = (lengths[i] - offsets[i]);
        g_testContext.MemorySpaceUnmap.Calls[i].CheckSize = true;
        g_testContext.MemorySpaceUnmap.Calls[i].ReturnValue = OS_EOK;
    }

    // We fake that the buffer is conformed. It does not matter which conformity
    // we test with, as we mock the translation call. In this case the PageMask
    // will be 0xFFFFF, which will cover our pages.
    oserr = SHMConform(
            handle.ID,
            &(SHMConformityOptions_t)  {
                    .BufferAlignment = 0,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
            },
            SHM_CONFORM_FILL_ON_CREATION,
            SHM_ACCESS_READ,
            0,
            handle.Length,
            &conformedHandle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(conformedHandle.ID, 0x20);
    assert_int_equal(conformedHandle.SourceID, 0x10); // This is now cloned
    assert_int_equal(conformedHandle.SourceFlags, SHM_CONFORM_FILL_ON_CREATION);
    assert_ptr_equal(conformedHandle.Buffer, conformedData);
    assert_int_equal(conformedHandle.Capacity, 0x8400);
    assert_int_equal(conformedHandle.Length, 0x8400);
    assert_int_equal(conformedHandle.Offset, 0);

    // Verify data-integrity of the cloned data
    uintptr_t cfAddress = (uintptr_t)conformedData;
    assert_memory_equal(cfAddress, buffers[0], lengths[0] - offsets[0]);
    cfAddress += lengths[0] - offsets[0];

    assert_memory_equal(cfAddress, buffers[1], lengths[1] - offsets[1]);
    cfAddress += lengths[1] - offsets[1];

    assert_memory_equal(cfAddress, buffers[2], conformedHandle.Length - (0x7000 - 0x856));

    // When a handle is already conformed, SHMConform will just attach
    // and map. So verify this happened as we expected
    // Ensure the right number of calls were made.
    assert_int_equal(g_testContext.CreateHandle.CallCount, 2);
    assert_int_equal(g_testContext.ArchSHMTypeToPageMask.Calls, 2);
    assert_int_equal(g_testContext.AcquireHandleOfType.Calls, 1);
    assert_int_equal(g_testContext.GetMemorySpaceMapping.Calls, 1);
    assert_int_equal(g_testContext.MemorySpaceMap.CallCount, 4);
    assert_int_equal(g_testContext.MemorySpaceUnmap.CallCount, 3);

    TeardownTest(state);
}

void TestSHMConform_NotConformedBackfilledOnUnmap(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t conformedHandle;
    int         pageCount = 9;
    paddr_t     page[9] = {
            0x1000856,
            0x1400000,
            0x1401000,
            0x1402000,
            0x1403000,
            0x1404000,
            0x1405000,
            0x2000000,
            0x2001000,
    };

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[0].ReturnedID         = 0x10;
    g_testContext.CreateHandle.Calls[0].ReturnedIDProvided = true;

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = 0x1596856;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = &page[0];
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    // We use garbage values as the memory contents are not accessed
    oserr = SHMExport(
            (void*)0x1596856,
            0x8400,
            0,
            0,
            &handle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(handle.ID, 0x10);
    assert_int_equal(handle.SourceID, UUID_INVALID);
    assert_int_equal(handle.SourceFlags, 0);
    assert_ptr_equal(handle.Buffer, 0x1596856);
    assert_int_equal(handle.Capacity, 0x8400);
    assert_int_equal(handle.Length, 0x8400);
    assert_int_equal(handle.Offset, 0);

    // 3. AcquireHandleOfType. We will be acquiring the original buffer created.
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 4. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 5. CreateHandle, return a non-standard id as we use it twice
    g_testContext.CreateHandle.Calls[1].ReturnedID         = 0x20;
    g_testContext.CreateHandle.Calls[1].ReturnedIDProvided = true;

    // 6. ArchSHMTypeToPageMask
    g_testContext.ArchSHMTypeToPageMask.PageMask = 0xFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue = OS_EOK;

    // 7. MemorySpaceMap, the first call is to allocate the entire cloned buffer. Because
    //    we want to verify the contents, we allocate a buffer which will be used for filling
    //    data into by SHM_CONFORM_FILL_ON_CREATION.
    void* conformedData = __TestAllocPage(0x8400);
    assert_non_null(conformedData);
    memset(conformedData, 0xFF, 0x8400);

    // We must provide a new set of pages again here
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 0x20;
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x8400;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = (vaddr_t)conformedData;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // We fake that the buffer is conformed. It does not matter which conformity
    // we test with, as we mock the translation call. In this case the PageMask
    // will be 0xFFFFF, which will cover our pages.
    oserr = SHMConform(
            handle.ID,
            &(SHMConformityOptions_t)  {
                    .BufferAlignment = 0,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
            },
            SHM_CONFORM_BACKFILL_ON_UNMAP,
            SHM_ACCESS_READ,
            0,
            handle.Length,
            &conformedHandle
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(conformedHandle.ID, 0x20);
    assert_int_equal(conformedHandle.SourceID, 0x10); // This is now cloned
    assert_int_equal(conformedHandle.SourceFlags, SHM_CONFORM_BACKFILL_ON_UNMAP);
    assert_ptr_equal(conformedHandle.Buffer, conformedData);
    assert_int_equal(conformedHandle.Capacity, 0x8400);
    assert_int_equal(conformedHandle.Length, 0x8400);
    assert_int_equal(conformedHandle.Offset, 0);

    // 8. LookupHandleOfType. __CopyBufferToSource will look up the source handle
    //    when trying to build an SG of the buffer.
    g_testContext.LookupHandleOfType.ExpectedID   = 0x10;
    g_testContext.LookupHandleOfType.CheckID      = true;
    g_testContext.LookupHandleOfType.ExpectedType = HandleTypeSHM;
    g_testContext.LookupHandleOfType.CheckType    = true;
    g_testContext.LookupHandleOfType.ReturnValue  = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 9-11. MemorySpaceMap+MemorySpaceUnmap, the next 3 calls is to individually map SG segments
    //       and copying data to these buffers, then unmapping them
    void*  buffers[3] = { NULL };
    size_t lengths[3] = { 0x1000, 0x6000, 0x2000 };
    size_t offsets[3] = { 0x856, 0, 0 };
    for (int i = 0; i < 3; i++) {
        buffers[i] = __TestAllocPage(lengths[i]);
        assert_non_null(buffers[i]);
        memset(buffers[i], 0, lengths[i]);

        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedSHMTag          = 0;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckSHMTag             = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedLength          = (lengths[i] - offsets[i]);
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckLength             = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedFlags           = MAPPING_COMMIT | MAPPING_PERSISTENT;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckFlags              = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ExpectedPlacement       = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_CONTIGUOUS;
        g_testContext.MemorySpaceMap.Calls[1 + i].CheckPlacement          = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnedMapping         = (vaddr_t)buffers[i];
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnedMappingProvided = true;
        g_testContext.MemorySpaceMap.Calls[1 + i].ReturnValue             = OS_EOK;

        g_testContext.MemorySpaceUnmap.Calls[i].ExpectedAddress = (vaddr_t)buffers[i];
        g_testContext.MemorySpaceUnmap.Calls[i].CheckAddress = true;
        g_testContext.MemorySpaceUnmap.Calls[i].ExpectedSize = (lengths[i] - offsets[i]);
        g_testContext.MemorySpaceUnmap.Calls[i].CheckSize = true;
        g_testContext.MemorySpaceUnmap.Calls[i].ReturnValue = OS_EOK;
    }

    // So now that we detach, it will unmap and destroy the handle.
    // This will spark a lot of actions due to the BACKFILL
    oserr = SHMDetach(&conformedHandle);
    assert_int_equal(oserr, OS_EOK);

    // Verify data-integrity of the cloned data
    uintptr_t cfAddress = (uintptr_t)conformedData;
    assert_memory_equal(cfAddress, (char*)buffers[0] + offsets[0], lengths[0] - offsets[0]);
    cfAddress += lengths[0] - offsets[0];

    assert_memory_equal(cfAddress, (char*)buffers[1] + offsets[1], lengths[1] - offsets[1]);
    cfAddress += lengths[1] - offsets[1];

    assert_memory_equal(cfAddress, (char*)buffers[2] + offsets[2], 0x1C56);

    // When a handle is already conformed, SHMConform will just attach
    // and map. So verify this happened as we expected
    // Ensure the right number of calls were made.
    assert_int_equal(g_testContext.CreateHandle.CallCount, 2);
    assert_int_equal(g_testContext.ArchSHMTypeToPageMask.Calls, 2);
    assert_int_equal(g_testContext.AcquireHandleOfType.Calls, 1);
    assert_int_equal(g_testContext.GetMemorySpaceMapping.Calls, 1);
    assert_int_equal(g_testContext.MemorySpaceMap.CallCount, 4);
    assert_int_equal(g_testContext.MemorySpaceUnmap.CallCount, 4);

    TeardownTest(state);
}

void TestSHMAttach_Simple(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;
    (void)state;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);

    // 3. AcquireHandleOfType
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    oserr = SHMAttach(handle.ID, &otherHandle);
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(otherHandle.ID, 1);
    assert_int_equal(otherHandle.Capacity, 0x1000);
    TeardownTest(state);
}

void TestSHMAttach_PrivateFailed(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_PRIVATE,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);

    // 3. AcquireHandleOfType
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    // 4. AreMemorySpacesRelated
    g_testContext.AreMemorySpacesRelated.ReturnValue = OS_EUNKNOWN;

    oserr = SHMAttach(handle.ID, &otherHandle);
    assert_int_equal(oserr, OS_EPERMISSIONS);
    TeardownTest(state);
}

void TestSHMAttach_InvalidID(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;

    // What we are testing here, is that specifically MemorySpaceMap is invoked
    // with the correct parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_PRIVATE,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);

    oserr = SHMAttach(14, &otherHandle);
    assert_int_equal(oserr, OS_ENOENT);
    TeardownTest(state);
}

static void __CreateRegularSHM(SHMHandle_t* shm, size_t size)
{
    oserr_t oserr;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = size;
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x10000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;
    oserr = SHMCreate(
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = size,
            },
            shm
    );
    assert_int_equal(oserr, OS_EOK);
}

void TestSHMMap_Simple(void** state)
{
    oserr_t     oserr;
    SHMHandle_t shm;

    // Create a normal buffer we can use. It must not be comitted. The SHM
    // is already filled, which we don't want as we are trying to make a separate
    // mapping that is not the original.
    __CreateRegularSHM(&shm, 0x1000);

    // Ensure new mapping
    shm.Buffer = NULL;

    // 1. LookupHandleOfType
    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[1].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[1].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[1].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[1].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED;
    g_testContext.MemorySpaceMap.Calls[1].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMapping         = 0x80000;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnValue             = OS_EOK;

    // Remap, uncommitted.
    oserr = SHMMap(
            &shm,
            0,
            shm.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(shm.Buffer, 0x80000);
    TeardownTest(state);
}

static void __CreateExportedSHM(SHMHandle_t* shm, const void* buffer, size_t size)
{
    int      pageCount;
    paddr_t* pages;
    oserr_t  oserr;
    paddr_t  startAddress = 0x1000000;

    // Create page array
    pageCount = (int)((size + (GetMemorySpacePageSize() - 1)) / GetMemorySpacePageSize());
    pages = test_malloc(pageCount * sizeof(paddr_t));
    assert_non_null(pages);

    for (int i = 0; i < pageCount; i++) {
        pages[i] = startAddress + (i * GetMemorySpacePageSize());
    }

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. GetMemorySpaceMapping
    g_testContext.GetMemorySpaceMapping.ExpectedAddress    = (vaddr_t)buffer;
    g_testContext.GetMemorySpaceMapping.CheckAddress       = true;
    g_testContext.GetMemorySpaceMapping.ExpectedPageCount  = pageCount;
    g_testContext.GetMemorySpaceMapping.CheckPageCount     = true;
    g_testContext.GetMemorySpaceMapping.PageValues         = pages;
    g_testContext.GetMemorySpaceMapping.PageValuesProvided = true;
    g_testContext.GetMemorySpaceMapping.ReturnValue        = OS_EOK;

    oserr = SHMExport(
            (void*)buffer,
            size,
            0,
            0,
            shm
    );

    // free resources before asserting
    test_free(pages);
    assert_int_equal(oserr, OS_EOK);
}

void TestSHMMap_SimpleExported(void** state)
{
    oserr_t     oserr;
    SHMHandle_t shm;
    void*       buffer;
    int         sgCount;
    SHMSG_t*    sg;

    // Allocate a new buffer, with wierd values
    buffer = (void*)0x109586;

    // Create a normal buffer we can use. It must not be comitted. The SHM
    // is already filled, which we don't want as we are trying to make a separate
    // mapping that is not the original.
    __CreateExportedSHM(&shm, buffer, 0xC888);

    // Ensure new mapping
    shm.Buffer = NULL;

    // 1. LookupHandleOfType
    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 2. MemorySpaceMap, this is the most interesting call to check, as that
    //    needs to contain the expected setup for the virtual region
    g_testContext.MemorySpaceMap.Calls[0].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[0].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedLength          = (0xC888 + 0x586);
    g_testContext.MemorySpaceMap.Calls[0].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[0].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[0].ExpectedPlacement       = MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED;
    g_testContext.MemorySpaceMap.Calls[0].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMapping         = 0x20000;
    g_testContext.MemorySpaceMap.Calls[0].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[0].ReturnValue             = OS_EOK;
    oserr = SHMMap(
            &shm,
            0,
            shm.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(shm.Buffer, (0x20000 + 0x586));
    assert_int_equal(shm.Length, 0xC888);

    // Ensure that the SG entry shows up correctly
    oserr = SHMBuildSG(shm.ID, &sgCount, NULL);
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(sgCount, 1);

    sg = test_malloc(sgCount * sizeof(SHMSG_t));
    assert_non_null(sg);

    oserr = SHMBuildSG(shm.ID, &sgCount, sg);
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(sg[0].Address, (0x1000000 + 0x586));
    assert_int_equal(sg[0].Length, 0xCA7A);

    test_free(sg);
    TeardownTest(state);
}

void TestSHMMap_CanCommit(void** state)
{
    oserr_t     oserr;
    SHMHandle_t shm;

    // Create a normal buffer we can use. It must not be comitted.
    __CreateRegularSHM(&shm, 0x1000);

    // 1. LookupHandleOfType
    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 2. AllocatePhysicalMemory
    paddr_t page = 0x25000;
    g_testContext.AllocatePhysicalMemory.PageValues = &page;
    g_testContext.AllocatePhysicalMemory.PageValuesProvided = true;
    g_testContext.AllocatePhysicalMemory.ExpectedPageCount = 1;
    g_testContext.AllocatePhysicalMemory.CheckPageCount = true;
    g_testContext.AllocatePhysicalMemory.ReturnValue = OS_EOK;

    // 3. MemorySpaceMap
    g_testContext.MemorySpaceMap.Calls[1].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[1].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedLength          = 0x1000;
    g_testContext.MemorySpaceMap.Calls[1].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[1].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedPlacement       = MAPPING_VIRTUAL_FIXED | MAPPING_PHYSICAL_FIXED;
    g_testContext.MemorySpaceMap.Calls[1].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMapping         = (vaddr_t)shm.Buffer;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedPages           = &page;
    g_testContext.MemorySpaceMap.Calls[1].CheckPages              = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedVirtualStart    = (vaddr_t)shm.Buffer;
    g_testContext.MemorySpaceMap.Calls[1].CheckVirtualStart       = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnValue             = OS_EOK;

    // Commit entire original mapping
    oserr = SHMMap(
            &shm,
            0,
            shm.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE | SHM_ACCESS_COMMIT
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(shm.Buffer, 0x10000);
    TeardownTest(state);
}

void TestSHMMap_CanRemap(void** state)
{
    oserr_t     oserr;
    SHMHandle_t shm;

    // Create a normal buffer we can use. It must not be comitted. We make it
    // a bit larger than the other tests, so we can move the virtual mapping
    __CreateRegularSHM(&shm, 0x4000);

    // 1. LookupHandleOfType
    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 2. MemorySpaceMap, expect a new mapping of 0x3000 when we map from page 2-4
    g_testContext.MemorySpaceMap.Calls[1].ExpectedSHMTag          = 1; // expect 1
    g_testContext.MemorySpaceMap.Calls[1].CheckSHMTag             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedLength          = 0x3000;
    g_testContext.MemorySpaceMap.Calls[1].CheckLength             = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedFlags           = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    g_testContext.MemorySpaceMap.Calls[1].CheckFlags              = true;
    g_testContext.MemorySpaceMap.Calls[1].ExpectedPlacement       = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS;
    g_testContext.MemorySpaceMap.Calls[1].CheckPlacement          = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMapping         = (vaddr_t)0x4000000;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnValue             = OS_EOK;

    // 3. MemorySpaceUnmap
    g_testContext.MemorySpaceUnmap.Calls[0].ExpectedAddress = 0x10000;
    g_testContext.MemorySpaceUnmap.Calls[0].CheckAddress    = true;
    g_testContext.MemorySpaceUnmap.Calls[0].ExpectedSize    = 0x4000;
    g_testContext.MemorySpaceUnmap.Calls[0].CheckSize       = true;
    g_testContext.MemorySpaceUnmap.Calls[0].ReturnValue     = OS_EOK;

    // Now we remap the original mapping, say we only want half
    oserr = SHMMap(
            &shm,
            6000, // (0x1770) 1800 bytes into page two
            shm.Capacity, // rest of buffer, but we are lazy this should be handled
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(shm.Buffer, 0x4000000);
    assert_int_equal(shm.Length, 0x2890);
    TeardownTest(state);
}

// 4. SHMUnmap
// 5. SHMCommit
// 6. SHMBuildSG
int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestSHMCreate_DEVICE, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_IPC, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_TRAP, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_REGULAR, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_COMMIT, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_CLEAN, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_PRIVATE, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_CONFORM, SetupTest),
            cmocka_unit_test_setup(TestSHMExport_Simple, SetupTest),
            cmocka_unit_test_setup(TestSHMExport_NotPageAligned, SetupTest),
            cmocka_unit_test_setup(TestSHMExport_PRIVATE, SetupTest),
            cmocka_unit_test_setup(TestSHMConform_IsConformed, SetupTest),
            cmocka_unit_test_setup(TestSHMConform_NotAlignmentConformed, SetupTest),
            cmocka_unit_test_setup(TestSHMConform_NotMemoryConformed, SetupTest),
            cmocka_unit_test_setup(TestSHMConform_NotConformedFilledOnCreation, SetupTest),
            cmocka_unit_test_setup(TestSHMConform_NotConformedBackfilledOnUnmap, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_Simple, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_PrivateFailed, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_InvalidID, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_Simple, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_SimpleExported, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_CanCommit, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_CanRemap, SetupTest),
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
    struct __MemorySpaceMapCalls* call =
            &g_testContext.MemorySpaceMap.Calls[g_testContext.MemorySpaceMap.CallCount++];
    printf("MemorySpaceMap()\n");
    assert_non_null(memorySpace);
    assert_non_null(options);
    assert_non_null(mappingOut);

    if (call->CheckSHMTag) {
        assert_int_equal(options->SHMTag, call->ExpectedSHMTag);
    }

    if (call->CheckVirtualStart) {
        assert_int_equal(options->VirtualStart, call->ExpectedVirtualStart);
    }

    if (call->CheckPhysicalStart) {
        assert_int_equal(options->VirtualStart, call->ExpectedPhysicalStart);
    }

    if (call->CheckLength) {
        assert_int_equal(options->Length, call->ExpectedLength);
        // Only check pages array if length was checked as well
        if (call->CheckPages) {
            int count = (int)(options->Length / GetMemorySpacePageSize());
            for (int i = 0; i < count; i++) {
                assert_int_equal(options->Pages[i], call->ExpectedPages[i]);
            }
        } else if (call->PagesProvided) {
            int count = (int)((options->Length + (GetMemorySpacePageSize() - 1)) / GetMemorySpacePageSize());
            for (int i = 0; i < count; i++) {
                options->Pages[i] = call->Pages[i];
            }
        }
    }

    if (call->CheckMask) {
        assert_int_equal(options->Mask, call->ExpectedMask);
    }

    if (call->CheckFlags) {
        assert_int_equal(options->Flags, call->ExpectedFlags);
    }

    if (call->CheckPlacement) {
        assert_int_equal(options->PlacementFlags, call->ExpectedPlacement);
    }

    if (call->ReturnedMappingProvided) {
        *mappingOut = call->ReturnedMapping;
    } else {
        *mappingOut = 0;
    }
    return call->ReturnValue;
}

oserr_t MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    struct __MemorySpaceUnmapCall* call =
            &g_testContext.MemorySpaceUnmap.Calls[g_testContext.MemorySpaceUnmap.CallCount++];
    printf("MemorySpaceUnmap()\n");

    // default sanitize values
    assert_non_null(memorySpace);
    assert_int_not_equal(address, 0);
    assert_int_not_equal(size, 0);

    if (call->CheckAddress) {
        assert_int_equal(address, call->ExpectedAddress);
    }

    if (call->CheckSize) {
        assert_int_equal(size, call->ExpectedSize);
    }
    return call->ReturnValue;
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
    assert_non_null(memorySpace);
    assert_non_null(dmaVectorOut);

    if (g_testContext.GetMemorySpaceMapping.CheckAddress) {
        assert_int_equal(address, g_testContext.GetMemorySpaceMapping.ExpectedAddress);
    }

    if (g_testContext.GetMemorySpaceMapping.CheckPageCount) {
        assert_int_equal(pageCount, g_testContext.GetMemorySpaceMapping.ExpectedPageCount);
        // page count must be supplied for us to enable dma vector out
        if (g_testContext.GetMemorySpaceMapping.PageValuesProvided) {
            for (int i = 0; i < pageCount; i++) {
                dmaVectorOut[i] = g_testContext.GetMemorySpaceMapping.PageValues[i];
            }
        }
    }

    g_testContext.GetMemorySpaceMapping.Calls++;
    return g_testContext.GetMemorySpaceMapping.ReturnValue;
}

oserr_t AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2)
{
    printf("AreMemorySpacesRelated()\n");
    g_testContext.AreMemorySpacesRelated.Calls++;
    return g_testContext.AreMemorySpacesRelated.ReturnValue;
}

uuid_t CreateHandle(
        _In_ HandleType_t       handleType,
        _In_ HandleDestructorFn destructor,
        _In_ void*              resource)
{
    printf("CreateHandle()\n");
    struct __CreateHandleCall* call =
            &g_testContext.CreateHandle.Calls[g_testContext.CreateHandle.CallCount++];

    // Assume all calls made in these tests equal HandleTypeSHM
    // and have non-null destructors
    assert_int_equal(handleType, HandleTypeSHM);
    assert_non_null(destructor);
    assert_non_null(resource);
    call->CreatedResource = resource;
    if (call->ReturnedIDProvided) {
        return call->ReturnedID;
    }
    return 1;
}

oserr_t
DestroyHandle(
        _In_ uuid_t handleId)
{
    printf("DestroyHandle()\n");
    assert_int_not_equal(handleId, UUID_INVALID);
    return OS_EOK;
}

oserr_t AcquireHandleOfType(
        _In_  uuid_t       handleId,
        _In_  HandleType_t handleType,
        _Out_ void**       resourceOut)
{
    printf("AcquireHandleOfType()\n");
    assert_non_null(resourceOut);

    if (g_testContext.AcquireHandleOfType.CheckID) {
        assert_int_equal(handleId, g_testContext.AcquireHandleOfType.ExpectedID);
    }

    if (g_testContext.AcquireHandleOfType.CheckType) {
        assert_int_equal(handleType, g_testContext.AcquireHandleOfType.ExpectedType);
    }

    g_testContext.AcquireHandleOfType.Calls++;
    if (g_testContext.AcquireHandleOfType.ResourceProvided) {
        *resourceOut = g_testContext.AcquireHandleOfType.Resource;
        return OS_EOK;
    } else {
        return OS_ENOENT;
    }
}

void* LookupHandleOfType(
        _In_ uuid_t       handleId,
        _In_ HandleType_t handleType)
{
    printf("LookupHandleOfType()\n");
    if (g_testContext.LookupHandleOfType.CheckID) {
        assert_int_equal(handleId, g_testContext.LookupHandleOfType.ExpectedID);
    }

    if (g_testContext.LookupHandleOfType.CheckType) {
        assert_int_equal(handleType, g_testContext.LookupHandleOfType.ExpectedType);
    }
    g_testContext.LookupHandleOfType.Calls++;
    return g_testContext.LookupHandleOfType.ReturnValue;
}

oserr_t
RegisterHandlePath(
        _In_ uuid_t      handleId,
        _In_ const char* path)
{
    printf("RegisterHandlePath()\n");
    assert_int_not_equal(handleId, UUID_INVALID);
    assert_non_null(path);
    return OS_EOK;
}

oserr_t ArchSHMTypeToPageMask(
        _In_  enum OSMemoryConformity conformity,
        _Out_ size_t*                 pageMaskOut)
{
    printf("ArchSHMTypeToPageMask()\n");
    if (g_testContext.ArchSHMTypeToPageMask.CheckConformity) {
        assert_int_equal(conformity, g_testContext.ArchSHMTypeToPageMask.ExpectedConformity);
    }
    if (g_testContext.ArchSHMTypeToPageMask.PageMaskProvided) {
        *pageMaskOut = g_testContext.ArchSHMTypeToPageMask.PageMask;
    }
    g_testContext.ArchSHMTypeToPageMask.Calls++;
    return g_testContext.ArchSHMTypeToPageMask.ReturnValue;
}
