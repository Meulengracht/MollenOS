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
 *
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <vafs/vafs.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum VaFsFilterType {
    VaFsFilterType_APLIB
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
    int                      Type;
};

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

#if defined(__VAFS_FILTER_APLIB)
#include <aplib.h>

static int __aplib_encode(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength)
{

    return 0;
}

static int __aplib_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{

    return 0;
}
#endif

static int __set_filter_ops(
    struct VaFs*              vafs,
    struct VaFsFeatureFilter* filter)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);

    switch (filter->Type) {
#if defined(__VAFS_FILTER_APLIB)
        case VaFsFilterType_APLIB: {
            filterOps.Encode = __aplib_encode;
            filterOps.Decode = __aplib_decode;
        }
#endif
        default: {
            fprintf(stderr, "unsupported filter type %i\n", filter->Type);
            return -1;
        }
    }

    return vafs_feature_add(vafs, &filterOps.Header);
}

int __handle_filter(
    struct VaFs* vafs)
{
    struct VaFsFeatureFilter* filter;
    int                       status;

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs, filter);
}

static enum VaFsFilterType __get_filter_from_name(
    const char* filterName)
{
#if defined(__VAFS_FILTER_APLIB)
    if (strcmp(filterName, "aplib") == 0)
        return VaFsFilterType_APLIB;
#endif
    return -1;
}

int __install_filter(
    struct VaFs* vafs,
    const char*  filterName)
{
    struct VaFsFeatureFilter filter;
    int                      status;

    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureFilter);
    filter.Type          = __get_filter_from_name(filterName);
    if (filter.Type == -1) {
        fprintf(stderr, "unsupported filter type %s\n", filterName);
        return -1;
    }

    status = vafs_feature_add(vafs, &filter.Header);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs, &filter);
}
