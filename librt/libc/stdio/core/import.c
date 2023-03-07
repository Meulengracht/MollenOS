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

#include <internal/_io.h>
#include "private.h"

size_t
OSHandleDeserialize(
        _In_ struct OSHandle* handle,
        _In_ const void*      buffer);

static size_t
__ParseInheritationHeader(
        _In_ uint8_t* headerData)
{
    struct InheritationHeader* header = (struct InheritationHeader*)&headerData[0];
    int                        status;
    stdio_handle_t*            handle;
    size_t                     bytesParsed = 0;

    // First we create the stdio handle, for this we only need what we can read
    // in the header. The payload will then be parsed, and last, the OS handle
    status = stdio_handle_create2(
            header->IOD,
            header->IOFlags,
            header->XTFlags | WX_PERSISTANT,
            header->Signature,
            NULL,
            &handle
    );
    assert(status == 0);
    bytesParsed += sizeof(struct InheritationHeader);

    // We've parsed the initial data required to set up a new io object.
    // Now we import the OS handle stuff
    bytesParsed += OSHandleDeserialize(&handle->OSHandle, &headerData[bytesParsed]);

    // Handle any implementation specific importation
    if (handle->Ops->deserialize) {
        bytesParsed += handle->Ops->deserialize(&headerData[bytesParsed], &handle->OpsContext);
    }
    return bytesParsed;
}

static void
__ParseInheritationBlock(
        _In_ void* inheritanceBlock)
{
    struct InheritationBlock* block = inheritanceBlock;
    size_t                    bytesConsumed = 0;

    for (int i = 0; i < block->Count; i++) {
        bytesConsumed += __ParseInheritationHeader(&block->Data[bytesConsumed]);
    }
}

void
CRTReadInheritanceBlock(
        _In_ void* inheritanceBlock)
{
    if (inheritanceBlock != NULL) {
        __ParseInheritationBlock(inheritanceBlock);
    }
}
