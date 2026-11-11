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
#include <handles/handle.h>
#include "private.h"
#include <heap.h>
#include <string.h>

// These tests exercise shared memory-group data and memory-space cleanup with
// small replacements for the handle table, allocator and page-table routines.
// They check reference counts and cleanup calls, without creating real mappings.
// Handle cleanup runs immediately here. The kernel queues it on the janitor.

/**
 * @brief One test resource and the references that keep it allocated.
 * A zero count makes the slot reusable. Releasing the last reference calls
 * Destructor immediately so each test can check allocation counts before returning.
 */
struct __TestHandle {
    HandleType_t       Type;
    HandleDestructorFn Destructor;
    void*              Resource;
    int                References;
};

/**
 * @brief Counters, handle entries and failure settings reset before each test.
 * FailHandle and FailAcquire apply only to their selected resource types.
 * CurrentHandle selects the simulated running space. SkipFree lets a test use
 * a stack allocation record while still checking that cleanup tries to free it.
 */
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
    bool               FailGrantTable;
    int                GrantCleanupCalls;
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
    uuid_t            groupGlobalHandleID;

    groupGlobalHandleID = MSContextNew();
    assert_int_not_equal(groupGlobalHandleID, UUID_INVALID);
    context = LookupGlobalHandle(groupGlobalHandleID, HandleTypeMSContext);
    assert_non_null(context);
    // SyncObject and UserCopyLock
    assert_int_equal(g_testContext.MutexConstructCalls, 2);
    assert_int_equal(g_testContext.DynamicMemoryPoolConstructCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 0);
    assert_int_equal(context->SignalHandler, 0);
    assert_int_equal(context->Security.GlobalHandleID, groupGlobalHandleID);
    assert_int_equal(context->Security.Capabilities, 0);

    // Releasing the final handle reference frees the context.
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

void TestMSContextAddAllocation_Happy(void** state)
{
    struct MSContext*   context;
    uuid_t              groupGlobalHandleID;
    struct MSAllocation allocation = {
            .Address = 0x13444,
            .Length = 0x1000
    };

    groupGlobalHandleID = MSContextNew();
    context = LookupGlobalHandle(groupGlobalHandleID, HandleTypeMSContext);
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    list_remove(&context->Allocations, &allocation.Header);
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EOK);
}

void TestMSContextDelete_Happy(void** state)
{
    struct MSContext*   context;
    uuid_t              groupGlobalHandleID;
    struct MSAllocation allocation = {
            .Header = { .value = &allocation },
            .Address = 0x13444,
            .Length = 0x1000
    };

    groupGlobalHandleID = MSContextNew();
    context = LookupGlobalHandle(groupGlobalHandleID, HandleTypeMSContext);
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    g_testContext.SkipFree = &allocation;
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EOK);

    // Deleting the context frees the allocation through DynamicMemoryPoolFree.
    assert_int_equal(g_testContext.MutexDestructCalls, 2);
    assert_int_equal(g_testContext.DynamicMemoryPoolFreeCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolDestroyCalls, 1);
}

// Children created from an inherited space must still refer to the process root.
// Closing the root's caller handle cannot free it while either child uses it.
static void
__TestGroupLifetime(
    _In_ void** state)
{
    MemorySpace_t*    root;
    MemorySpace_t*    child;
    MemorySpace_t*    sibling;
    uuid_t            rootHandle;
    uuid_t            rootSecurityGroupID;
    uuid_t            childHandle;
    uuid_t            childSecurityGroupID;
    uuid_t            siblingHandle;
    uuid_t            siblingSecurityGroupID;
    uuid_t            groupGlobalHandleID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &rootHandle,
        &rootSecurityGroupID
    ), OS_EOK);
    root = LookupHandle(rootHandle, HandleTypeMemorySpace);
    groupGlobalHandleID = root->GroupGlobalHandleID;
    assert_int_equal(rootSecurityGroupID, groupGlobalHandleID);
    assert_int_equal(root->ParentHandle, UUID_INVALID);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 1);

    g_testContext.CurrentHandle = rootHandle;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &childHandle,
        &childSecurityGroupID
    ), OS_EOK);
    child = LookupHandle(childHandle, HandleTypeMemorySpace);
    assert_int_equal(childSecurityGroupID, groupGlobalHandleID);
    assert_int_equal(child->GroupGlobalHandleID, groupGlobalHandleID);
    assert_int_equal(child->ParentHandle, rootHandle);
    assert_ptr_equal(__MSContextGet(root), __MSContextGet(child));

    g_testContext.CurrentHandle = childHandle;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &siblingHandle,
        &siblingSecurityGroupID
    ), OS_EOK);
    sibling = LookupHandle(siblingHandle, HandleTypeMemorySpace);
    assert_int_equal(siblingSecurityGroupID, groupGlobalHandleID);
    assert_int_equal(sibling->ParentHandle, rootHandle);
    assert_int_equal(sibling->GroupGlobalHandleID, groupGlobalHandleID);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 3);
    assert_ptr_equal(MemorySpaceGetSecurityContext(), &__MSContextGet(child)->Security);

    // Releasing the root handle must leave inherited tables valid.
    assert_int_equal(DestroyHandle(rootHandle), OS_EINCOMPLETE);
    assert_int_equal(DestroyHandle(childHandle), OS_EOK);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 2);
    assert_int_equal(DestroyHandle(siblingHandle), OS_EOK);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 0);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 3);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

// A direct group reference keeps process-wide data allocated even after the last
// memory space is deleted. Releasing that reference must free the group too.
static void
__TestRetainedGroup(
    _In_ void** state)
{
    MemorySpace_t*    space;
    struct MSContext* context;
    uuid_t            handle;
    uuid_t            securityGroupID;
    uuid_t            groupGlobalHandleID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &handle,
        &securityGroupID
    ), OS_EOK);
    space = LookupHandle(handle, HandleTypeMemorySpace);
    groupGlobalHandleID = space->GroupGlobalHandleID;
    assert_int_equal(AcquireGlobalHandle(groupGlobalHandleID, HandleTypeMSContext, (void**)&context), OS_EOK);
    assert_int_equal(DestroyHandle(handle), OS_EOK);
    assert_ptr_equal(LookupGlobalHandle(groupGlobalHandleID, HandleTypeMSContext), context);
    assert_int_equal(context->Security.GlobalHandleID, groupGlobalHandleID);
    // The context and its grant table stay allocated until the final reference is released.
    assert_int_equal(g_testContext.LiveAllocations, 2);
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

// Kernel-only spaces have page-table resources but no application memory group.
static void
__TestKernelSpace(
    _In_ void** state)
{
    MemorySpace_t*    space;
    uuid_t            handle;
    uuid_t            securityGroupID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_INHERIT,
        &handle,
        &securityGroupID
    ), OS_EOK);
    space = LookupHandle(handle, HandleTypeMemorySpace);
    assert_int_equal(space->GroupGlobalHandleID, UUID_INVALID);
    assert_int_equal(securityGroupID, UUID_INVALID);
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
__TestGroupGlobalHandleIDFailure(
    _In_ void** state)
{
    uuid_t            handle;
    uuid_t            securityGroupID;

    (void)state;
    g_testContext.FailHandle = true;
    g_testContext.FailHandleType = HandleTypeMSContext;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &handle,
        &securityGroupID
    ), OS_EOOM);
    assert_int_equal(g_testContext.LiveAllocations, 0);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 0);
}

// A page-table clone failure must undo both references added for an inherited
// space. The failing clone handles its own page-table cleanup, so these tests
// expect no call to MmuDestroyVirtualSpace for that partial space.
static void
__TestCloneFailure(
    _In_ void** state)
{
    uuid_t            root;
    uuid_t            rootSecurityGroupID;
    uuid_t            child;
    uuid_t            childSecurityGroupID;
    uuid_t            groupGlobalHandleID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &root,
        &rootSecurityGroupID
    ), OS_EOK);
    groupGlobalHandleID = ((MemorySpace_t*)LookupHandle(root, HandleTypeMemorySpace))->GroupGlobalHandleID;
    g_testContext.CurrentHandle = root;
    g_testContext.CloneResult = OS_EOOM;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &child,
        &childSecurityGroupID
    ), OS_EOOM);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 1);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 0);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);

    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &root,
        &rootSecurityGroupID
    ), OS_EOOM);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

// Handle registration happens after a successful page-table clone. If it fails,
// the complete space must be destroyed, including its parent and group references.
static void
__TestSpaceHandleFailure(
    _In_ void** state)
{
    uuid_t            root;
    uuid_t            rootSecurityGroupID;
    uuid_t            groupGlobalHandleID;
    uuid_t            handle;
    uuid_t            securityGroupID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &root,
        &rootSecurityGroupID
    ), OS_EOK);
    groupGlobalHandleID = ((MemorySpace_t*)LookupHandle(root, HandleTypeMemorySpace))->GroupGlobalHandleID;
    g_testContext.CurrentHandle = root;
    g_testContext.FailHandle = true;
    g_testContext.FailHandleType = HandleTypeMemorySpace;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &handle,
        &securityGroupID
    ), OS_EOOM);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(g_testContext.Handles[groupGlobalHandleID].References, 1);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);

    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &handle,
        &securityGroupID
    ), OS_EOOM);
    assert_int_equal(g_testContext.VirtualSpaceDestroyCalls, 3);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

// Fail each reference step separately. Failure to reference the group must also
// release the parent reference added by the preceding step.
static void
__TestGroupAcquireFailure(
    _In_ void** state)
{
    uuid_t            root;
    uuid_t            rootSecurityGroupID;
    uuid_t            child;
    uuid_t            childSecurityGroupID;

    (void)state;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION,
        &root,
        &rootSecurityGroupID
    ), OS_EOK);
    g_testContext.CurrentHandle = root;
    g_testContext.FailAcquire = true;
    g_testContext.FailAcquireType = HandleTypeMSContext;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &child,
        &childSecurityGroupID
    ), OS_ENOENT);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    g_testContext.FailAcquireType = HandleTypeMemorySpace;
    assert_int_equal(CreateMemorySpace(
        MEMORY_SPACE_APPLICATION | MEMORY_SPACE_INHERIT,
        &child,
        &childSecurityGroupID
    ), OS_ENOENT);
    assert_int_equal(g_testContext.Handles[root].References, 1);
    assert_int_equal(DestroyHandle(root), OS_EOK);
    assert_int_equal(g_testContext.LiveAllocations, 0);
}

HandleGrantsTable_t*
CreateHandleGrantsTable(void)
{
    if (g_testContext.FailGrantTable) {
        return NULL;
    }
    return kmalloc(1);
}

void
DestroyHandleGrantsTable(
    _In_ HandleGrantsTable_t* table)
{
    if (table != NULL) {
        g_testContext.GrantCleanupCalls++;
        kfree(table);
    }
}

static void
__TestGrantTableFailure(
    _In_ void** state)
{
    (void)state;
    g_testContext.FailGrantTable = true;
    assert_int_equal(MSContextNew(), UUID_INVALID);
    assert_int_equal(g_testContext.LiveAllocations, 0);
    assert_int_equal(g_testContext.GrantCleanupCalls, 0);
    assert_int_equal(g_testContext.DynamicMemoryPoolDestroyCalls, 1);
}

static void
__TestGrantCleanup(
    _In_ void** state)
{
    uuid_t groupGlobalHandleID;

    (void)state;
    groupGlobalHandleID = MSContextNew();
    assert_int_not_equal(groupGlobalHandleID, UUID_INVALID);
    assert_int_equal(AcquireGlobalHandle(groupGlobalHandleID, HandleTypeMSContext, NULL), OS_EOK);
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EINCOMPLETE);
    assert_int_equal(ReleaseGlobalHandle(groupGlobalHandleID), OS_EOK);
    assert_int_equal(g_testContext.GrantCleanupCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolDestroyCalls, 1);
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
            cmocka_unit_test_setup(__TestGroupGlobalHandleIDFailure, SetupTest),
            cmocka_unit_test_setup(__TestCloneFailure, SetupTest),
            cmocka_unit_test_setup(__TestSpaceHandleFailure, SetupTest),
            cmocka_unit_test_setup(__TestGroupAcquireFailure, SetupTest),
            cmocka_unit_test_setup(__TestGrantCleanup, SetupTest),
            cmocka_unit_test_setup(__TestGrantTableFailure, SetupTest),
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

static uuid_t
__CreateTestHandle(
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

uuid_t
CreateHandle(
    _In_ HandleType_t       type,
    _In_ HandleDestructorFn grantCleanup,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource)
{
    (void)grantCleanup;
    assert_int_not_equal(type, HandleTypeMSContext);
    return __CreateTestHandle(type, destructor, resource);
}

uuid_t
CreateGlobalHandle(
    _In_ HandleType_t       type,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource)
{
    assert_int_equal(type, HandleTypeMSContext);
    return __CreateTestHandle(type, destructor, resource);
}

static void*
__LookupTestHandle(
    _In_ uuid_t       handle,
    _In_ HandleType_t type)
{
    assert_true(handle < 16);
    assert_true(g_testContext.Handles[handle].References > 0);
    assert_int_equal(g_testContext.Handles[handle].Type, type);
    return g_testContext.Handles[handle].Resource;
}

void*
LookupHandle(
    _In_ uuid_t       handle,
    _In_ HandleType_t type)
{
    assert_int_not_equal(type, HandleTypeMSContext);
    return __LookupTestHandle(handle, type);
}

void*
LookupGlobalHandle(
    _In_ uuid_t       globalHandleID,
    _In_ HandleType_t type)
{
    // MSContext uses global handles from creation. Inherited memory spaces also
    // use this lookup when adding a kernel reference to their existing root.
    // These test entries track lifetime only, without process permission checks.
    assert_true(type == HandleTypeMSContext || type == HandleTypeMemorySpace);
    return __LookupTestHandle(globalHandleID, type);
}

oserr_t
AcquireHandle(
    _In_  uuid_t       handle,
    _In_  HandleType_t type,
    _Out_ void**       resource)
{
    void* value;

    if (g_testContext.FailAcquire && type == g_testContext.FailAcquireType) {
        return OS_ENOENT;
    }
    value = LookupHandle(handle, type);
    g_testContext.Handles[handle].References++;
    if (resource != NULL) {
        *resource = value;
    }
    return OS_EOK;
}

oserr_t
AcquireGlobalHandle(
    _In_  uuid_t       globalHandleID,
    _In_  HandleType_t type,
    _Out_ void**       resource)
{
    void* value;

    if (g_testContext.FailAcquire && type == g_testContext.FailAcquireType) {
        return OS_ENOENT;
    }
    value = LookupGlobalHandle(globalHandleID, type);
    g_testContext.Handles[globalHandleID].References++;
    if (resource != NULL) {
        *resource = value;
    }
    return OS_EOK;
}

// Unlike the kernel janitor, this replacement calls the destructor before
// returning. OS_EINCOMPLETE still means the reference was successfully released.
static oserr_t
__ReleaseTestHandle(
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

oserr_t
DestroyHandle(
    _In_ uuid_t handle)
{
    assert_int_not_equal(g_testContext.Handles[handle].Type, HandleTypeMSContext);
    return __ReleaseTestHandle(handle);
}

oserr_t
ReleaseGlobalHandle(
    _In_ uuid_t globalHandleID)
{
    // Pair both the memory-group and parent-space references taken during setup.
    // A parent reference must be releasable even outside its creating process.
    assert_true(g_testContext.Handles[globalHandleID].Type == HandleTypeMSContext ||
                g_testContext.Handles[globalHandleID].Type == HandleTypeMemorySpace);
    return __ReleaseTestHandle(globalHandleID);
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
    return LookupHandle(g_testContext.CurrentHandle, HandleTypeMemorySpace);
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
