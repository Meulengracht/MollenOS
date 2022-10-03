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

#include <vafs/vafs.h>
#include <zstd.h>

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

static int __zstd_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    /* Read the content size from the frame header. For simplicity we require
     * that it is always present. By default, zstd will write the content size
     * in the header when it is known. If you can't guarantee that the frame
     * content size is always written into the header, either use streaming
     * decompression, or ZSTD_decompressBound().
     */
    size_t             decompressedSize;
    unsigned long long contentSize = ZSTD_getFrameContentSize(Input, InputLength);
    if (contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "__zstd_decode: failed to get frame content size\n");
        return -1;
    }

    /* Decompress.
     * If you are doing many decompressions, you may want to reuse the context
     * and use ZSTD_decompressDCtx(). If you want to set advanced parameters,
     * use ZSTD_DCtx_setParameter().
     */
    decompressedSize = ZSTD_decompress(Output, *OutputLength, Input, InputLength);
    if (ZSTD_isError(decompressedSize)) {
        return -1;
    }
    *OutputLength = (uint32_t)decompressedSize;
    return 0;
}

static int __set_filter_ops(
        struct VaFs* vafs)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));

    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.Encode = NULL;
    filterOps.Decode = __zstd_decode;

    return vafs_feature_add(vafs, &filterOps.Header);
}

int __ValiFSHandleFilter(struct VaFs* vafs)
{
    struct VaFsFeatureFilter* filter;
    int                       status;

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs);
}
