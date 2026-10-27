/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

#include <limits.h>
#include <ddk/types/interrupt.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/interrupts/provider.h>
#include <ddk/devices/bus.h>
#include <ddk/resources.h>
#include <ddk/utils.h>
#include <string.h>

#include <bus/pci/host-private.h>
#include <bus/pci/function.h>
#include <bus/pci/config.h>
#include <bus/pci/device.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/dma.h>
#include <devices.h>

static oserr_t
__PciBusControl(
    _In_ PciDevice_t*              device,
    _In_ struct OSIOCtlBusControl* request)
{
    uint16_t settings;
    uint16_t requested;

    TRACE("__PciBusControl(%u:%u:%u.%u, flags=0x%x)",
        device->Host->Identification.Segment, device->Bus, device->Slot,
        device->Function, request->Flags);

    // Do not change command bits while this handler forbids activation.
    if (device->Handler != NULL && device->Handler->BlockActivation) {
        WARNING("__PciBusControl activation blocked for %u:%u:%u.%u",
            device->Host->Identification.Segment, device->Bus, device->Slot, device->Function);
        return OS_ENOTSUPPORTED;
    }

    // Preserve command bits that this request does not control.
    settings = PciRead16(device->Host, device->Bus, device->Slot, device->Function, 0x04);

    // Reset these bits before applying the requested state.
    settings &= ~(PCI_COMMAND_BUSMASTER | PCI_COMMAND_FASTBTB
                  | PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_INTDISABLE);

    // An active MSI reservation owns the interrupt state until it is released.
    if (device->Msi.Active || !(request->Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
        settings |= PCI_COMMAND_INTDISABLE;
    }

    // I/O and memory decoding.
    if (request->Flags & __DEVICEMANAGER_IOCTL_MMIO_ENABLE) {
        settings |= PCI_COMMAND_MMIO;
    }
    if (request->Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
        settings |= PCI_COMMAND_PORTIO;
    }

    // Bus mastering.
    if (request->Flags & __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE) {
        settings |= PCI_COMMAND_BUSMASTER;
    }

    // Fast back-to-back transactions.
    if (request->Flags & __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE) {
        settings |= PCI_COMMAND_FASTBTB;
    }

    // Memory write and invalidate is enabled here but never cleared.
    if (request->Flags & __DEVICEMANAGER_IOCTL_MEMWRTINVD_ENABLE) {
        settings |= PCI_COMMAND_MEMWRITE;
    }

    requested = settings;
    if (device->Msix != NULL) {
        // Keep memory access enabled and interrupts disabled until the MSI-X
        // table is restored and unmapped. Save the requested bits for release.
        settings |= PCI_COMMAND_INTDISABLE | PCI_COMMAND_MMIO;
    }

    PciWrite16(device->Host, device->Bus, device->Slot, device->Function, 0x04, settings);

    if (device->Msix != NULL) {
        PciMsixUpdateCommand(device->Msix, requested);
    }

    // Remember the requested INTx state because the reservation keeps INTx
    // disabled in hardware until release restores the command register.
    if (device->Msi.Active) {
        device->Msi.Command &= ~PCI_COMMAND_INTDISABLE;
        if (!(request->Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
            device->Msi.Command |= PCI_COMMAND_INTDISABLE;
        }
    }
    return OS_EOK;
}

static oserr_t
__PciIoctlDevice(
    _In_    PciDevice_t* device,
    _In_    int          direction,
    _In_    unsigned int reg,
    _InOut_ size_t*      value,
    _In_    size_t       width)
{
    size_t limit = device->Host->IsExtended ? 4096 : 256;

    TRACE("__PciIoctlDevice(%u:%u:%u.%u, direction=%i, reg=0x%x, width=%u)",
        device->Host->Identification.Segment, device->Bus, device->Slot,
        device->Function, direction, reg, (unsigned)width);

    // Reject unsupported sizes and misaligned or out-of-range accesses before
    // they reach hardware configuration space.
    if (direction != __DEVICEMANAGER_IOCTL_EXT_READ && direction != __DEVICEMANAGER_IOCTL_EXT_WRITE) {
        WARNING("__PciIoctlDevice invalid direction %i", direction);
        return OS_EINVALPARAMS;
    }

    if (width != 1 && width != 2 && width != 4) {
        WARNING("__PciIoctlDevice invalid access width %llu", (unsigned long long)width);
        return OS_EINVALPARAMS;
    }
    
    if (reg >= limit || width > limit - reg || (reg & (width - 1)) != 0) {
        WARNING("__PciIoctlDevice invalid register 0x%x, width %u, limit %u",
            reg, (unsigned)width, (unsigned)limit);
        return OS_EINVALPARAMS;
    }

    if (direction == __DEVICEMANAGER_IOCTL_EXT_READ) {
        *value = PciDeviceRead(device, reg, width);
        return OS_EOK;
    }

    // Reads remain useful during a reservation, but writes could replace the
    // configuration that the interrupt code must restore later.
    if (device->Msi.Active || device->Msix != NULL) {
        WARNING("__PciIoctlDevice write blocked by an active interrupt reservation");
        return OS_EBUSY;
    }

    if (device->Handler != NULL && device->Handler->BlockActivation) {
        WARNING("__PciIoctlDevice configuration writes blocked by device policy");
        return OS_ENOTSUPPORTED;
    }

    PciDeviceWrite(device, reg, (uint32_t)*value, width);
    return OS_EOK;
}

oserr_t
PciDeviceAddReference(
    _InOut_ PciDevice_t* device)
{
    // Child buses and the PCI registry share this count. Host destruction must
    // wait for every owner before freeing the device and its attachment.
    PciCriticalSectionEnter();
    // Never wrap the count: a wrapped value could make a live device look unused.
    if (device->ProviderReferences == UINT_MAX) {
        PciCriticalSectionLeave();
        ERROR("PciDeviceAddReference provider reference count exhausted");
        return OS_EOVERFLOW;
    }
    
    device->ProviderReferences++;
    PciCriticalSectionLeave();
    return OS_EOK;
}

void
PciDeviceRelease(
    _InOut_ PciDevice_t* device)
{
    // The caller must hold a reference. Hardware cleanup belongs to the host and
    // must not run inside a registry callback or while another owner remains.
    PciCriticalSectionEnter();
    device->ProviderReferences--;
    PciCriticalSectionLeave();
}

static oserr_t
__PciProviderAddReference(
    _In_ void* context)
{
    // Adapt the registry's untyped context to the shared PCI ownership helper.
    return PciDeviceAddReference(context);
}

static void
__PciProviderRelease(
    _In_ void* context)
{
    // Registry and attached-bus owners must balance the same reference count.
    PciDeviceRelease(context);
}

static oserr_t
__PciInterruptControl(
    _In_    PciDevice_t*         device,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length);

static oserr_t
__PciProviderControl(
    _In_ void*               context,
    _In_ uuid_t              principal,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    PciDevice_t*             device = context;
    struct DeviceResources*  resources = buffer;
    struct OSIOCtlBusControl activation = { 0 };
    enum DeviceActivation    stage;
    oserr_t                  status;

    (void)principal;

    if (request == OSIOCTLREQUEST_DEVICE_RESOURCES) {
        if (buffer == NULL || length != sizeof(struct DeviceResources)) {
            return OS_EINVALPARAMS;
        }
        
        memset(resources, 0, sizeof(struct DeviceResources));
        for (unsigned int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
            if (device->PublishedIo[i].Id != UUID_INVALID) {
                resources->Registers[resources->Count++] = device->PublishedIo[i];
            }
        }
        return OS_EOK;
    }

    if (request == OSIOCTLREQUEST_DEVICE_ACTIVATE) {
        if (buffer == NULL || length != sizeof(stage)) {
            return OS_EINVALPARAMS;
        }
        
        stage = *(enum DeviceActivation*)buffer;
        if (stage != DevicePrepare && stage != DeviceStart && stage != DeviceStop) {
            return OS_EINVALPARAMS;
        }
        
        activation.Flags = __DEVICEMANAGER_IOCTL_MMIO_ENABLE;
        if (stage == DeviceStart) {
            activation.Flags |= __DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE;
        }

        PciCriticalSectionEnter();
        status = __PciBusControl(device, &activation);
        PciCriticalSectionLeave();
        return status;
    }

    // Interrupt requests use the same lock as register access so a reservation
    // cannot race with another operation changing the device configuration.
    if (request == OSIOCTLREQUEST_PCI_INTERRUPT_QUERY ||
        request == OSIOCTLREQUEST_PCI_INTERRUPT) {
        PciCriticalSectionEnter();
        status = __PciInterruptControl(context, request, buffer, length);
        PciCriticalSectionLeave();
        return status;
    }

    // All other supported requests were handled above.
    if (request != OSIOCTLREQUEST_BUS_CONTROL) {
        WARNING("__PciProviderControl unsupported request %u", request);
        return OS_ENOTSUPPORTED;
    }

    if (buffer == NULL || length < sizeof(struct OSIOCtlBusControl)) {
        WARNING("__PciProviderControl invalid bus-control buffer, length %llu",
            (unsigned long long)length);
        return OS_EINVALPARAMS;
    }

    // Bus control changes the same command register used by interrupt setup.
    PciCriticalSectionEnter();
    status = __PciBusControl(context, buffer);
    PciCriticalSectionLeave();
    return status;
}

static uint32_t
__PciMsiRead(
    _In_ void*  context,
    _In_ size_t offset,
    _In_ size_t width)
{
    // MSI helpers use this callback to share the device's configuration path.
    return PciDeviceRead(context, offset, width);
}

static void
__PciMsiWrite(
    _In_ void*    context,
    _In_ size_t   offset,
    _In_ uint32_t value,
    _In_ size_t   width)
{
    // Keep MSI writes on the same path as other configuration updates.
    PciDeviceWrite(context, offset, value, width);
}

static oserr_t
__PciInterruptControl(
    _In_    PciDevice_t*         device,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct PciInterruptCapabilities   capabilities;
    struct PciInterruptSupport*       support;
    const struct PciInterruptRequest* interrupt;
    oserr_t                           status;
    struct PciConfigOperations        access = {
        device,
        __PciMsiRead,
        __PciMsiWrite
    };

    TRACE("__PciInterruptControl(%u:%u:%u.%u, request=%u, length=%llu)",
        device->Host->Identification.Segment, device->Bus, device->Slot,
        device->Function, request, (unsigned long long)length);

    if (buffer == NULL) {
        WARNING("__PciInterruptControl missing request buffer");
        return OS_EINVALPARAMS;
    }

    if (request == OSIOCTLREQUEST_PCI_INTERRUPT_QUERY) {
        support = buffer;

        if (length != sizeof(struct PciInterruptSupport)) {
            WARNING("__PciInterruptControl invalid query length %llu",
                (unsigned long long)length);
            return OS_EINVALPARAMS;
        }

        memset(support, 0, sizeof(*support));

        // Report no strategies when policy or device type prevents their use.
        if (!PciIsMsiAvailable() || device->Host->InterruptControllerId == UUID_INVALID ||
            !PciDeviceAllowsDriverBinding(device)) {
            TRACE("__PciInterruptControl message interrupts unavailable by policy or device type");
            return OS_EOK;
        }

        status = PciInterruptProbe(&access, &capabilities);
        if (status == OS_ENOTSUPPORTED) {
            TRACE("__PciInterruptControl no message interrupt capability");
            return OS_EOK;
        }
        if (status != OS_EOK) {
            WARNING("__PciInterruptControl capability probe failed: %u", status);
            return status;
        }

        support->ControllerID = device->Host->InterruptControllerId;
        support->MsiCapacity = capabilities.MsiCapacity;
        support->MsixCapacity = capabilities.MsixCapacity;

        if (capabilities.MsiOffset != 0) {
            support->Strategies = INTERRUPT_STRATEGY_MSI;
        }

        // The capability can exist while its table is inaccessible. Check the
        // BAR mapping before advertising MSI-X support.
        if (capabilities.MsixOffset != 0) {
            status = PciMsixDeviceProbe(device);
            if (status != OS_EOK) {
                WARNING("__PciInterruptControl MSI-X table unavailable: %u", status);
            } else {
                support->Strategies |= INTERRUPT_STRATEGY_MSIX;
            }
        }
        return OS_EOK;
    }

    if (length != sizeof(struct PciInterruptRequest)) {
        WARNING("__PciInterruptControl invalid control length %llu", (unsigned long long)length);
        return OS_EINVALPARAMS;
    }

    interrupt = buffer;
    if (!PciInterruptRequestIsValid(interrupt)) {
        WARNING("__PciInterruptControl invalid operation %u for strategy %u",
            interrupt->Operation, interrupt->Strategy);
        return OS_EINVALPARAMS;
    }
    
    // Keep one interrupt mode at a time so each mode can restore its own saved
    // configuration without overwriting the other's state.
    if (interrupt->Strategy == INTERRUPT_STRATEGY_MSIX && device->Msi.Active) {
        WARNING("__PciInterruptControl MSI-X request conflicts with active MSI");
        return OS_EBUSY;
    }
    
    if (interrupt->Strategy == INTERRUPT_STRATEGY_MSI && device->Msix != NULL) {
        WARNING("__PciInterruptControl MSI request conflicts with active MSI-X");
        return OS_EBUSY;
    }

    // New reservations require current platform support and an eligible device.
    // A release must finish even if policy changed after the reservation.
    if (interrupt->Operation == PciInterruptReserve) {
        if (!PciIsMsiAvailable() || device->Host->InterruptControllerId == UUID_INVALID ||
            !PciDeviceAllowsDriverBinding(device)) {
            WARNING("__PciInterruptControl reservation blocked by device policy or type");
            return OS_ENOTSUPPORTED;
        }
    }
    
    if (interrupt->Strategy == INTERRUPT_STRATEGY_MSIX) {
        return PciMsixDeviceControl(device, interrupt);
    }
    return PciMsiControl(&access, &device->Msi, interrupt);
}

static oserr_t
__PciProviderAccessRegister(
    _In_    void*        context,
    _In_    int          direction,
    _In_    unsigned int reg,
    _InOut_ size_t*      value,
    _In_    size_t       width)
{
    oserr_t status;

    // Serialize this path with bus control and MSI operations, which also update
    // PCI configuration registers and saved restoration state.
    PciCriticalSectionEnter();
    status = __PciIoctlDevice(context, direction, reg, value, width);
    PciCriticalSectionLeave();
    return status;
}

static oserr_t
__PciPrepareDma(
    _In_  void*                    context,
    _In_  uuid_t                   principal,
    _Out_ struct DmDmaDescription* description)
{
    PciDevice_t*             device = context;
    struct PciDmaDescription configured;
    oserr_t                  status;

    (void)principal;

    PciCriticalSectionEnter();
    status = PciHostGetDmaDescription(device->Host, &configured);
    PciCriticalSectionLeave();
    if (status != OS_EOK) {
        return status;
    }
    
    description->HostId = configured.Host.HostId;
    description->CachePolicy = configured.CachePolicy;
    description->Map = configured.Map;
    return OS_EOK;
}

const struct DmDeviceProviderOperations g_pciDeviceProviderOperations = {
    .PrepareDma = __PciPrepareDma,
    .AddReference = __PciProviderAddReference,
    .Release = __PciProviderRelease,
    .Control = __PciProviderControl,
    .AccessRegister = __PciProviderAccessRegister
};

int
PciDeviceAllowsDriverBinding(
    _In_ const PciDevice_t* device)
{
    // Bridges are managed through their child bus and handled functions already
    // have an owner, so neither should be offered to a generic driver.
    return !device->IsBridge && device->Handler == NULL;
}
