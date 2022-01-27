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

#include <errno.h>
#include <ddk/initrd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vafs/vafs.h>
#include "depacks.h"

enum VaFsFilterType {
    VaFsFilterType_APLIB
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
    int                      Type;
};

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

static int __aplib_encode_dummy(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength)
{
    _CRT_UNUSED(Input);
    _CRT_UNUSED(InputLength);
    _CRT_UNUSED(Output);
    _CRT_UNUSED(OutputLength);
    return -1;
}

static int __aplib_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    size_t decompressedSize;

    decompressedSize = aP_get_orig_size(Input);
    if (decompressedSize == APLIB_ERROR) {
        errno = EINVAL;
        return -1;
    }

    if (decompressedSize > *OutputLength) {
        errno = ENODATA;
        return -1;
    }

    decompressedSize = aP_depack_safe(Input, InputLength, Output, decompressedSize);
    *OutputLength = decompressedSize;
    return 0;
}

static int __set_filter_ops(
    struct VaFs*              vafs,
    struct VaFsFeatureFilter* filter)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);

    switch (filter->Type) {
        case VaFsFilterType_APLIB: {
            filterOps.Encode = __aplib_encode_dummy;
            filterOps.Decode = __aplib_decode;
        } break;
        default: {
            fprintf(stderr, "unsupported filter type %i\n", filter->Type);
            return -1;
        }
    }

    return vafs_feature_add(vafs, &filterOps.Header);
}

int DdkInitrdHandleVafsFilter(
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
