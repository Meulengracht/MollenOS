/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __TYPES_QUERY_H__
#define __TYPES_QUERY_H__

enum OSSystemQueryRequest {
    OSSYSTEMQUERY_BOOTVIDEOINFO,
    OSSYSTEMQUERY_CPUINFO,
    OSSYSTEMQUERY_MEMINFO,
    OSSYSTEMQUERY_THREADS,
};

typedef struct OSSystemCPUInfo {
    size_t NumberOfProcessors;
    size_t NumberOfActiveCores;
} OSSystemCPUInfo_t;

typedef struct OSSystemMemoryInfo {
    size_t PagesTotal;
    size_t PagesUsed;
    size_t PageSizeBytes;
    size_t AllocationGranularityBytes;
} OSSystemMemoryInfo_t;

#endif //!__TYPES_QUERY_H__
