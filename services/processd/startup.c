/**
 * Copyright 2021, Philip Meulengracht
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

#include <process.h>
#include <ddk/convert/inheritance.h>
#include <internal/_syscalls.h>
#include <internal/_process.h>
#include <sys_process_service_server.h>
#include <string.h>
#include <stdlib.h>

struct __StartupLayout {
    ProcessStartupInformation_t Header;
    size_t                      Length;
    uintptr_t                   Modules[PROCESS_MAXMODULES];
};

static oserr_t
__AddStartupSection(
    _InOut_ size_t* length,
    _In_    size_t  sectionLength)
{
    // Compare against the remaining room, so the sum cannot overflow.
    if (sectionLength > PROCESS_STARTUP_MAX_SIZE - *length) {
        return OS_EBUFFER;
    }
    *length += sectionLength;
    return OS_EOK;
}

static oserr_t
__MeasureStartupInformation(
    _In_  Process_t*                                  process,
    _In_  size_t                                      environmentLength,
    _In_  size_t                                      argumentsLength,
    _In_  const struct sys_process_inheritance_input* input,
    _Out_ struct __StartupLayout*                     layout)
{
    int     moduleCount = PROCESS_MAXMODULES;
    size_t  moduleBytes;
    size_t  inheritanceBytes = 0;
    oserr_t status;

    layout->Length = sizeof(ProcessStartupInformation_t);
    if (input->resources_count != 0) {
        // Counts were bounded by the input decoder.
        inheritanceBytes = (size_t)__InheritanceOutputLength(
            input->resources_count,
            input->descriptors_count
        );
    }

    status = __AddStartupSection(&layout->Length, inheritanceBytes);
    if (status != OS_EOK) {
        return status;
    }

    status = __AddStartupSection(&layout->Length, argumentsLength);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __AddStartupSection(&layout->Length, environmentLength);
    if (status != OS_EOK) {
        return status;
    }

    status = PEModuleEntryPoints(process->load_context, layout->Modules, &moduleCount);
    if (status != OS_EOK) {
        return status;
    }

    if (moduleCount < 1 || moduleCount > PROCESS_MAXMODULES) {
        return OS_EINVALPARAMS;
    }

    // The last entry is the main program, which does not need library initialization.
    moduleBytes = (size_t)(moduleCount - 1) * sizeof(uintptr_t);
    status = __AddStartupSection(&layout->Length, moduleBytes);
    if (status != OS_EOK) {
        return status;
    }

    layout->Header.ArgumentsLength = (uint32_t)argumentsLength;
    layout->Header.InheritationLength = (uint32_t)inheritanceBytes;
    layout->Header.LibraryEntriesLength = (uint32_t)moduleBytes;
    layout->Header.EnvironmentBlockLength = (uint32_t)environmentLength;
    return OS_EOK;
}

static oserr_t
__ImportInheritedResources(
    _In_  Process_t*                                  process,
    _In_  const struct sys_process_inheritance_input* input,
    _Out_ uuid_t*                                     resourcesOut)
{
    // No child thread exists yet. If any import fails, the caller destroys the
    // child memory space, releasing its earlier grants without a manager-side alias.
    for (uint32_t i = 0; i < input->resources_count; i++) {
        GrantToken_t token;
        oserr_t      status;
        
        status = __FromSysGrantToken(&input->resources[i], &token);
        if (status != OS_EOK) {
            return status;
        }

        status = Syscall_HandleImport(
            &token,
            process->load_context->MemorySpace,
            &resourcesOut[i]
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__WriteStartupInformation(
    _In_ const struct __StartupLayout*                layout,
    _In_ const char*                                  environment,
    _In_ const char*                                  arguments,
    _In_ const struct sys_process_inheritance_output* inherited,
    _In_ char*                                        buffer)
{
    struct ProcessStartupSections sections;
    gracht_buffer_t               payload;
    oserr_t                       status;

    // Write the header first and let the shared reader place the sections, so
    // the layout written here is the layout the new process reads.
    memcpy(buffer, &layout->Header, sizeof(layout->Header));
    
    status = __ProcessStartupSplit(buffer, layout->Length, &sections);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(sections.Arguments, arguments, layout->Header.ArgumentsLength);

    if (layout->Header.InheritationLength != 0) {
        payload = (gracht_buffer_t) {
            .data = sections.Inheritance,
            .limit = layout->Header.InheritationLength
        };
        
        serialize_sys_process_inheritance_output(&payload, inherited);
        if (payload.error || payload.index != layout->Header.InheritationLength) {
            return OS_EPROTOCOL;
        }
    }

    memcpy(sections.LibraryEntries, layout->Modules, layout->Header.LibraryEntriesLength);
    if (layout->Header.EnvironmentBlockLength != 0) {
        memcpy(sections.Environment, environment, layout->Header.EnvironmentBlockLength);
    }
    return OS_EOK;
}

oserr_t
PmPrepareStartupInformation(
    _In_ Process_t*                                  process,
    _In_ const char*                                 environment,
    _In_ size_t                                      environmentLength,
    _In_ const char*                                 arguments,
    _In_ size_t                                      argumentsLength,
    _In_ const struct sys_process_inheritance_input* input)
{
    struct sys_process_inheritance_output inherited = __InheritanceOutput(input);
    struct __StartupLayout                layout;
    char*                                 buffer = NULL;
    oserr_t                               status;

    status = __MeasureStartupInformation(
        process,
        environmentLength,
        argumentsLength,
        input,
        &layout
    );
    if (status != OS_EOK) {
        return status;
    }

    // All size checks and allocations precede the first import.
    if (input->resources_count != 0) {
        inherited.resources = calloc(input->resources_count, sizeof(*inherited.resources));
        if (inherited.resources == NULL) {
            return OS_EOOM;
        }
    }

    buffer = malloc(layout.Length);
    if (buffer == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    status = __ImportInheritedResources(process, input, inherited.resources);
    if (status != OS_EOK) {
        goto cleanup;
    }

    status = __WriteStartupInformation(&layout, environment, arguments, &inherited, buffer);
    if (status != OS_EOK) {
        goto cleanup;
    }

    process->startup_data.Buffer = buffer;
    process->startup_data.Length = (uint32_t)layout.Length;
    buffer = NULL;

cleanup:
    free(buffer);
    free(inherited.resources);
    return status;
}

oserr_t
PmGetProcessStartupInformation(
    _In_  const struct gracht_security_context* caller,
    _In_  uuid_t                                threadHandle,
    _In_  const SHMTransfer_t*                  destination,
    _Out_ uuid_t*                               processHandleOut,
    _Out_ uint32_t*                             bytesWrittenOut)
{
    Process_t* process;
    size_t     capacity;
    oserr_t    oserr;

    if (!__IsAuthenticatedCaller(caller)) {
        return OS_EPERMISSIONS;
    }

    oserr = PmReferenceProcessByThread(threadHandle, &process);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (caller->principal != (uint64_t)process->load_context->SecurityGroupID) {
        oserr = OS_EPERMISSIONS;
        goto release;
    }

    capacity = __SHMTransferLength(destination);
    if (capacity > PROCESS_STARTUP_MAX_SIZE) {
        oserr = OS_EBUFFER;
        goto release;
    }

    if (process->startup_data.Length > capacity) {
        oserr = OS_EBUFFER;
        goto release;
    }

    oserr = __SHMTransferWrite(
        destination,
        process->startup_data.Buffer,
        process->startup_data.Length
    );
    if (oserr == OS_EOK) {
        *processHandleOut = process->handle.ID;
        *bytesWrittenOut = process->startup_data.Length;
    }

release:
    PmReleaseProcess(process);
    return oserr;
}

void
sys_process_get_startup_information_invocation(
    struct gracht_message*         message,
    const uuid_t                   handle,
    const struct sys_shm_transfer* destination)
{
    SHMTransfer_t transfer;
    uuid_t        processHandle = UUID_INVALID;
    uint32_t      bytesWritten = 0;
    oserr_t       oserr;

    oserr = __FromSysSHMTransfer(destination, &transfer);
    if (oserr == OS_EOK) {
        oserr = PmGetProcessStartupInformation(
            gracht_server_get_security_context(message),
            handle,
            &transfer,
            &processHandle,
            &bytesWritten
        );
    }
    sys_process_get_startup_information_response(message, oserr, processHandle, bytesWritten);
}
