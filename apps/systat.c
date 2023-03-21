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
    OSSystemCPUInfo_t    cpuInfo;
    OSSystemMemoryInfo_t memoryInfo;
    oserr_t              oserr;
    size_t               bytesQueried;
    uint64_t             memoryTotal;
    uint64_t             memoryInUse;

    oserr = OSSystemQuery(
            OSSYSTEMQUERY_CPUINFO,
            &cpuInfo,
            sizeof(OSSystemCPUInfo_t),
            &bytesQueried
    );
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        printf("systat: failed to retrieve system stats: %i\n", errno);
        return -1;
    }

    oserr = OSSystemQuery(
            OSSYSTEMQUERY_MEMINFO,
            &memoryInfo,
            sizeof(OSSystemMemoryInfo_t),
            &bytesQueried
    );
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        printf("systat: failed to retrieve system stats: %i\n", errno);
        return -1;
    }

    memoryTotal = (memoryInfo.PageSizeBytes * memoryInfo.PagesTotal) / (1024 * 1024);
    memoryInUse = (memoryInfo.PageSizeBytes * memoryInfo.PagesUsed) / (1024 * 1024);

    printf("processor count: %u", (uint32_t)cpuInfo.NumberOfProcessors);
    printf(" (cores active: %u)\n", (uint32_t)cpuInfo.NumberOfActiveCores);
    printf("page-size: %u bytes\n", (uint32_t)memoryInfo.PageSizeBytes);
    printf("allocation-size: %u bytes\n", (uint32_t)memoryInfo.AllocationGranularityBytes);
    printf("memory usage: %llu/%llu MiB\n", memoryInUse, memoryTotal);
    return 0;
}
