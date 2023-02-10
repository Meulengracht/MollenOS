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

struct __CreateHandle {
    void* CreatedResource;
    int   Calls;
};

struct __ArchSHMTypeToPageMask {
    MOCK_STRUCT_INPUT(int, Type);
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

    vaddr_t ReturnedMapping;
    bool    ReturnedMappingProvided;

    oserr_t ReturnValue;
};

struct __MemorySpaceMap {
    struct __MemorySpaceMapCalls Calls[2];
    int                          CallCount;
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
    MOCK_STRUCT_RETURN(oserr_t);
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

    // Function mocks
    struct __AllocatePhysicalMemory AllocatePhysicalMemory;
    struct __CreateHandle CreateHandle;
    struct __ArchSHMTypeToPageMask ArchSHMTypeToPageMask;
    struct __MemorySpaceMap MemorySpaceMap;
    struct __GetMemorySpaceMapping GetMemorySpaceMapping;
    struct __AreMemorySpacesRelated AreMemorySpacesRelated;
    struct __AcquireHandleOfType AcquireHandleOfType;
    struct __LookupHandleOfType LookupHandleOfType;
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

void TestSHMCreate_DEVICE(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
    (void)state;

    // The following function calls are expected during normal
    // creation:

    // 1. CreateHandle, nothing really we care enough to check here except
    //    that it gets invoked as expected. The default returned value is 1

    // 2. ArchSHMTypeToPageMask, this makes a bit more sense to check, that
    //    we indeed have the expected type, it should be passed through
    g_testContext.ArchSHMTypeToPageMask.ExpectedType     = SHM_TYPE_DRIVER_ISA;
    g_testContext.ArchSHMTypeToPageMask.CheckType        = true;
    g_testContext.ArchSHMTypeToPageMask.PageMask         = 0xFFFFFFFF;
    g_testContext.ArchSHMTypeToPageMask.PageMaskProvided = true;
    g_testContext.ArchSHMTypeToPageMask.ReturnValue      = OS_EOK;

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
                .Type = SHM_TYPE_DRIVER_ISA,
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                .Size = 0x1000,
            },
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_IPC(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
    (void)state;

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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(kernelMapping, 0x40000);
    assert_int_equal(userMapping, 0x1000000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_TRAP(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_REGULAR(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_COMMIT(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_CLEAN(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMCreate_PRIVATE(void** state)
{
    oserr_t oserr;
    void*   kernelMapping = NULL;
    void*   userMapping = NULL;
    uuid_t  shmID;
    size_t  shmSize;
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
            &kernelMapping,
            &userMapping,
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_null(kernelMapping);
    assert_int_equal(userMapping, 0x10000);
    assert_int_equal(shmID, 1);

    // Update the resource that should be returned by AcquireHandleOfType & LookupHandleOfType
    g_testContext.AcquireHandleOfType.Resource = g_testContext.CreateHandle.CreatedResource;
    g_testContext.AcquireHandleOfType.ResourceProvided = true;

    g_testContext.LookupHandleOfType.ReturnValue = g_testContext.CreateHandle.CreatedResource;

    // When stuff is private, we expect some functions to return an error
    // when invoked.
    // 1. SHMAttach
    oserr = SHMAttach(shmID, &shmSize);
    assert_int_equal(oserr, OS_EPERMISSIONS);

    // 2. SHMMap
    oserr = SHMMap(
            &(SHMHandle_t) {
                .ID = shmID
            },
            0,
            0x1000, // must be non-zero
            0
    );
    assert_int_equal(oserr, OS_EPERMISSIONS);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

void TestSHMExport_Simple(void** state)
{
    oserr_t oserr;
    uuid_t  shmID;
    paddr_t page = 0x10000;
    (void)state;

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
            &shmID
    );
    assert_int_equal(oserr, OS_EOK);
    assert_int_equal(shmID, 1);

    // Cleanup the SHM context manually, it's a bit hacky, but to avoid
    // that the test complains about leaking memory.
    test_free(g_testContext.CreateHandle.CreatedResource);
}

// 2. SHMAttach
// 3. SHMMap
// 4. SHMUnmap
// 5. SHMCommit
// 6. SHMBuildSG
// 7. SHMKernelMapping
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
    // Assume all calls made in these tests equal HandleTypeSHM
    // and have non-null destructors
    assert_int_equal(handleType, HandleTypeSHM);
    assert_non_null(destructor);
    assert_non_null(resource);
    g_testContext.CreateHandle.CreatedResource = resource;
    g_testContext.CreateHandle.Calls++;
    return 1;
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

    if (g_testContext.AcquireHandleOfType.ResourceProvided) {
        *resourceOut = g_testContext.AcquireHandleOfType.Resource;
    }

    g_testContext.AcquireHandleOfType.Calls++;
    return g_testContext.AcquireHandleOfType.ReturnValue;
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

oserr_t ArchSHMTypeToPageMask(
        _In_  unsigned int dmaType,
        _Out_ size_t*      pageMaskOut)
{
    printf("ArchSHMTypeToPageMask()\n");
    if (g_testContext.ArchSHMTypeToPageMask.CheckType) {
        assert_int_equal(dmaType, g_testContext.ArchSHMTypeToPageMask.ExpectedType);
    }
    if (g_testContext.ArchSHMTypeToPageMask.PageMaskProvided) {
        *pageMaskOut = g_testContext.ArchSHMTypeToPageMask.PageMask;
    }
    g_testContext.ArchSHMTypeToPageMask.Calls++;
    return g_testContext.ArchSHMTypeToPageMask.ReturnValue;
}
