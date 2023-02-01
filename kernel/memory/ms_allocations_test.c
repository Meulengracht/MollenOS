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

int TestSetup(void** state)
{
    (void)state;
    return 0;
}

int TestTeardown(void** state)
{
    (void)state;
    return 0;
}

void TestMSAllocationCreate_Happy(void** state)
{
    (void)state;


}


int main(void)
{
    const struct CMUnitTest tests [] = {
            cmocka_unit_test(TestMSAllocationCreate_Happy),
    };
    return cmocka_run_group_tests(tests, TestSetup, TestTeardown);
}

// Mock this, called by MSAllocationCreate
void MSContextAddAllocation(struct MSContext* context, struct MSAllocation* allocation) {
    (void)context;
    (void)allocation;
}

void MutexLock(Mutex_t* mutex) {

}

void MutexUnlock(Mutex_t* mutex) {

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
