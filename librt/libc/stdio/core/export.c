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

#include <assert.h>
#include <internal/_io.h>
#include "private.h"

/**
 * Returns whether or not the handle should be inheritted by sub-processes based on the requested
 * startup information and the handle settings.
 */
static oserr_t
__IsInheritable(
        _In_ struct InheritanceOptions* options,
        _In_ stdio_handle_t*            handle)
{
    oserr_t oserr = OS_EOK;

    if (handle->XTFlags & WX_DONTINHERIT) {
        oserr = OS_EUNKNOWN;
    }

    // If we didn't request to inherit one of the handles, then we don't account it
    // for being the one requested.
    if (handle->IOD == options->StdOutHandle &&
        !(options->Flags & PROCESS_INHERIT_STDOUT)) {
        oserr = OS_EUNKNOWN;
    } else if (handle->IOD == options->StdInHandle &&
               !(options->Flags & PROCESS_INHERIT_STDIN)) {
        oserr = OS_EUNKNOWN;
    } else if (handle->IOD == options->StdErrHandle &&
               !(options->Flags & PROCESS_INHERIT_STDERR)) {
        oserr = OS_EUNKNOWN;
    } else if (!(options->Flags & PROCESS_INHERIT_FILES)) {
        if (handle->IOD != options->StdOutHandle &&
            handle->IOD != options->StdInHandle &&
            handle->IOD != options->StdErrHandle) {
            oserr = OS_EUNKNOWN;
        }
    }
    return oserr;
}

struct __get_inherit_context {
    struct InheritanceOptions* options;
    int                        file_count;
};

static void
__count_inherit_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry  = element;
    stdio_handle_t*                  object = entry->handle;
    struct __get_inherit_context*    context = userContext;
    _CRT_UNUSED(index);

    if (__IsInheritable(context->options, object) == OS_EOK) {
        context->file_count++;
    }
}

static int
__InheritableHandlesCount(
        _In_ struct InheritanceOptions* options)
{
    struct __get_inherit_context context = {
            .options = options,
            .file_count = 0
    };
    LOCK_FILES();
    hashtable_enumerate(
            IODescriptors(),
            __count_inherit_entry,
            &context
    );
    UNLOCK_FILES();
    return context.file_count;
}

struct __create_inherit_context {
    struct InheritanceOptions* options;
    struct InheritationBlock*  inheritation_block;
    size_t                     bytes_written;
};

size_t
OSHandleSerialize(
        _In_ struct OSHandle* handle,
        _In_ void*            buffer);

static void
__create_inherit_callback(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry  = element;
    stdio_handle_t*                  object = entry->handle;
    struct __create_inherit_context* context = userContext;
    struct InheritationHeader*       header;
    void*                            payload;
    _CRT_UNUSED(index);

    if (__IsInheritable(context->options, object) != OS_EOK) {
        return;
    }

    header = (struct InheritationHeader*)
            &context->inheritation_block->Data[context->bytes_written];

    // Check for this fd to be equal to one of the custom handles
    // if it is equal, we need to update the fd of the handle to our reserved
    if (object->IOD == context->options->StdOutHandle) {
        header->IOD = STDOUT_FILENO;
    } else if (object->IOD == context->options->StdInHandle) {
        header->IOD = STDIN_FILENO;
    } else if (object->IOD == context->options->StdErrHandle) {
        header->IOD = STDERR_FILENO;
    } else {
        header->IOD = object->IOD;
    }
    header->Signature = object->Signature;
    header->IOFlags = object->IOFlags;
    header->XTFlags = (int)object->XTFlags;

    // Mark that we have now written the header
    context->bytes_written += sizeof(struct InheritationHeader);

    // Write the OS handle data. This is needed to reconstruct the full
    // io descriptor
    payload = &context->inheritation_block->Data[context->bytes_written];
    context->bytes_written += OSHandleSerialize(&object->OSHandle, payload);

    if (object->Ops->serialize) {
        payload = &context->inheritation_block->Data[context->bytes_written];
        context->bytes_written += object->Ops->serialize(object->OpsContext, payload);
    }
}

void
CRTWriteInheritanceBlock(
        _In_  struct InheritanceOptions* options,
        _In_  void*                      buffer,
        _Out_ uint32_t*                  lengthWrittenOut)
{
    struct __create_inherit_context context;
    struct InheritationBlock*       inheritationBlock;
    int                             numberOfObjects;

    assert(options != NULL);

    if (options->Flags == PROCESS_INHERIT_NONE) {
        *lengthWrittenOut = 0;
        return;
    }

    numberOfObjects = __InheritableHandlesCount(options);
    if (numberOfObjects == 0) {
        *lengthWrittenOut = 0;
        return;
    }

    inheritationBlock = buffer;
    inheritationBlock->Count = numberOfObjects;

    context.options = options;
    context.inheritation_block = inheritationBlock;
    context.bytes_written = sizeof(struct InheritationBlock);

    LOCK_FILES();
    hashtable_enumerate(
            IODescriptors(),
            __create_inherit_callback,
            &context
    );
    UNLOCK_FILES();
    *lengthWrittenOut = context.bytes_written;
}
