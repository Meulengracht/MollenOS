/**
 * MollenOS
 *
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

#include <component/ic.h>
#include <ddk/io.h>
#include <heap.h>
#include <interrupts.h>

#include "bcm2711-msi.h"
#include "../common/device.h"

enum __BcmMsiState {
    // No caller owns setup; an allocation may try once the PCI host is ready.
    BcmMsiUninitialized,
    // One allocation owns setup; competing allocations must retry later.
    BcmMsiStarting,
    // Register setup and parent activation succeeded; the address is now fixed.
    BcmMsiReady
};

struct __BcmMsiController {
    uintptr_t             Registers;
    uuid_t                ParentId;
    uint64_t              MessageAddress;
    _Atomic(unsigned int) State;
    _Atomic(uint32_t)     Claimed;
};

static uint32_t
__BcmMsiRead(
    _In_ struct __BcmMsiController* controller,
    _In_ uint32_t                  offset)
{
    uint32_t value;

    // Read the device, not a cached compiler copy, and order the read before
    // later memory accesses that depend on its result.
    ReadVolatileMemory((const volatile void*)(controller->Registers + offset), &value, sizeof(value));
    mb();
    return value;
}

static void
__BcmMsiWrite(
    _In_ struct __BcmMsiController* controller,
    _In_ uint32_t                  offset,
    _In_ uint32_t                  value)
{
    // Order device writes before later memory accesses. Setup and pending-bit
    // handling must not have their register operations reordered.
    WriteVolatileMemory((volatile void*)(controller->Registers + offset), &value, sizeof(value));
    mb();
}

static irqstatus_t
__BcmMsiInterrupt(
    _In_ InterruptFunctionTable_t* functions,
    _In_ void*                     context)
{
    struct __BcmMsiController* controller = context;
    uint32_t                   pending;

    (void)functions;
    // Masked messages may still be pending. Leave them untouched until their
    // owner re-enables delivery; only service the currently unmasked snapshot.
    pending = __BcmMsiRead(controller, BCM_MSI_STATUS);
    pending &= ~__BcmMsiRead(controller, BCM_MSI_MASK_STATUS);

    // Clear each message before calling its handler so a new arrival during
    // handling stays pending. Do not keep rereading in this interrupt: a busy
    // device could otherwise keep the CPU here indefinitely. Remaining work
    // keeps the combined level-high parent asserted for another invocation.
    for (uint32_t ordinal = 0; ordinal < BCM2711_MSI_COUNT; ordinal++) {
        uint32_t bit = 1U << ordinal;
        if (pending & bit) {
            __BcmMsiWrite(controller, BCM_MSI_CLEAR, bit);
            InterruptHandleDirect(INTERRUPT_MESSAGE_BASE + ordinal);
        }
    }
    return IRQSTATUS_HANDLED;
}

static oserr_t
__BcmMsiSelectAddress(
    _InOut_ struct __BcmMsiController* controller)
{
    uint32_t low;
    uint32_t code;
    uint32_t revision;
    uint64_t base;
    uint64_t size;
    uint64_t address = BCM_MSI_ADDRESS_LOW;

    // The device service (deviced) resets this block and programs its device-to-
    // RAM window before publishing PCI devices. Boot only reserved our parent
    // interrupt; wait until a driver's first allocation to inspect the finished
    // setup. Reset or window changes after allocation are not supported.
    if (__BcmMsiRead(controller, BCM_PCIE_RESET) & BCM_PCIE_RESET_MASK) {
        return OS_EBUSY;
    }

    low = __BcmMsiRead(controller, BCM_PCIE_STATUS);
    if (low == UINT32_MAX || (low & BCM_PCIE_READY) != BCM_PCIE_READY) {
        return OS_EBUSY;
    }
    
    revision = __BcmMsiRead(controller, BCM_PCIE_REVISION) & BCM_PCIE_REVISION_MASK;
    if (revision < BCM_PCIE_REVISION_MIN || revision == BCM_PCIE_REVISION_MASK) {
        return OS_ENOTSUPPORTED;
    }

    // BAR registers describe where incoming device writes go. The Pi 4 host
    // driver uses only BAR2 for RAM. Refuse enabled extra windows, because we
    // cannot safely choose an interrupt destination without checking them too.
    if ((__BcmMsiRead(controller, BCM_BAR1_LOW) & BCM_BAR_SIZE_MASK) ||
        (__BcmMsiRead(controller, BCM_BAR3_LOW) & BCM_BAR_SIZE_MASK)) {
        return OS_ENOTSUPPORTED;
    }

    low = __BcmMsiRead(controller, BCM_BAR2_LOW);
    code = low & BCM_BAR_SIZE_MASK;
    if (code == 0 || code > BCM_BAR_SIZE_CODE_MAX) {
        return OS_ENOTSUPPORTED;
    }

    // Decode the programmed window, not just firmware's requested RAM length:
    // the host may have rounded that length up to a power of two.
    size = 1ULL << (code + BCM_BAR_SIZE_SHIFT);
    base = ((uint64_t)__BcmMsiRead(controller, BCM_BAR2_HIGH) << BCM_PCIE_ADDRESS_BITS) |
        (low & ~BCM_BAR_SIZE_MASK);
    if ((base & (size - 1)) || size - 1 > UINT64_MAX - base) {
        return OS_EINVALPARAMS;
    }

    // Prefer a destination below 4 GiB for devices with 32-bit message addresses.
    // If RAM covers it, try the higher destination. Never give a driver an
    // address inside RAM: its interrupt write could become a memory write.
    if (address >= base && address - base < size) {
        address = BCM_MSI_ADDRESS_HIGH;
    }

    if (address >= base && address - base < size) {
        return OS_ENOTSUPPORTED;
    }
    
    controller->MessageAddress = address;
    return OS_EOK;
}

static oserr_t
__BcmMsiPrepare(
    _InOut_ struct __BcmMsiController* controller)
{
    unsigned int expected = BcmMsiUninitialized;
    oserr_t      status;

    // Only one allocation may program the shared receiver. Do not spin while
    // another caller is setting it up; the interrupt core can report busy.
    // A ready state also publishes the final message address to later callers.
    if (!atomic_compare_exchange_strong(&controller->State, &expected, BcmMsiStarting)) {
        return expected == BcmMsiReady ? OS_EOK : OS_EBUSY;
    }

    status = __BcmMsiSelectAddress(controller);
    if (status != OS_EOK) {
        goto fail;
    }

    // Stop delivery and disable address matching before replacing its fields.
    // Clear old pending bits so firmware's earlier activity cannot be delivered
    // as a new driver's interrupt. No message has an owner at first setup.
    __BcmMsiWrite(controller, BCM_MSI_MASK_SET, BCM_MSI_ALL_BITS);
    __BcmMsiWrite(controller, BCM_MSI_BAR_LOW, 0);
    __BcmMsiWrite(controller, BCM_MSI_CLEAR, BCM_MSI_ALL_BITS);
    __BcmMsiWrite(controller, BCM_MSI_BAR_HIGH,
        (uint32_t)(controller->MessageAddress >> BCM_PCIE_ADDRESS_BITS));
    __BcmMsiWrite(controller, BCM_MSI_DATA, BCM_MSI_DATA_CONFIG);

    // Enable matching last, then check both the destination and the mask state
    // before enabling the GIC parent. Individual messages remain masked.
    __BcmMsiWrite(controller, BCM_MSI_BAR_LOW,
        (uint32_t)controller->MessageAddress | BCM_MSI_BAR_ENABLE);
    if (__BcmMsiRead(controller, BCM_MSI_BAR_LOW) !=
            ((uint32_t)controller->MessageAddress | BCM_MSI_BAR_ENABLE) ||
        __BcmMsiRead(controller, BCM_MSI_MASK_STATUS) != BCM_MSI_ALL_BITS) {
        status = OS_EUNKNOWN;
        goto disable;
    }

    // Allocation runs outside the core dispatch lock. SetEnabled callbacks must
    // not re-enter the core, so parent activation belongs here instead.
    status = InterruptSetEnabled(controller->ParentId, 1);
    if (status != OS_EOK) {
        goto disable;
    }

    atomic_store(&controller->State, BcmMsiReady);
    return OS_EOK;

disable:
    // A failed setup must not leave an active address match. All messages are
    // still masked, and resetting the state allows a later allocation to retry.
    __BcmMsiWrite(controller, BCM_MSI_BAR_LOW, 0);

fail:
    atomic_store(&controller->State, BcmMsiUninitialized);
    return status;
}

static uint32_t
__BcmMsiCapacity(
    _In_ void* context)
{
    (void)context;
    // Unlike MIP, this receiver always exposes its complete bank of 32 messages.
    return BCM2711_MSI_COUNT;
}

static oserr_t
__BcmMsiAllocate(
    _In_    void*                          context,
    _In_    uint32_t                       ordinal,
    _InOut_ InterruptControllerResource_t* resource)
{
    struct __BcmMsiController* controller = context;
    uint32_t                   bit = 1U << ordinal;
    oserr_t                    status;

    // The host service runs after boot publication. Defer register programming
    // until allocation, when its RAM window and link can be checked.
    status = __BcmMsiPrepare(controller);
    if (status != OS_EOK) {
        return status;
    }

    // Claim independently of the core's dispatch slot so a competing allocation
    // cannot change receiver state before the core rejects its duplicate slot.
    if (atomic_fetch_or(&controller->Claimed, bit) & bit) {
        return OS_EBUSY;
    }

    // Own the pending bit before clearing it. A competing allocation that the
    // core later rejects must never clear a live owner's pending message.
    __BcmMsiWrite(controller, BCM_MSI_CLEAR, bit);

    // There is no separate GIC line for this child. The parent handler dispatches
    // a software table slot; the device selects it with the low five data bits.
    resource->TableIndex = INTERRUPT_MESSAGE_BASE + ordinal;
    resource->Token = ordinal;
    resource->Line = INTERRUPT_NONE;
    resource->Message.Address = controller->MessageAddress;
    resource->Message.Data = BCM_MSI_DATA_BASE | ordinal;
    return OS_EOK;
}

static oserr_t
__BcmMsiSetEnabled(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable)
{
    struct __BcmMsiController* controller = context;

    // Mask commands are per-bit, with no read/modify/write or shared lock.
    // Preserve pending messages across disable/re-enable of the same owner.
    __BcmMsiWrite(controller, enable ? BCM_MSI_MASK_CLEAR : BCM_MSI_MASK_SET,
        1U << resource->Token);
    return OS_EOK;
}

static oserr_t
__BcmMsiRelease(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  retire)
{
    struct __BcmMsiController* controller = context;
    uint32_t                   bit = 1U << resource->Token;

    // Stop delivery and discard this owner's pending message before reuse.
    // Retirement keeps the claim forever, preventing a late device write from
    // reaching an unrelated future owner of the same message number.
    __BcmMsiWrite(controller, BCM_MSI_MASK_SET, bit);
    __BcmMsiWrite(controller, BCM_MSI_CLEAR, bit);
    if (!retire) {
        atomic_fetch_and(&controller->Claimed, ~bit);
    }
    return OS_EOK;
}

oserr_t
Arm64Bcm2711MsiInitialize(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t            gicId,
    _In_ uint32_t            interruptCount)
{
    const DeviceTreeNode_t*     node;
    const DeviceTreeNode_t*     gic;
    struct Arm64Bcm2711MsiRoute route = { 0 };
    struct Arm64Bcm2711MsiRoute candidate;
    struct __BcmMsiController*  controller;
    InterruptDescriptor_t       parent = { 0 };
    oserr_t                     status;
    uuid_t                      id;

    struct InterruptMessageBackend backend = {
        .Operations = {
            .Capacity = __BcmMsiCapacity,
            .AllocateOrdinal = __BcmMsiAllocate,
            .SetEnabled = __BcmMsiSetEnabled,
            .Release = __BcmMsiRelease
        }
    };
    
    InterruptMessageControllerIdentity_t identity = {
        .Version = 1,
        .Kind = InterruptMessageControllerKindDeviceTree
    };
    
    gic = DeviceTreeFindPhandle(tree, gicId);
    if (!gic) {
        return OS_EINVALPARAMS;
    }

    // Find and validate the single supported receiver before reserving any
    // interrupt. An absent receiver is normal on other Raspberry Pi models.
    for (node = tree->Nodes; node; node = node->NextNode) {
        status = Arm64Bcm2711MsiReadRoute(tree, node, gic, interruptCount, &candidate);
        if (status == OS_ENOTSUPPORTED) {
            continue;
        }
        if (status != OS_EOK) {
            return status;
        }
        
        // BCM2711 has one host and one 32-vector receiver. Reject duplicate
        // descriptions before publishing anything or claiming dispatch slots.
        if (route.Host) {
            return OS_EINVALPARAMS;
        }
        route = candidate;
    }

    if (!route.Host) {
        return OS_EOK;
    }

    controller = kmalloc(sizeof(*controller));
    if (!controller) {
        return OS_EOOM;
    }

    memset(controller, 0, sizeof(struct __BcmMsiController));
    controller->ParentId = UUID_INVALID;
    
    controller->Registers = Arm64MapDevice(route.RegisterBase, BCM2711_MSI_REGISTER_LENGTH);
    if (!controller->Registers) {
        status = OS_EOOM;
        goto fail;
    }

    // Reserve and bind the combined level-triggered parent, but leave it
    // disabled. The PCI host is not ready yet; first allocation enables it only
    // after receiver setup has succeeded with every child message masked.
    parent.Line = (int)route.ParentInterrupt;
    parent.Pin = INTERRUPT_NONE;
    parent.Vectors[0] = INTERRUPT_NONE;
    parent.AcpiConform = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_TRIGGERMODE;
    parent.Context = controller;
    parent.ResourceTable.Handler = __BcmMsiInterrupt;
    
    status = InterruptReserve(&parent, INTERRUPT_KERNEL | INTERRUPT_EXCLUSIVE, &controller->ParentId);
    if (status != OS_EOK) {
        goto fail;
    }
    
    status = InterruptBind(controller->ParentId, &parent.ResourceTable);
    if (status != OS_EOK) {
        goto release;
    }

    // Publish the host identity only after the parent handler is bound.
    // Success gives the interrupt subsystem a permanent context and mapping.
    backend.Context = controller;
    identity.FirmwareNode = route.Host->NodeOffset;
    
    status = CreateInterruptMessageController(&identity, &backend, &id);
    if (status == OS_EOK) {
        return OS_EOK;
    }

release:
    // The parent was never enabled, so unregister cannot invoke hardware or
    // leave a reader holding this unpublished context. MMIO maps are permanent.
    (void)InterruptUnregister(controller->ParentId);

fail:
    kfree(controller);
    return status;
}
