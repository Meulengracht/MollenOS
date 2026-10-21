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
#include <arch/mmu.h>
#include <machine.h>
#include "private.h"
#include <string.h>

/**
 * @brief Synchronous handle table fixture for checking resource ownership.
 */
struct __TestHandle {
    HandleType_t       Type;
    HandleDestructorFn Destructor;
    void*              Resource;
    int                References;
};

static struct __TestContext {
    int                MutexConstructCalls;
    int                MutexLockCalls;
    int                MutexUnlockCalls;
    int                MutexDestructCalls;
    int                DynamicMemoryPoolConstructCalls;
    int                DynamicMemoryPoolFreeCalls;
    int                DynamicMemoryPoolDestroyCalls;
    void*              SkipFree;
    struct __TestHandle Handles[16];
    bool               FailAllocation;
    bool               FailHandle;
    HandleType_t       FailHandleType;
    bool               FailAcquire;
    HandleType_t       FailAcquireType;
    oserr_t            CloneResult;
    int                VirtualSpaceDestroyCalls;
    int                LiveAllocations;
    uuid_t             CurrentHandle;
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
    g_testContext.CurrentHandle = UUID_INVALID;
    return 0;
}

void TestMSContextNew_Happy(void** state)
{
    struct MSContext* context;
    uuid_t            handle;

    handle = MSContextNew();
    assert_int_not_equal(handle, UUID_INVALID);
    context = LookupHandleOfType(handle, HandleTypeMSContext);
    assert_non_null(context);
    // SyncObject and UserCopyLock
    assert_int_equal(g_testContext.MutexConstructCalls, 2);
    assert_int_equal(g_testContext.DynamicMemoryPoolConstructCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 0);
    assert_int_equal(context->SignalHandler, 0);
    assert_int_equal(context->Security.Handle, handle);
    assert_int_equal(atomic_load(&context->Security.Capabilities), 0);

    // delete again, should be OK to invoke this
    assert_int_equal(DestroyHandle(handle), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

void TestMSContextAddAllocation_Happy(void** state)
{
    struct MSContext*   context;
    uuid_t              handle;
    struct MSAllocation allocation = {
            .Address = 0x13444,
            .Length = 0x1000
    };

    handle = MSContextNew();
    context = LookupHandleOfType(handle, HandleTypeMSContext);
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    list_remove(&context->Allocations, &allocation.Header);
    assert_int_equal(DestroyHandle(handle), OS_EOK);
}

void TestMSContextDelete_Happy(void** state)
{
    struct MSContext*   context;
    uuid_t              handle;
    struct MSAllocation allocation = {
            .Header = { .value = &allocation },
            .Address = 0x13444,
            .Length = 0x1000
    };

    handle = MSContextNew();
    context = LookupHandleOfType(handle, HandleTypeMSContext);
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    g_testContext.SkipFree = &allocation;
    assert_int_equal(DestroyHandle(handle), OS_EOK);

    // ensure that DynamicMemoryPoolFree was called once, this means
    // the allocation was freed
    assert_int_equal(g_testContext.MutexDestructCalls, 2);
    assert_int_equal(g_testContext.DynamicMemoryPoolFreeCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolDestroyCalls, 1);
}

static void
__TestGroupLifetime(
    _In_ void** state)
{
    MemorySpace_t* root;
    MemorySpace_t* child;
    MemorySpace_t* sibling;
    uuid_t         rootHandle;
    uuid_t         childHandle;
    uuid_t         siblingHandle;
    uuid_t         groupHandle;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &rootHandle), OS_EOK);
    root = LookupHandleOfType(rootHandle, HandleTypeMemorySpace);
    groupHandle = root->GroupHandle;
    assert_int_equal(root->ParentHandle, UUID_INVALID);
    assert_int_equal(g_testContext.Handles[groupHandle].References, 1);

    g_testContext.CurrentHandle = rootHandle;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &childHandle), OS_EOK);
    child = LookupHandleOfType(childHandle, HandleTypeMemorySpace);
    assert_int_equal(child->GroupHandle, groupHandle);
    assert_int_equal(child->ParentHandle, rootHandle);
    assert_ptr_equal(__MSContextGet(root), __MSContextGet(child));

    g_testContext.CurrentHandle = childHandle;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &siblingHandle), OS_EOK);
    sibling = LookupHandleOfType(siblingHandle, HandleTypeMemorySpace);
    assert_int_equal(sibling->ParentHandle, rootHandle);
    assert_int_equal(sibling->GroupHandle, groupHandle);
    assert_int_equal(g_testContext.Handles[groupHandle].References, 3);
    assert_ptr_equal(MemorySpaceGetSecurityContext(), &__MSContextGet(child)->Security);

    // Dropping the original owner's reference cannot retire inherited tables.
    assert_int_equal(DestroyHandle(rootHandle), OS_EINCOMPLETE);
    assert_int_equal(DestroyHandle(childHandle), OS_EOK);
    assert_int_equal(g_testContext.Handles[groupHandle].References, 2);
    assert_int_equal(DestroyHandle(siblingHandle), OS_EOK);
    assert_int_equal(g_testContext.Handles[groupHandle].References, 0);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 3);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestRetainedGroup(
    _In_ void** state)
{
    MemorySpace_t*    space;
    struct MSContext* context;
    uuid_t            handle;
    uuid_t            group;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &handle), OS_EOK);
    space = LookupHandleOfType(handle, HandleTypeMemorySpace);
    group = space->GroupHandle;
    assert_int_equal(AcquireHandleOfType(group, HandleTypeMSContext, (void**)&context), OS_EOK);
    assert_int_equal(DestroyHandle(handle), OS_EOK);
    assert_ptr_equal(LookupHandleOfType(group, HandleTypeMSContext), context);
    assert_int_equal(context->Security.Handle, group);
    assert_int_equal(g_testContext.LiveAllocations, 1);
    assert_int_equal(DestroyHandle(group), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestKernelSpace(
    _In_ void** state)
{
    MemorySpace_t* space;
    uuid_t         handle;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_INHERIT, &handle), OS_EOK);
    space = LookupHandleOfType(handle, HandleTypeMemorySpace);
    assert_int_equal(space->GroupHandle, UUID_INVALID);
    assert_null(__MSContextGet(space));
    g_testContext.CurrentHandle = handle;
    assert_null(MemorySpaceGetSecurityContext());
    assert_int_equal(DestroyHandle(handle), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestGroupAllocationFailure(
    _In_ void** state)
{
    (void)state;
    g_testContext.FailAllocation = true;
    assert_int_equal(MSContextNew(), UUID_INVALID);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestGroupHandleFailure(
    _In_ void** state)
{
    uuid_t handle;

    (void)state;
    g_testContext.FailHandle = true;
    g_testContext.FailHandleType = HandleTypeMSContext;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &handle), OS_EOOM);
    assert_int_equal(g_testContext.LiveAllocations, 0);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 0);
}

static void
__TestCloneFailure(
    _In_ void** state)
{
    uuid_t root;
    uuid_t child;
    uuid_t group;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &root), OS_EOK);
    group = ((MemorySpace_t*)LookupHandleOfType(root, HandleTypeMemorySpace))->GroupHandle;
    g_testContext.CurrentHandle = root;
    g_testContext.CloneResult = OS_EOOM;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &child), OS_EOOM);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(g_testContext.Handles[group].References, 1);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 0);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);

    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &root), OS_EOOM);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestSpaceHandleFailure(
    _In_ void** state)
{
    uuid_t root;
    uuid_t group;
    uuid_t handle;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &root), OS_EOK);
    group = ((MemorySpace_t*)LookupHandleOfType(root, HandleTypeMemorySpace))->GroupHandle;
    g_testContext.CurrentHandle = root;
    g_testContext.FailHandle = true;
    g_testContext.FailHandleType = HandleTypeMemorySpace;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &handle), OS_EOOM);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(g_testContext.Handles[group].References, 1);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);

    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &handle), OS_EOOM);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 3);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

static void
__TestGroupAcquireFailure(
    _In_ void** state)
{
    uuid_t root;
    uuid_t child;

    (void)state;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION, &root), OS_EOK);
    g_testContext.CurrentHandle = root;
    g_testContext.FailAcquire = true;
    g_testContext.FailAcquireType = HandleTypeMSContext;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &child), OS_ENOENT);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    g_testContext.FailAcquireType = HandleTypeMemorySpace;
    assert_int_equal(CreateMemorySpace(MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT, &child), OS_ENOENT);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMSContextNew_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSContextAddAllocation_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSContextDelete_Happy, SetupTest),
            cmocka_unit_test_setup(__TestGroupLifetime, SetupTest),
            cmocka_unit_test_setup(__TestRetainedGroup, SetupTest),
            cmocka_unit_test_setup(__TestKernelSpace, SetupTest),
            cmocka_unit_test_setup(__TestGroupAllocationFailure, SetupTest),
            cmocka_unit_test_setup(__TestGroupHandleFailure, SetupTest),
            cmocka_unit_test_setup(__TestCloneFailure, SetupTest),
            cmocka_unit_test_setup(__TestSpaceHandleFailure, SetupTest),
            cmocka_unit_test_setup(__TestGroupAcquireFailure, SetupTest),
    };
    return cmocka_run_group_tests(tests, Setup, Teardown);
}

void MutexConstruct(Mutex_t* mutex, unsigned int configuration) {
    assert_non_null(mutex);
    g_testContext.MutexConstructCalls++;
}

void MutexLock(Mutex_t* mutex) {
    assert_non_null(mutex);
    g_testContext.MutexLockCalls++;
}

void MutexUnlock(Mutex_t* mutex) {
    assert_non_null(mutex);
    g_testContext.MutexUnlockCalls++;
}
void MutexDestruct(Mutex_t* mutex) {
    assert_non_null(mutex);
    g_testContext.MutexDestructCalls++;
}

void DynamicMemoryPoolConstruct(DynamicMemoryPool_t* pool, uintptr_t startAddress,
                                size_t size, size_t chunkSize) {
    assert_non_null(pool);
    assert_int_equal(startAddress, 0x1000); // match values in GetMachine
    assert_int_equal(size, 0x10000); // match values in GetMachine
    assert_int_equal(chunkSize, 0x1000); // match values in GetMachine
    g_testContext.DynamicMemoryPoolConstructCalls++;
}

void DynamicMemoryPoolFree(DynamicMemoryPool_t* pool, uintptr_t address) {
    assert_non_null(pool);
    g_testContext.DynamicMemoryPoolFreeCalls++;
}

void DynamicMemoryPoolDestroy(DynamicMemoryPool_t* pool) {
    assert_non_null(pool);
    g_testContext.DynamicMemoryPoolDestroyCalls++;
}

void* kmalloc(size_t size) {
    if (g_testContext.FailAllocation) {
        return NULL;
    }
    g_testContext.LiveAllocations++;
    return test_malloc(size);
}

void kfree(void* memp) {
    if (g_testContext.SkipFree == memp) {
        return;
    }
    if (memp != NULL) {
        g_testContext.LiveAllocations--;
    }
    return test_free(memp);
}

SystemMachine_t* GetMachine(void)
{
    static SystemMachine_t machine = {
        .MemoryMap = {
                .UserHeap = {
                        .Start = 0x1000,
                        .Length = 0x10000
                },
                .ThreadLocal = { .Start = 0x1000, .Length = 0xffff }
        },
        .MemoryGranularity = 0x1000
    };
    return &machine;
}

uuid_t
CreateHandle(
    _In_ HandleType_t       type,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource)
{
    uuid_t handle;

    if (g_testContext.FailHandle && type == g_testContext.FailHandleType) {
        return UUID_INVALID;
    }
    for (handle = 1; handle < 16; handle++) {
        if (g_testContext.Handles[handle].References == 0) {
            g_testContext.Handles[handle] = (struct __TestHandle) {
                .Type = type,
                .Destructor = destructor,
                .Resource = resource,
                .References = 1
            };
            return handle;
        }
    }
    fail_msg("Test handle table exhausted");
    return UUID_INVALID;
}

void*
LookupHandleOfType(
    _In_ uuid_t       handle,
    _In_ HandleType_t type)
{
    assert_true(handle < 16);
    assert_true(g_testContext.Handles[handle].References > 0);
    assert_int_equal(g_testContext.Handles[handle].Type, type);
    return g_testContext.Handles[handle].Resource;
}

oserr_t
AcquireHandleOfType(
    _In_  uuid_t       handle,
    _In_  HandleType_t type,
    _Out_ void**       resource)
{
    void* value;

    if (g_testContext.FailAcquire && type == g_testContext.FailAcquireType) {
        return OS_ENOENT;
    }
    value = LookupHandleOfType(handle, type);
    g_testContext.Handles[handle].References++;
    if (resource != NULL) {
        *resource = value;
    }
    return OS_EOK;
}

oserr_t
DestroyHandle(
    _In_ uuid_t handle)
{
    struct __TestHandle* entry;

    assert_true(handle < 16);
    entry = &g_testContext.Handles[handle];
    assert_true(entry->References > 0);
    if (--entry->References != 0) {
        return OS_EINCOMPLETE;
    }
    entry->Destructor(entry->Resource);
    return OS_EOK;
}

MemorySpace_t*
GetDomainMemorySpace(void)
{
    return &GetMachine()->SystemSpace;
}

MemorySpace_t*
GetCurrentMemorySpace(void)
{
    if (g_testContext.CurrentHandle == UUID_INVALID) {
        return GetDomainMemorySpace();
    }
    return LookupHandleOfType(g_testContext.CurrentHandle, HandleTypeMemorySpace);
}

uuid_t
GetCurrentMemorySpaceHandle(void)
{
    return g_testContext.CurrentHandle;
}

size_t
GetMemorySpacePageSize(void)
{
    return 0x1000;
}

oserr_t
MmuCloneVirtualSpace(
    _In_ MemorySpace_t* parent,
    _In_ MemorySpace_t* child,
    _In_ int            inherit)
{
    (void)parent;
    (void)child;
    (void)inherit;
    return g_testContext.CloneResult;
}

oserr_t
MmuDestroyVirtualSpace(
    _In_ MemorySpace_t* memorySpace)
{
    (void)memorySpace;
    g_testContext.VirtualSpaceDestroyCalls++;
    return OS_EOK;
}
