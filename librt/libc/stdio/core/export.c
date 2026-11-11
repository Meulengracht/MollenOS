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

#include <ddk/convert/grants.h>
#include <internal/_io.h>
#include <os/grants.h>
#include <os/types/file.h>
#include <stdlib.h>

#include "private.h"

struct __export_resource {
    uuid_t              ID;
    unsigned int        Signature;
    GrantTokenOptions_t Options;
};

static void
__SelectInheritedFile(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       context)
{
    const struct stdio_object_entry* entry = element;
    stdio_handle_t**                 selected = context;
    stdio_handle_t*                  handle = entry->handle;
    _CRT_UNUSED(index);

    // Standard descriptors are selected explicitly below. Runtime IPC and other
    // priority descriptors must never enter the application's inheritance list.
    if (handle->IOD > STDERR_FILENO &&
        !(handle->XTFlags & (__IO_NOINHERIT | __IO_PRIORITY))) {
        selected[handle->IOD] = handle;
    }
}

static oserr_t
__SelectStandardFile(
    _InOut_ stdio_handle_t** selected,
    _In_    int              source,
    _In_    int              destination)
{
    stdio_handle_t* handle;

    if (source < 0 || source >= INTERNAL_MAXFILES) {
        return OS_EINVALPARAMS;
    }

    handle = stdio_handle_get(source);
    if (handle == NULL) {
        return OS_ENOENT;
    }

    if (handle->XTFlags & __IO_PRIORITY) {
        return OS_ENOTSUPPORTED;
    }

    // Preserve the existing no-inherit selection policy, even for stdio.
    selected[destination] = (handle->XTFlags & __IO_NOINHERIT) ? NULL : handle;
    return OS_EOK;
}

static oserr_t
__SelectInheritedFiles(
    _In_  const struct InheritanceOptions* options,
    _Out_ stdio_handle_t**                 selected)
{
    const int sources[] = {
        options->StdInHandle,
        options->StdOutHandle,
        options->StdErrHandle
    };

    const unsigned int flags[] = {
        PROCESS_INHERIT_STDIN,
        PROCESS_INHERIT_STDOUT,
        PROCESS_INHERIT_STDERR
    };

    if (options->Flags & ~PROCESS_INHERIT_ALL) {
        return OS_EINVALPARAMS;
    }

    if (options->Flags & PROCESS_INHERIT_FILES) {
        hashtable_enumerate(IODescriptors(), __SelectInheritedFile, selected);
    }

    for (int i = 0; i < 3; i++) {
        oserr_t status;

        if (!(options->Flags & flags[i])) {
            continue;
        }
        
        status = __SelectStandardFile(selected, sources[i], i);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

void
CRTReleaseInheritanceBlock(
    _InOut_ struct inheritance_export* exported)
{
    for (uint32_t i = 0; i < exported->count; i++) {
        OSGrantTokenRevoke(exported->controls[i]);
    }
    free(exported->controls);
    memset(exported, 0, sizeof(struct inheritance_export));
}

static uint32_t
__FindExportResource(
    _In_ const struct __export_resource* resources,
    _In_ uint32_t                        count,
    _In_ uuid_t                          id)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        if (resources[i].ID == id) {
            break;
        }
    }
    return i;
}

// The descriptor is the number in the child. For standard streams it can
// differ from the local descriptor number of handle.
static oserr_t
__AddInheritedDescriptor(
    _In_    int                                   descriptor,
    _In_    const stdio_handle_t*                 handle,
    _InOut_ struct __export_resource*             resources,
    _InOut_ struct sys_process_inheritance_input* input)
{
    GrantTokenOptions_t tokenOptions;
    unsigned int        flags;
    uint32_t            resource;
    oserr_t             status;

    status = CRTPrepareInheritedHandle(handle, &flags, &tokenOptions);
    if (status != OS_EOK) {
        return status;
    }

    // Descriptor aliases share one export, but distinct restricted reference
    // IDs stay separate.
    resource = __FindExportResource(
        resources,
        input->resources_count,
        handle->OSHandle.ID
    );
    if (resource == input->resources_count) {
        resources[resource] = (struct __export_resource) {
            .ID = handle->OSHandle.ID,
            .Signature = handle->Signature,
            .Options = tokenOptions
        };
        input->resources_count++;
    } else {
        // One local resource must have one reconstruction type. Different
        // descriptor modes may share it, but different signatures may not.
        if (resources[resource].Signature != handle->Signature) {
            return OS_EINVALPARAMS;
        }
        resources[resource].Options.Scope |= tokenOptions.Scope;
    }

    input->descriptors[input->descriptors_count++] = (struct sys_inherited_descriptor) {
        .descriptor = descriptor,
        .resource_index = resource,
        .signature = handle->Signature,
        .flags = flags
    };
    return OS_EOK;
}

static oserr_t
__ExportResources(
    _In_    const struct __export_resource*       resources,
    _InOut_ struct sys_process_inheritance_input* input,
    _InOut_ struct inheritance_export*            exported)
{
    for (uint32_t i = 0; i < input->resources_count; i++) {
        GrantToken_t token;
        oserr_t      status;
        
        status = OSGrantTokenCreate(resources[i].ID, &resources[i].Options, &token);
        if (status != OS_EOK) {
            return status;
        }

        // Record the control handle first, so the caller revokes it on failure.
        exported->controls[exported->count++] = token.ControlHandleID;

        status = __ToSysGrantToken(&token, &input->resources[i]);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
CRTWriteInheritanceBlock(
    _In_  const struct InheritanceOptions* options,
    _Out_ void*                            buffer,
    _In_  uint32_t                         capacity,
    _Out_ struct inheritance_export*       exportOut)
{
    struct sys_process_inheritance_input input = { 0 };
    struct inheritance_export            exported = { 0 };
    gracht_buffer_t                      wire = { .data = buffer, .limit = capacity };
    stdio_handle_t**                     selected = NULL;
    struct __export_resource*            resources = NULL;
    oserr_t                              status = OS_EOK;

    memset(exportOut, 0, sizeof(*exportOut));
    if (options->Flags == PROCESS_INHERIT_NONE) {
        return OS_EOK;
    }

    selected = calloc(INTERNAL_MAXFILES, sizeof(*selected));
    resources = calloc(INTERNAL_MAXFILES, sizeof(*resources));
    input.resources = calloc(INTERNAL_MAXFILES, sizeof(*input.resources));
    input.descriptors = calloc(INTERNAL_MAXFILES, sizeof(*input.descriptors));
    exported.controls = calloc(INTERNAL_MAXFILES, sizeof(*exported.controls));
    
    if (selected == NULL || resources == NULL || input.resources == NULL ||
        input.descriptors == NULL || exported.controls == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    status = __SelectInheritedFiles(options, selected);
    if (status != OS_EOK) {
        goto cleanup;
    }

    // Build the complete selection before creating any token.
    for (uint32_t i = 0; i < INTERNAL_MAXFILES; i++) {
        if (selected[i] == NULL) {
            continue;
        }

        status = __AddInheritedDescriptor((int)i, selected[i], resources, &input);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }
    if (input.descriptors_count == 0) {
        goto cleanup;
    }

    // Check the generated layout against capacity before exporting resources.
    // Placeholder tokens have the same fixed encoded size as the real tokens.
    serialize_sys_process_inheritance_input(&wire, &input);
    if (wire.error) {
        status = OS_EBUFFER;
        goto cleanup;
    }

    status = __ExportResources(resources, &input, &exported);
    if (status != OS_EOK) {
        goto cleanup;
    }

    wire.index = 0;
    serialize_sys_process_inheritance_input(&wire, &input);
    exported.length = wire.index;

cleanup:
    free(selected);
    free(resources);
    sys_process_inheritance_input_destroy(&input);

    if (status == OS_EOK) {
        *exportOut = exported;
    } else {
        CRTReleaseInheritanceBlock(&exported);
    }
    return status;
}
