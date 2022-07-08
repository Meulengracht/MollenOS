/**
 * Copyright 2021, Philip Meulengracht
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
 *
 * System Statistics Application
 */

#include <errno.h>
#include <os/mollenos.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    SystemDescriptor_t systemDescriptor;
    oserr_t         osStatus;
    uint64_t           memoryTotal;
    uint64_t           memoryInUse;

    osStatus = SystemQuery(&systemDescriptor);
    if (osStatus != OsOK) {
        OsErrToErrNo(osStatus);
        printf("systat: failed to retrieve system stats: %i\n", errno);
        return -1;
    }

    memoryTotal = (systemDescriptor.PageSizeBytes * systemDescriptor.PagesTotal) / (1024 * 1024);
    memoryInUse = (systemDescriptor.PageSizeBytes * systemDescriptor.PagesUsed) / (1024 * 1024);

    printf("processor count: %u", (uint32_t)systemDescriptor.NumberOfProcessors);
    printf(" (cores active: %u)\n", (uint32_t)systemDescriptor.NumberOfActiveCores);
    printf("page-size: %u bytes\n", (uint32_t)systemDescriptor.PageSizeBytes);
    printf("allocation-size: %u bytes\n", (uint32_t)systemDescriptor.AllocationGranularityBytes);
    printf("memory usage: %llu/%llu MiB\n", memoryInUse, memoryTotal);
    return 0;
}
