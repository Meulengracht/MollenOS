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
 *
 */

//#define __TRACE

#include <ddk/barrier.h>
#include <ddk/utils.h>

#include "runtime-private.h"

#define RP1_COMPLETION_CFG(source) (0x8U + 4U * (source))
#define RP1_COMPLETION_SET          0x800U
#define RP1_COMPLETION_IACK        (1U << 2)

static irqstatus_t
__Rp1CompleteInterrupt(
    _In_ InterruptFunctionTable_t* functions,
    _In_ void*                     context)
{
    InterruptResourceTable_t*          resources = context;
    const struct Rp1CompletionContext* source;
    DeviceIo_t*                        registers = resources->IoResources[0];
    size_t                             offset;

    // This callback runs in interrupt context. Use only the resources mapped
    // by the kernel and the supplied function table.
    source = (const struct Rp1CompletionContext*)resources->MemoryResources[0].Address;
    offset = RP1_COMPLETION_CFG(source->Source);

    // The child must clear or mask its device before this callback runs.
    // RP1 keeps a level-triggered interrupt active until we acknowledge it here.
    // Read back the register so the write completes before the main interrupt
    // controller is told that handling has finished.
    mb();
    functions->WriteIoSpace(registers, RP1_COMPLETION_SET + offset, RP1_COMPLETION_IACK, 4);
    mb();
    (void)functions->ReadIoSpace(registers, offset, 4);
    return IRQSTATUS_HANDLED;
}

oserr_t
Rp1CompletionStart(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int       source)
{
    struct Rp1Completion*    completion = &runtime->Completions[source];
    InterruptResourceTable_t resources = { .Handler = __Rp1CompleteInterrupt };
    oserr_t                  status;

    TRACE("Rp1CompletionStart(runtime=%p, source=%u)", runtime, source);

    // Store the handle before binding so Rp1CompletionsStop can release it
    // if the resource mapping fails.
    status = DeviceInterruptBusHandlerCreate(&completion->Handle);
    if (status != OS_EOK) {
        ERROR("Rp1CompletionStart: DeviceInterruptBusHandlerCreate source %u failed: %u", source, status);
        return status;
    }

    completion->Context.Source = source;
    resources.IoResources[0] = &runtime->Pci.Apbs;
    resources.MemoryResources[0].Address = (uintptr_t)&completion->Context;
    resources.MemoryResources[0].Length = sizeof(completion->Context);

    // The context must stay valid until DeviceInterruptBusHandlerRelease succeeds.
    // If release fails, keep the runtime so cleanup can be retried.
    status = DeviceInterruptBusHandlerBind(completion->Handle, &resources);
    if (status != OS_EOK) {
        ERROR("Rp1CompletionStart: DeviceInterruptBusHandlerBind source %u, handle %u failed: %u",
              source, completion->Handle, status);
        return status;
    }
    TRACE("Rp1CompletionStart: source %u bound to completion %u", source, completion->Handle);
    return OS_EOK;
}

oserr_t
Rp1CompletionsStop(
    _InOut_ struct Rp1Runtime* runtime)
{
    oserr_t status;

    TRACE("Rp1CompletionsStop(runtime=%p)", runtime);
    for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
        if (runtime->Completions[i].Handle == UUID_INVALID) {
            continue;
        }

        // A child group may still use this callback. Keep its data and register
        // mapping until the callback has been released.
        status = DeviceInterruptBusHandlerRelease(runtime->Completions[i].Handle);
        if (status != OS_EOK) {
            ERROR("Rp1CompletionsStop: DeviceInterruptBusHandlerRelease source %u, handle %u failed: %u",
                  i, runtime->Completions[i].Handle, status);
            return status;
        }
        runtime->Completions[i].Handle = UUID_INVALID;
    }
    return OS_EOK;
}
