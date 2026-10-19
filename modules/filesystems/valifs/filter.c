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

#include <ddk/initrd.h>
#include <errno.h>
#include <vafs/reader.h>
#include <zstd.h>

static int __zstd_decode(
    _In_  const void* input,
    _In_  size_t      inputLength,
    _In_  void*       output,
    _In_  size_t      outputLength,
    _In_  void*       userData,
    _Out_ size_t*     bytesWrittenOut)
{
    size_t decoded;
    (void)userData;

    decoded = ZSTD_decompress(output, outputLength, input, inputLength);;
    if (ZSTD_isError(decoded)) {
        errno = EINVAL;
        return -1;
    }
    
    *bytesWrittenOut = decoded;
    return 0;
}

void __ValiFSConfigureReader(struct VaFsReaderConfiguration* configuration,
                           struct VaFsCodec codecs[2])
{
    DdkInitrdConfigureVafsReader(configuration);
    codecs[0] = configuration->SupportedCodecs[0];
    codecs[1] = (struct VaFsCodec) { .ID = "zstd", .Decode = __zstd_decode };
    vafs_reader_config_set_codecs(configuration, codecs, 2);
}
