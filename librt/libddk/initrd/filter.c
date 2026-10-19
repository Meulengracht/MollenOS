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
#include <limits.h>
#include <ddk/initrd.h>
#include <vafs/reader.h>
#include "depacks.h"

static int __aplib_decode(
    _In_  const void* input,
    _In_  size_t      inputLength,
    _In_  void*       output,
    _In_  size_t      outputLength,
    _In_  void*       userData,
    _Out_ size_t*     bytesWrittenOut)
{
    unsigned int original, decoded;
    (void)userData;

    if (!input || !output || !bytesWrittenOut || inputLength < 24 || inputLength > UINT_MAX) {
        errno = EINVAL;
        return -1;
    }

    original = aP_get_orig_size(input);
    if (original == APLIB_ERROR || original > outputLength) {
        errno = EINVAL;
        return -1;
    }
    
    decoded = aP_depack_safe(input, (unsigned int)inputLength, output, original);
    if (decoded == APLIB_ERROR || decoded != original) {
        errno = EINVAL;
        return -1;
    }
    
    *bytesWrittenOut = decoded;
    return 0;
}

static struct VaFsCodec g_initrdCodecs[] = {
    { .ID = "aplib", .Decode = __aplib_decode }
};

void DdkInitrdConfigureVafsReader(struct VaFsReaderConfiguration* configuration)
{
    vafs_reader_config_initialize(configuration);
    vafs_reader_config_set_codecs(configuration, g_initrdCodecs, 1);
}
