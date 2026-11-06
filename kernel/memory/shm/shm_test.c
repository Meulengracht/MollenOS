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
 */

#include <testbase.h>
#include <machine.h>
#include <os/types/memory.h>
#include "private.h"
#include "../space/private.h"
#include <handles/handle.h>
#include <shm/shm.h>
#include <ds/streambuffer.h>
#include <os/futex.h>
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

struct __AcquireHandle {
    uuid_t       ExpectedID;
    bool         CheckID;
    HandleType_t ExpectedType;
    bool         CheckType;
    void*        Resource;
    bool         ResourceProvided;
    int          Calls;
};

struct __LookupHandle {
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
    MOCK_STRUCT_FUNC(MemorySpaceMap);
    MOCK_STRUCT_FUNC(MemorySpaceUnmap);
    MOCK_STRUCT_FUNC(GetMemorySpaceMapping);
    MOCK_STRUCT_FUNC(AreMemorySpacesRelated);
    MOCK_STRUCT_FUNC(AcquireHandle);
    MOCK_STRUCT_FUNC(LookupHandle);
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
    g_testContext.MemorySpace.GroupGlobalHandleID = 1;
    g_testContext.MemorySpace.Flags   = MEMORY_SPACE_APPLICATION;

    // Use 4KB pages unless the test sets another size.
    g_testContext.Machine.MemoryGranularity = 0x1000;
    return 0;
}

void TeardownTest(void** state) {
    (void)state;

    // Free test resources here to avoid leak reports.
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

void
TestSHMCreate_RetiredDeviceKind(
    void** state)
{
    SHM_t configuration = {
        .Flags = 0x00000300U,
        .Size = 0x1000
    };
    SHMHandle_t handle;
    SHMHandle_t saved;

    (void)state;
    memset(&handle, 0xa5, sizeof(handle));
    memcpy(&saved, &handle, sizeof(saved));
    assert_int_equal(SHMCreate(&configuration, &handle), OS_ENOTSUPPORTED);
    assert_memory_equal(&handle, &saved, sizeof(handle));
    TeardownTest(state);
}

void TestSHMCreate_IPC(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    void*       kernelMapping;
    _Alignas(streambuffer_t) unsigned char storage[0x1000];

    // IPC creation initializes the stream before making it available.
    // The kernel mapping must point to writable storage.
    memset(storage, 0xa5, sizeof(storage));

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the userspace and kernel mapping parameters.

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
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMapping         = (vaddr_t)storage;
    g_testContext.MemorySpaceMap.Calls[1].ReturnedMappingProvided = true;
    g_testContext.MemorySpaceMap.Calls[1].ReturnValue             = OS_EOK;

    // Check the MemorySpaceMap parameters.
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
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // Set up LookupHandle before calling SHMKernelMapping.
    g_testContext.LookupHandle.ExpectedID   = 1;
    g_testContext.LookupHandle.CheckID      = true;
    g_testContext.LookupHandle.ExpectedType = HandleTypeSHM;
    g_testContext.LookupHandle.CheckType    = true;
    g_testContext.LookupHandle.ReturnValue  = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // Verify the kernel mapping
    oserr = SHMKernelMapping(handle.ID, &kernelMapping);
    assert_int_equal(oserr, OS_EOK);
    assert_ptr_equal(kernelMapping, storage);
    assert_int_equal(((streambuffer_t*)storage)->capacity, sizeof(storage) - sizeof(streambuffer_t));
    TeardownTest(state);
}

void TestSHMCreate_TRAP(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
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
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_REGULAR(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
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
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_COMMIT(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
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
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);
    TeardownTest(state);
}

void TestSHMCreate_CLEAN(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
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

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // 4. AcquireHandle
    //    Set the resource after SHMCreate.

    // Check the MemorySpaceMap parameters.
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
    assert_int_equal(handle.Capacity, 0x1000);
    assert_int_equal(handle.Length, 0x1000);
    assert_int_equal(handle.Offset, 0);

    // Return the created resource from AcquireHandle and LookupHandle.
    g_testContext.AcquireHandle.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandle.ResourceProvided = true;

    g_testContext.LookupHandle.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // An unrelated memory space cannot attach to a private buffer.
    oserr = SHMAttach(handle.ID, &otherHandle);
    assert_int_equal(oserr, OS_EPERMISSIONS);
    TeardownTest(state);
}

void TestSHMAttach_Simple(void** state)
{
    oserr_t     oserr;
    SHMHandle_t handle;
    SHMHandle_t otherHandle;
    (void)state;

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);

    // 3. AcquireHandle
    g_testContext.AcquireHandle.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandle.ResourceProvided = true;

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

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_PRIVATE,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = 0x1000,
            },
            &handle
    );
    assert_int_equal(oserr, OS_EOK);

    // 3. AcquireHandle
    g_testContext.AcquireHandle.Resource = g_testContext.CreateHandle.Calls[0].CreatedResource;
    g_testContext.AcquireHandle.ResourceProvided = true;

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

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Check the MemorySpaceMap parameters.
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

    // Expected calls:

    // 1. CreateHandle returns 1 by default.

    // 2. MemorySpaceMap checks the virtual region parameters.
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

    // Create an uncommitted buffer. The handle includes the original mapping.
    __CreateRegularSHM(&shm, 0x1000);

    // Clear the original address to request a separate mapping.
    shm.Buffer = NULL;

    // 1. LookupHandle
    g_testContext.LookupHandle.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

    // 2. MemorySpaceMap checks the virtual region parameters.
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

void TestSHMMap_CanCommit(void** state)
{
    oserr_t     oserr;
    SHMHandle_t shm;

    // Create an uncommitted buffer.
    __CreateRegularSHM(&shm, 0x1000);

    // 1. LookupHandle
    g_testContext.LookupHandle.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

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

    // Create an uncommitted four-page buffer so the mapping can move.
    __CreateRegularSHM(&shm, 0x4000);

    // 1. LookupHandle
    g_testContext.LookupHandle.ReturnValue = g_testContext.CreateHandle.Calls[0].CreatedResource;

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

    // Remap from offset 6000 to the end of the buffer.
    oserr = SHMMap(
            &shm,
            6000, // 0x1770, within page two
            shm.Capacity, // Length is limited to the remaining buffer.
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
            cmocka_unit_test_setup(TestSHMCreate_RetiredDeviceKind, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_IPC, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_TRAP, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_REGULAR, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_COMMIT, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_CLEAN, SetupTest),
            cmocka_unit_test_setup(TestSHMCreate_PRIVATE, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_Simple, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_PrivateFailed, SetupTest),
            cmocka_unit_test_setup(TestSHMAttach_InvalidID, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_Simple, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_CanCommit, SetupTest),
            cmocka_unit_test_setup(TestSHMMap_CanRemap, SetupTest),
    };
    return cmocka_run_group_tests(tests, Setup, Teardown);
}

void* kmalloc(size_t size) {
    return test_malloc(size);
}

oserr_t
OSFutex(
    _In_ OSFutexParameters_t* parameters,
    _In_ OSAsyncContext_t*    context)
{
    // These SHM tests only initialize streams. They must not block or wake.
    (void)parameters;
    (void)context;
    fail();
    return OS_EUNKNOWN;
}

void kfree(void* memp) {
    return test_free(memp);
}

size_t GetMemorySpacePageSize(void) {
    return g_testContext.Machine.MemoryGranularity;
}

// These legacy tests exercise ordinary SHM. Reserved backing is covered with
// the real memory component by testing/shm_memory_ownership_test.py.
oserr_t
SystemMemoryAllocateDevice(
    _In_    SystemMemory_t*                systemMemory,
    _In_    size_t                         length,
    _In_    size_t                         alignment,
    _In_    size_t                         boundary,
    _InOut_ struct SystemMemoryAllocation* allocation)
{
    (void)systemMemory;
    (void)length;
    (void)alignment;
    (void)boundary;
    (void)allocation;
    return OS_ENOTSUPPORTED;
}

oserr_t
SystemMemoryReleaseAllocation(
    _InOut_ struct SystemMemoryAllocation* allocation)
{
    assert_null(allocation->Owner);
    return OS_EOK;
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
        // Page values require a checked page count.
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

    // Check required arguments.
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
        // Page count must be checked before copying page values.
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
        _In_ HandleDestructorFn grantCleanup,
        _In_ HandleDestructorFn destructor,
        _In_ void*              resource)
{
    printf("CreateHandle()\n");
    struct __CreateHandleCall* call =
            &g_testContext.CreateHandle.Calls[g_testContext.CreateHandle.CallCount++];

    // These tests create HandleTypeSHM handles with non-null destructors.
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

oserr_t AcquireHandle(
        _In_  uuid_t       handleId,
        _In_  HandleType_t handleType,
        _Out_ void**       resourceOut)
{
    printf("AcquireHandle()\n");
    assert_non_null(resourceOut);

    if (g_testContext.AcquireHandle.CheckID) {
        assert_int_equal(handleId, g_testContext.AcquireHandle.ExpectedID);
    }

    if (g_testContext.AcquireHandle.CheckType) {
        assert_int_equal(handleType, g_testContext.AcquireHandle.ExpectedType);
    }

    g_testContext.AcquireHandle.Calls++;
    if (g_testContext.AcquireHandle.ResourceProvided) {
        *resourceOut = g_testContext.AcquireHandle.Resource;
        return OS_EOK;
    } else {
        return OS_ENOENT;
    }
}

void* LookupHandle(
        _In_ uuid_t       handleId,
        _In_ HandleType_t handleType)
{
    printf("LookupHandle()\n");
    if (g_testContext.LookupHandle.CheckID) {
        assert_int_equal(handleId, g_testContext.LookupHandle.ExpectedID);
    }

    if (g_testContext.LookupHandle.CheckType) {
        assert_int_equal(handleType, g_testContext.LookupHandle.ExpectedType);
    }
    g_testContext.LookupHandle.Calls++;
    return g_testContext.LookupHandle.ReturnValue;
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
