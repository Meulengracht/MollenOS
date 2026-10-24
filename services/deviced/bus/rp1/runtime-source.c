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

#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <ddk/utils.h>
#include <string.h>

#include "runtime-private.h"

#define RP1_VECTOR_BYTES   16U
#define RP1_VECTOR_CONTROL 12U

oserr_t
Rp1SourceSetTableMask(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int       source,
    _In_    int                mask)
{
    size_t   offset = source * RP1_VECTOR_BYTES + RP1_VECTOR_CONTROL;
    uint32_t value;
    uint32_t actual;
    oserr_t  status;

    // Change only the mask bit. Keep the MSI-X address, data, and other
    // settings unchanged.
    value = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
    value = mask ? value | 1U : value & ~1U;
    status = WriteDeviceIo(&runtime->Pci.Table, offset, value, 4);
    if (status != OS_EOK) {
        ERROR("Rp1SourceSetTableMask: WriteDeviceIo source %u, offset 0x%zx, mask %d failed: %u",
              source, offset, mask, status);
        return status;
    }
    
    actual = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
    if (actual != value) {
        ERROR("Rp1SourceSetTableMask: source %u, offset 0x%zx readback 0x%x, expected 0x%x",
              source, offset, actual, value);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
Rp1SourceMask(
    _In_ void*        context,
    _In_ unsigned int source)
{
    return Rp1SourceSetTableMask(context, source, 1);
}

oserr_t
Rp1SourceUnmask(
    _In_ void*        context,
    _In_ unsigned int source)
{
    return Rp1SourceSetTableMask(context, source, 0);
}

static struct Rp1Source*
__Rp1ChildSource(
    _In_ struct Rp1Child* child,
    _In_ unsigned int    index)
{
    // Firmware numbers belong to the child. Convert one to its RP1 table entry
    // before checking who owns it or changing interrupt delivery.
    return &child->Bus->Runtime->Sources[child->Firmware.Interrupts[index].Number];
}

static int
__Rp1PendingCloseFinished(
    _In_ oserr_t status)
{
    if (status == OS_EOK || status == OS_ENOENT) {
        return 1;
    }
    // These errors happen before attachment succeeds, so this caller has no
    // callbacks to release.
    return status == OS_EPERMISSIONS || status == OS_ENOTSUPPORTED;
}

static oserr_t
__Rp1ReserveSources(
    _InOut_ struct Rp1Child*                  child,
    _In_    uuid_t                            principal,
    _In_    const struct PciInterruptRequest* request)
{
    struct InterruptProviderVector        vectors[FDT_RP1_MAX_INTERRUPTS] = { 0 };
    struct InterruptProviderAttachRequest attachment = {
        .Allocation = request->Allocation,
        .Principal = principal,
        .Device = child->DeviceId,
        .ControllerID = child->Bus->Parent->Host->InterruptControllerId,
        .Count = request->Count,
        .Vectors = vectors
    };

    struct Rp1Source* source;
    oserr_t           status;
    oserr_t           closeStatus;
    unsigned int      count = 0;

    TRACE("__Rp1ReserveSources(device=%u, principal=%u, allocation=%u, count=%u)",
          child->DeviceId, principal, request->Allocation, request->Count);
    
    // Each reserved index points to the same position in the child's firmware
    // list. Reject a count that extends past that list.
          if (request->Count == 0 || request->Count > child->Firmware.InterruptCount) {
        WARNING("__Rp1ReserveSources: device %u requested %u sources, capacity %u",
                child->DeviceId, request->Count, child->Firmware.InterruptCount);
        return OS_EINVALPARAMS;
    }

    for (unsigned int i = 0; i < child->Firmware.InterruptCount; i++) {
        source = __Rp1ChildSource(child, i);
        if (source->Owner != NULL) {
            // Allow a retry only from the same caller and allocation. Another
            // caller cannot take over the interrupt.
            if (source->Owner != child || source->Allocation != request->Allocation ||
                source->Principal != principal) {
                TRACE("__Rp1ReserveSources: device %u interrupt %u already owned by allocation %u, principal %u",
                      child->DeviceId, i, source->Allocation, source->Principal);
                return OS_EBUSY;
            }
            count++;
        }
    }
    if (count != 0 && count != request->Count) {
        // The kernel attaches the whole group at once. Reject partial state
        // rather than guessing which missing entries should be added.
        WARNING("__Rp1ReserveSources: allocation %u retry requested %u sources, retained %u",
                request->Allocation, request->Count, count);
        return OS_EINVALPARAMS;
    }

    // Choose the callbacks here, where the firmware numbers and RP1 bus are
    // available. Drivers do not receive these handles.
    for (unsigned int i = 0; i < request->Count; i++) {
        vectors[i].Completion = child->Bus->Runtime->Completions[
            child->Firmware.Interrupts[i].Number
        ].Handle;
    }

    // Attach the whole group before writing to device registers. The kernel
    // checks the driver, controller, and number of interrupts, then returns
    // their messages. Retrying the same request does not attach callbacks
    // again, even if the previous reply could not be copied to the driver.
    status = DeviceInterruptProviderAttach(&attachment);
    if (status != OS_EOK) {
        WARNING("__Rp1ReserveSources: DeviceInterruptProviderAttach device %u, allocation %u, principal %u failed: %u",
                child->DeviceId, request->Allocation, principal, status);
        // Keep an earlier attachment if a retry fails. Its messages may
        // already be in use by the device.
        if (count != 0 && __Rp1ChildSource(child, 0)->MessageReady) {
            TRACE("__Rp1ReserveSources: preserving confirmed attachment for allocation %u", request->Allocation);
            return status == OS_EBUSY ? OS_EUNKNOWN : status;
        }
        
        // The kernel may attach the group even if returning its messages fails.
        // Close that attachment before clearing our state. The PCI lock keeps
        // bus removal from interrupting this cleanup.
        closeStatus = DeviceInterruptProviderClose(
            request->Allocation,
            principal,
            child->DeviceId
        );
        if (__Rp1PendingCloseFinished(closeStatus)) {
            TRACE("__Rp1ReserveSources: pending allocation %u closed with status %u", request->Allocation, closeStatus);
            // These errors mean the caller has no attachment to keep. No
            // device registers have been written.
            for (unsigned int i = 0; i < count; i++) {
                source = __Rp1ChildSource(child, i);
                memset(source, 0, sizeof(*source));
            }
            return status;
        }
        ERROR("__Rp1ReserveSources: DeviceInterruptProviderClose device %u, allocation %u failed: %u; cleanup retained",
              child->DeviceId, request->Allocation, closeStatus);
    }

    for (unsigned int i = 0; i < request->Count; i++) {
        source = __Rp1ChildSource(child, i);
        source->Owner = child;
        source->Allocation = request->Allocation;
        source->Principal = principal;
        if (status == OS_EOK) {
            source->Message = vectors[i].Message;
            source->MessageReady = 1;
        }
    }

    // Keep removal blocked if cleanup fails. The driver must retry Release.
    // libddk treats EBUSY as an untouched reservation owned by someone else,
    // so return a general error when this cleanup needs another try.
    return status == OS_EBUSY ? OS_EUNKNOWN : status;
}

static oserr_t
__Rp1ProgramSource(
    _InOut_ struct Rp1Child*                  child,
    _In_    unsigned int                      count,
    _In_    const struct PciInterruptRequest* request)
{
    struct Rp1Runtime*       runtime = child->Bus->Runtime;
    struct Rp1Source*        source;
    DeviceInterruptMessage_t message;
    unsigned int             number;
    uint32_t                 words[3];
    uint32_t                 actual;
    size_t                   offset;
    oserr_t                  status;

    TRACE("__Rp1ProgramSource(device=%u, allocation=%u, index=%u)",
          child->DeviceId, request->Allocation, request->Index);
    
    // The request index belongs to this child's allocation. Check it before
    // converting it to a firmware number or changing the MSI-X table.
          if (request->Index >= count) {
        WARNING("__Rp1ProgramSource: index %u exceeds allocation %u count %u",
                request->Index, request->Allocation, count);
        return OS_EINVALPARAMS;
    }
    
    source = __Rp1ChildSource(child, request->Index);
    if (!source->MessageReady) {
        TRACE("__Rp1ProgramSource: allocation %u attachment is not confirmed", request->Allocation);
        return OS_EBUSY;
    }
    
    number = child->Firmware.Interrupts[request->Index].Number;
    message = (DeviceInterruptMessage_t) {
        request->AddressLow, request->AddressHigh, request->Data
    };
    
    // Attachment supplied the message. Check the driver's request against it
    // before writing to the device. Never let a caller choose the address for
    // a PCI write.
    if (memcmp(&message, &source->Message, sizeof(message)) != 0) {
        WARNING("__Rp1ProgramSource: allocation %u source %u message does not match its kernel reservation",
                request->Allocation, number);
        return OS_EINVALPARAMS;
    }
    
    if (source->Programmed) {
        // The kernel message stays the same for this allocation. After checking
        // it, a repeated Program needs no table writes, even while enabled.
        return OS_EOK;
    }
    
    if (runtime->Controller.Enabled[number]) {
        // Changing an active entry could redirect an interrupt that is already
        // on its way. Disable delivery before changing its message.
        TRACE("__Rp1ProgramSource: source %u is still enabled", number);
        return OS_EBUSY;
    }
    
    status = Rp1SourceSetTableMask(runtime, number, 1);
    if (status != OS_EOK) {
        ERROR("__Rp1ProgramSource: Rp1SourceSetTableMask source %u failed: %u", number, status);
        return status;
    }
    
    words[0] = message.AddressLow;
    words[1] = message.AddressHigh;
    words[2] = message.Data;

    for (unsigned int i = 0; i < 3; i++) {
        offset = number * RP1_VECTOR_BYTES + i * sizeof(uint32_t);
        status = WriteDeviceIo(&runtime->Pci.Table, offset, words[i], 4);
        if (status != OS_EOK) {
            ERROR("__Rp1ProgramSource: WriteDeviceIo source %u, word %u, offset 0x%zx failed: %u",
                  number, i, offset, status);
            return status;
        }
        
        actual = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
        if (actual != words[i]) {
            ERROR("__Rp1ProgramSource: source %u, word %u readback 0x%x, expected 0x%x",
                  number, i, actual, words[i]);
            return OS_EUNKNOWN;
        }
    }
    // Mark the entry ready only after all three message words match on readback.
    // A retry can then skip writing to the table.
    source->Programmed = 1;
    return OS_EOK;
}

static oserr_t
__Rp1DisableSources(
    _InOut_ struct Rp1Child* child,
    _In_    unsigned int    count)
{
    oserr_t result = OS_EOK;

    TRACE("__Rp1DisableSources(device=%u, count=%u)", child->DeviceId, count);
    // Try to mask every source, even if one fails. A failed mask must not leave
    // the other interrupts active while the group is being released.
    for (unsigned int i = 0; i < count; i++) {
        oserr_t status = Rp1InterruptDisable(
            &child->Bus->Runtime->Controller,
            child->Firmware.Interrupts[i].Number
        );
        if (status != OS_EOK) {
            ERROR("__Rp1DisableSources: Rp1InterruptDisable device %u, source %u failed: %u",
                  child->DeviceId, child->Firmware.Interrupts[i].Number, status);
            result = status;
        }
    }
    return result;
}

static oserr_t
__Rp1EnableSources(
    _InOut_ struct Rp1Child* child,
    _In_    unsigned int    count)
{
    struct Rp1Runtime* runtime = child->Bus->Runtime;

    TRACE("__Rp1EnableSources(device=%u, count=%u)", child->DeviceId, count);
    // Check every message first. If one is missing, do not enable only part of
    // the child's interrupt group.
    for (unsigned int i = 0; i < count; i++) {
        if (!__Rp1ChildSource(child, i)->Programmed) {
            WARNING("__Rp1EnableSources: device %u interrupt %u is not programmed", child->DeviceId, i);
            return OS_EINVALPARAMS;
        }
    }

    for (unsigned int i = 0; i < count; i++) {
        unsigned int number = child->Firmware.Interrupts[i].Number;
        oserr_t      status;
        
        if (runtime->Controller.Enabled[number]) {
            continue;
        }
        
        status = Rp1InterruptEnable(&runtime->Controller, number);
        if (status != OS_EOK) {
            ERROR("__Rp1EnableSources: Rp1InterruptEnable device %u, source %u failed: %u",
                  child->DeviceId, number, status);
            
            // Earlier interrupts may already be active. Disable them before
            // returning the enable error.
            oserr_t cleanupStatus = __Rp1DisableSources(child, count);
            if (cleanupStatus != OS_EOK) {
                ERROR("__Rp1EnableSources: __Rp1DisableSources device %u rollback failed: %u",
                      child->DeviceId, cleanupStatus);
            }
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__Rp1CloseAllocation(
    _In_ struct Rp1Child* child,
    _In_ uuid_t           principal,
    _In_ uuid_t           allocation)
{
    oserr_t status;

    TRACE("__Rp1CloseAllocation(device=%u, principal=%u, allocation=%u)", child->DeviceId, principal, allocation);
    
    status = DeviceInterruptProviderClose(allocation, principal, child->DeviceId);
    // Kernel cleanup may have already removed the group and its callbacks.
    // Release the hardware interrupts either way.
    if (status == OS_ENOENT) {
        TRACE("__Rp1CloseAllocation: allocation %u is already gone", allocation);
        return OS_EOK;
    }
    
    if (status != OS_EOK) {
        WARNING("__Rp1CloseAllocation: DeviceInterruptProviderClose device %u, allocation %u, principal %u failed: %u",
                child->DeviceId, allocation, principal, status);
    }
    return status;
}

oserr_t
Rp1RuntimeControl(
    _InOut_ struct Rp1Child*             child,
    _In_    uuid_t                      principal,
    _InOut_ struct PciInterruptRequest* request)
{
    struct Rp1Runtime* runtime = child->Bus->Runtime;
    struct Rp1Source*  source;
    unsigned int       count = 0;
    oserr_t            status;

    TRACE("Rp1RuntimeControl(device=%u, principal=%u, allocation=%u, operation=%u)",
          child->DeviceId, principal, request->Allocation, request->Operation);
    
    // Check the fields needed by every request before handling it. This keeps
    // reserve, program, enable, disable, and release requests clear.
    if (principal == UUID_INVALID || request->Allocation == UUID_INVALID) {
        WARNING("Rp1RuntimeControl: device %u missing principal or allocation", child->DeviceId);
        return OS_EPERMISSIONS;
    }
    
    if (request->Strategy != INTERRUPT_STRATEGY_MSIX || request->Reserved != 0 ||
        request->Operation > PciInterruptRelease) {
        WARNING("Rp1RuntimeControl: device %u invalid strategy 0x%x, reserved %u or operation %u",
                child->DeviceId, request->Strategy, request->Reserved, request->Operation);
        return OS_EINVALPARAMS;
    }
    
    if (request->Operation != PciInterruptReserve && request->Count != 0) {
        WARNING("Rp1RuntimeControl: operation %u cannot specify count %u", request->Operation, request->Count);
        return OS_EINVALPARAMS;
    }
    
    if (request->Operation != PciInterruptProgram &&
        (request->Index || request->AddressLow || request->AddressHigh || request->Data)) {
        WARNING("Rp1RuntimeControl: operation %u cannot specify a message", request->Operation);
        return OS_EINVALPARAMS;
    }
    
    if (runtime == NULL || !child->Bus->Ready || runtime->Quiescing) {
        TRACE("Rp1RuntimeControl: device %u runtime is absent, not ready or quiescing", child->DeviceId);
        return OS_EBUSY;
    }
    
    if (request->Operation == PciInterruptReserve) {
        return __Rp1ReserveSources(child, principal, request);
    }
    
    // Find this allocation's interrupts and check their owner before carrying
    // out the request. The allocation ID alone does not prove ownership.
    for (unsigned int i = 0; i < child->Firmware.InterruptCount; i++) {
        source = __Rp1ChildSource(child, i);
        if (source->Allocation == request->Allocation && source->Owner == child) {
            if (source->Principal != principal) {
                WARNING("Rp1RuntimeControl: principal %u does not own allocation %u", principal, request->Allocation);
                return OS_EPERMISSIONS;
            }
            count++;
        }
    }
    
    if (count == 0) {
        // Also clean up if the Reserve request never reached the server.
        if (request->Operation == PciInterruptRelease) {
            return __Rp1CloseAllocation(child, principal, request->Allocation);
        }
        if (request->Operation == PciInterruptDisable) {
            return OS_EOK;
        }
        TRACE("Rp1RuntimeControl: device %u has no sources for allocation %u", child->DeviceId, request->Allocation);
        return OS_ENOENT;
    }
    
    if (request->Operation == PciInterruptProgram) {
        return __Rp1ProgramSource(child, count, request);
    }
    
    if (request->Operation == PciInterruptEnable) {
        // Enable hardware only after the driver has bound and enabled the full
        // interrupt group. Delivery can start when the first entry is unmasked.
        status = DeviceInterruptProviderCheckEnabled(
            request->Allocation,
            principal,
            child->DeviceId
        );
        if (status != OS_EOK) {
            WARNING("Rp1RuntimeControl: DeviceInterruptProviderCheckEnabled device %u, allocation %u failed: %u",
                    child->DeviceId, request->Allocation, status);
            return status;
        }
        return __Rp1EnableSources(child, count);
    }
    
    // Disable the hardware before closing the kernel attachment or clearing
    // its saved messages. The completion callback may still be in use.
    status = __Rp1DisableSources(child, count);
    if (status != OS_EOK) {
        return status;
    }
    
    if (request->Operation != PciInterruptRelease) {
        return OS_EOK;
    }
    
    // Check synchronization and unbind the client's handler before releasing
    // the interrupts. The kernel keeps this allocation's callback separate
    // from callbacks used by later allocations of the same source.
    status = __Rp1CloseAllocation(child, principal, request->Allocation);
    if (status != OS_EOK) {
        // Attach may have returned busy before checking ownership or strategy.
        // If Close confirms there is no attachment, release the reservation.
        if (__Rp1ChildSource(child, 0)->MessageReady ||
            !__Rp1PendingCloseFinished(status)) {
            return status;
        }
    }
    
    for (unsigned int i = 0; i < count; i++) {
        source = __Rp1ChildSource(child, i);
        memset(source, 0, sizeof(struct Rp1Source));
    }
    
    TRACE("Rp1RuntimeControl: released device %u allocation %u (%u sources)",
          child->DeviceId, request->Allocation, count);
    return OS_EOK;
}
