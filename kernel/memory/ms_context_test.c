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
    int   MutexConstructCalls;
    int   MutexLockCalls;
    int   MutexUnlockCalls;
    int   MutexDestructCalls;
    int   DynamicMemoryPoolConstructCalls;
    int   DynamicMemoryPoolFreeCalls;
    int   DynamicMemoryPoolDestroyCalls;
    void* SkipFree;
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
    return 0;
}

void TestMSContextNew_Happy(void** state)
{
    struct MSContext* context;

    context = MSContextNew();
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexConstructCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolConstructCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 0);
    assert_int_equal(context->SignalHandler, 0);

    // delete again, should be OK to invoke this
    MSContextDelete(context);
}

void TestMSContextAddAllocation_Happy(void** state)
{
    struct MSContext*   context;
    struct MSAllocation allocation = {
            .Address = 0x13444,
            .Length = 0x1000
    };

    context = MSContextNew();
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    // manually free object, don't want to free the allocation
    // we have on the stack
    test_free(context);
}

void TestMSContextDelete_Happy(void** state)
{
    struct MSContext*   context;
    struct MSAllocation allocation = {
            .Header = { .value = &allocation },
            .Address = 0x13444,
            .Length = 0x1000
    };

    context = MSContextNew();
    assert_non_null(context);
    assert_int_equal(g_testContext.MutexLockCalls, 0);
    assert_int_equal(g_testContext.MutexUnlockCalls, 0);

    MSContextAddAllocation(context, &allocation);
    assert_int_equal(g_testContext.MutexLockCalls, 1);
    assert_int_equal(g_testContext.MutexUnlockCalls, 1);
    assert_int_equal(list_count(&context->Allocations), 1);

    g_testContext.SkipFree = &allocation;
    MSContextDelete(context);

    // ensure that DynamicMemoryPoolFree was called once, this means
    // the allocation was freed
    assert_int_equal(g_testContext.MutexDestructCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolFreeCalls, 1);
    assert_int_equal(g_testContext.DynamicMemoryPoolDestroyCalls, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(TestMSContextNew_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSContextAddAllocation_Happy, SetupTest),
            cmocka_unit_test_setup(TestMSContextDelete_Happy, SetupTest),
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
    return test_malloc(size);
}

void kfree(void* memp) {
    if (g_testContext.SkipFree == memp) {
        return;
    }
    return test_free(memp);
}

SystemMachine_t* GetMachine(void)
{
    return &(SystemMachine_t) {
        .MemoryMap = {
                .UserHeap = {
                        .Start = 0x1000,
                        .Length = 0x10000
                }
        },
        .MemoryGranularity = 0x1000
    };
}
