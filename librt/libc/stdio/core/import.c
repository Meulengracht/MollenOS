/**
 * Copyright, Philip Meulengracht
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

#include <ddk/convert/inheritance.h>
#include <internal/_io.h>
#include <os/handle.h>
#include <os/types/file.h>
#include <stdlib.h>

#include "private.h"

struct __import_resource {
    unsigned int Signature;
    OSHandle_t   Handle;
};

static oserr_t
__RecordResourceSignature(
    _InOut_ struct __import_resource* resource,
    _In_    unsigned int              signature)
{
    // Zero is never a supported signature, so it marks an unseen resource.
    if (resource->Signature == 0) {
        resource->Signature = signature;
        return OS_EOK;
    }

    if (resource->Signature != signature) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ValidateStdioDescriptors(
    _In_    const struct sys_inherited_descriptor* descriptors,
    _In_    uint32_t                               descriptorCount,
    _InOut_ struct __import_resource*              resources)
{
    if (descriptorCount > INTERNAL_MAXFILES) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < descriptorCount; i++) {
        oserr_t status;

        if (descriptors[i].descriptor >= INTERNAL_MAXFILES) {
            return OS_EINVALPARAMS;
        }

        status = CRTValidateInheritedDescriptor(
            descriptors[i].signature,
            descriptors[i].flags
        );
        if (status != OS_EOK) {
            return status;
        }

        // The layout check bounded resource_index.
        status = __RecordResourceSignature(
            &resources[descriptors[i].resource_index],
            descriptors[i].signature
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__ValidateChildInheritance(
    _In_    const struct sys_process_inheritance_output* output,
    _InOut_ struct __import_resource*                    resources)
{
    OSHandle_t existing;
    oserr_t    status;

    status = __ValidateStdioDescriptors(
        output->descriptors,
        output->descriptors_count,
        resources
    );
    if (status != OS_EOK) {
        return status;
    }

    // Check all collisions before changing either local table. Internal startup
    // descriptors keep their numbers; inheritance must not silently replace them.
    for (uint32_t i = 0; i < output->descriptors_count; i++) {
        if (stdio_handle_get((int)output->descriptors[i].descriptor) != NULL) {
            return OS_EEXISTS;
        }
    }

    for (uint32_t i = 0; i < output->resources_count; i++) {
        if (output->resources[i] == UUID_INVALID) {
            return OS_EINVALPARAMS;
        }

        for (uint32_t j = 0; j < i; j++) {
            if (output->resources[j] == output->resources[i]) {
                return OS_EINVALPARAMS;
            }
        }

        status = OSHandlesFind(output->resources[i], &existing);
        if (status != OS_ENOENT) {
            return status == OS_EOK ? OS_EEXISTS : status;
        }
    }
    return OS_EOK;
}

oserr_t
CRTReadInheritanceBlock(
    _In_ const void* buffer,
    _In_ uint32_t    length)
{
    struct sys_process_inheritance_output output = { 0 };
    gracht_buffer_t                       wire = { .data = (char*)buffer, .limit = length };
    struct __import_resource*             resources = NULL;
    stdio_handle_t*                       descriptor;
    uint32_t                              wrapped = 0;
    uint32_t                              created = 0;
    uint32_t                              resource;
    oserr_t                               status;

    if (length == 0) {
        return OS_EOK;
    }

    if (length > PROCESS_STARTUP_MAX_SIZE) {
        return OS_EBUFFER;
    }

    if (buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    deserialize_sys_process_inheritance_output(&wire, &output);
    if (wire.error || wire.index != length) {
        status = wire.error == ENOMEM ? OS_EOOM : OS_EINVALPARAMS;
        goto cleanup;
    }

    // The layout check bounds the resource count before it sizes the table.
    status = __ValidateInheritanceLayout(&output);
    if (status != OS_EOK) {
        goto cleanup;
    }

    resources = calloc(output.resources_count, sizeof(*resources));
    if (resources == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    status = __ValidateChildInheritance(&output, resources);
    if (status != OS_EOK) {
        goto cleanup;
    }

    for (uint32_t i = 0; i < output.resources_count; i++) {
        status = CRTAdoptInheritedHandle(
            resources[i].Signature,
            output.resources[i],
            &resources[i].Handle
        );
        if (status != OS_EOK) {
            goto cleanup;
        }
        wrapped++;
    }

    for (uint32_t i = 0; i < output.descriptors_count; i++) {
        int code = stdio_handle_create(
            (int)output.descriptors[i].descriptor,
            0,
            output.descriptors[i].flags,
            output.descriptors[i].signature,
            NULL,
            &descriptor
        );
        if (code) {
            status = OS_EOOM;
            goto cleanup;
        }

        // The general constructor derives read mode from open flags. Here the
        // validated descriptor mode is already complete, including write-only.
        descriptor->XTFlags = CRTInheritedLocalFlags(
            output.descriptors[i].signature,
            output.descriptors[i].flags
        );

        stdio_handle_set_shared(descriptor);
        resource = output.descriptors[i].resource_index;

        status = OSHandlesAcquire(&resources[resource].Handle);
        if (status != OS_EOK) {
            stdio_handle_delete(descriptor);
            goto cleanup;
        }

        stdio_handle_set_handle(descriptor, &resources[resource].Handle);
        created++;
    }
    status = OS_EOK;

cleanup:
    if (status != OS_EOK) {
        for (uint32_t i = 0; i < created; i++) {
            descriptor = stdio_handle_get((int)output.descriptors[i].descriptor);
            OSHandleDestroy(&descriptor->OSHandle);
            stdio_handle_delete(descriptor);
        }
    }

    // On success the descriptors keep their local references. On failure, any
    // unwrapped kernel grants are released by the required process exit.
    for (uint32_t i = 0; i < wrapped; i++) {
        OSHandleDestroy(&resources[i].Handle);
    }

    free(resources);
    sys_process_inheritance_output_destroy(&output);
    return status;
}
