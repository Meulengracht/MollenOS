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

#include <bus/pci/device.h>
#include <limits.h>
#include <bus/pci/host-private.h>
#include <bus/pci/function.h>
#include <bus/pci/config.h>
#include <bus/pci/publish.h>
#include <bus/pci/strings.h>
#include <bus/pci/interrupts.h>
#include <ddk/interrupt.h>
#include <devices.h>
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__PciBusControl(
    _In_ PciDevice_t*              device,
    _In_ struct OSIOCtlBusControl* request)
{
    uint16_t settings;
    uint16_t requested;

    // Do not change command bits while the host or this handler forbids activation.
    if (device->Host->DriversBlocked ||
        (device->Handler != NULL && device->Handler->BlockActivation)) {
        return OS_ENOTSUPPORTED;
    }

    // Read value, modify and write back
    settings = PciRead16(device->Host, device->Bus, device->Slot, device->Function, 0x04);

    // Clear all possible flags first
    settings &= ~(PCI_COMMAND_BUSMASTER | PCI_COMMAND_FASTBTB
                  | PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_INTDISABLE);

    // An active MSI reservation owns the interrupt state until it is released.
    if (device->Msi.Active || !(request->Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
        settings |= PCI_COMMAND_INTDISABLE;
    }

    // Handle io/mmio
    if (request->Flags & __DEVICEMANAGER_IOCTL_MMIO_ENABLE) {
        settings |= PCI_COMMAND_MMIO;
    }
    if (request->Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
        settings |= PCI_COMMAND_PORTIO;
    }

    // Handle busmaster
    if (request->Flags & __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE) {
        settings |= PCI_COMMAND_BUSMASTER;
    }

    // Handle fast-b2b
    if (request->Flags & __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE) {
        settings |= PCI_COMMAND_FASTBTB;
    }

    // Handle memory write and invalidate
    if (request->Flags & __DEVICEMANAGER_IOCTL_MEMWRTINVD_ENABLE) {
        settings |= PCI_COMMAND_MEMWRITE;
    }

    requested = settings;
    if (device->Msix != NULL) {
        // Keep memory access enabled and interrupts disabled until the MSI-X
        // table is restored and unmapped. Save the requested bits for Release.
        settings |= PCI_COMMAND_INTDISABLE | PCI_COMMAND_MMIO;
    }

    // Write back settings
    PciWrite16(device->Host, device->Bus, device->Slot, device->Function, 0x04, settings);

    if (device->Msix != NULL) {
        PciMsixUpdateCommand(device->Msix, requested);
    }

    // Remember the requested INTx state because the reservation keeps INTx
    // disabled in hardware until Release restores the command register.
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

    // Reject unsupported sizes and misaligned or out-of-range accesses before
    // they reach hardware configuration space.
    if (direction != __DEVICEMANAGER_IOCTL_EXT_READ && direction != __DEVICEMANAGER_IOCTL_EXT_WRITE) {
        return OS_EINVALPARAMS;
    }
    if (width != 1 && width != 2 && width != 4) {
        return OS_EINVALPARAMS;
    }
    if (reg >= limit || width > limit - reg || (reg & (width - 1)) != 0) {
        return OS_EINVALPARAMS;
    }

    if (direction == __DEVICEMANAGER_IOCTL_EXT_READ) {
        *value = PciDeviceRead(device, reg, width);
        return OS_EOK;
    }

    // Reads remain useful during a reservation, but writes could replace the
    // configuration that the interrupt code must restore later.
    if (device->Msi.Active || device->Msix != NULL) {
        return OS_EBUSY;
    }

    if (device->Host->DriversBlocked ||
        (device->Handler != NULL && device->Handler->BlockActivation)) {
        return OS_ENOTSUPPORTED;
    }

    PciDeviceWrite(device, reg, (uint32_t)*value, width);
    return OS_EOK;
}

oserr_t
PciDeviceRetain(
    _InOut_ PciDevice_t* device)
{
    // Child providers use the same count as the PCI registry provider so host
    // destruction sees every owner before freeing the device and its attachment.
    PciCriticalSectionEnter();
    // Never wrap the count: a wrapped value could make a live device look unused.
    if (device->ProviderReferences == UINT_MAX) {
        PciCriticalSectionLeave();
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
    // Release only ownership. Hardware cleanup belongs to the host owner and
    // must not run inside a registry callback or while another owner remains.
    PciCriticalSectionEnter();
    // Matching the increment here keeps the count change atomic with Retain.
    device->ProviderReferences--;
    PciCriticalSectionLeave();
}

static oserr_t
__PciProviderRetain(
    _In_ void* context)
{
    // Adapt the registry's untyped context to the shared PCI ownership helper.
    return PciDeviceRetain(context);
}

static void
__PciProviderRelease(
    _In_ void* context)
{
    // Registry and attached-bus owners must balance the same reference count.
    PciDeviceRelease(context);
}

static oserr_t
__PciInterruptControl(PciDevice_t* device, enum OSIOCtlRequest request,
                       void* buffer, size_t length);

static oserr_t
__PciProviderControl(
    _In_ void*               context,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    oserr_t status;

    // Interrupt requests use the same lock as register access so a reservation
    // cannot race with another operation changing the device configuration.
    if (request == OSIOCTLREQUEST_PCI_INTERRUPT_QUERY ||
        request == OSIOCTLREQUEST_PCI_INTERRUPT) {
        PciCriticalSectionEnter();
        status = __PciInterruptControl(context, request, buffer, length);
        PciCriticalSectionLeave();
        return status;
    }

    // This provider supports interrupt requests and bus control only.
    if (request != OSIOCTLREQUEST_BUS_CONTROL) {
        return OS_ENOTSUPPORTED;
    }
    if (buffer == NULL || length < sizeof(struct OSIOCtlBusControl)) {
        return OS_EINVALPARAMS;
    }

    // Validate the whole request before passing its buffer to bus control.
    PciCriticalSectionEnter();
    status = __PciBusControl(context, buffer);
    PciCriticalSectionLeave();
    return status;
}

static uint32_t
__PciMsiRead(void* context, size_t offset, size_t width)
{
    // MSI helpers use this callback to share the device's configuration path.
    return PciDeviceRead(context, offset, width);
}

static void
__PciMsiWrite(void* context, size_t offset, uint32_t value, size_t width)
{
    // Keep MSI writes on the same path as other configuration updates.
    PciDeviceWrite(context, offset, value, width);
}

static oserr_t
__PciInterruptControl(
    _In_    PciDevice_t*        device,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*              buffer,
    _In_    size_t             length)
{
    struct PciInterruptCapabilities   capabilities;
    struct PciInterruptSupport*       support;
    const struct PciInterruptRequest* interrupt;
    oserr_t                          status;
    struct PciMsiAccess               access = {
        device,
        __PciMsiRead,
        __PciMsiWrite
    };

    if (buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    if (request == OSIOCTLREQUEST_PCI_INTERRUPT_QUERY) {
        support = buffer;

        if (length != sizeof(struct PciInterruptSupport)) {
            return OS_EINVALPARAMS;
        }

        memset(support, 0, sizeof(*support));

        // Report no strategies when policy or device type prevents their use.
        if (!PciIsMsiAvailable() || device->Host->DriversBlocked ||
            device->IsBridge || device->Handler != NULL) {
            return OS_EOK;
        }

        status = PciInterruptProbe(&access, &capabilities);
        if (status == OS_EOK) {
            support->MsiCapacity = capabilities.MsiCapacity;
            support->MsixCapacity = capabilities.MsixCapacity;
            
            // A capability in config space is not enough; the table must also
            // be safely accessible through this device's BAR mapping.
            if (capabilities.MsiOffset != 0) {
                support->Strategies = INTERRUPT_STRATEGY_MSI;
            }
            if (capabilities.MsixOffset != 0 && PciMsixDeviceProbe(device) == OS_EOK) {
                support->Strategies |= INTERRUPT_STRATEGY_MSIX;
            }
        }
        return status == OS_ENOTSUPPORTED ? OS_EOK : status;
    }

    if (length != sizeof(struct PciInterruptRequest)) {
        return OS_EINVALPARAMS;
    }

    interrupt = buffer;
    if (!PciInterruptRequestIsValid(interrupt)) {
        return OS_EINVALPARAMS;
    }
    
    // Keep one interrupt mode at a time so each mode can restore its own saved
    // configuration without overwriting the other's state.
    if (interrupt->Strategy == INTERRUPT_STRATEGY_MSIX && device->Msi.Active) {
        return OS_EBUSY;
    }
    
    if (interrupt->Strategy == INTERRUPT_STRATEGY_MSI && device->Msix != NULL) {
        return OS_EBUSY;
    }

    // Apply current restrictions to new reservations, but let a release finish
    // even if platform policy changed after the reservation was made.
    if (interrupt->Operation == PciInterruptReserve &&
        (!PciIsMsiAvailable() || device->Host->DriversBlocked ||
         device->IsBridge || device->Handler != NULL)) {
        return OS_ENOTSUPPORTED;
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

static const struct DmDeviceProviderOperations g_pciDeviceProviderOperations = {
    .Retain = __PciProviderRetain,
    .Release = __PciProviderRelease,
    .Control = __PciProviderControl,
    .AccessRegister = __PciProviderAccessRegister
};

static int
__PciAllowsDriverBinding(
    _In_ const PciDevice_t* device)
{
    // Bridges are managed through their child bus and handled functions already
    // have an owner, so neither should be offered to a generic driver.
    return !device->IsBridge && device->Handler == NULL 
            && !device->Host->DriversBlocked;
}

static void
__PciReleaseIoSpaces(
    _InOut_ DeviceIo_t* spaces)
{
    for (unsigned int i = 0; i < 6; i++) {
        // Skip empty slots, and clear released entries so another cleanup pass
        // cannot treat stale data as a live resource.
        if (spaces[i].Type != DeviceIoInvalid) {
            DestroyDeviceIo(&spaces[i]);
            memset(&spaces[i], 0, sizeof(DeviceIo_t));
        }
    }
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void
__PciAddIdeChannelPorts(
    _InOut_ DeviceIo_t* spaces,
    _In_    size_t      base,
    _In_    size_t      control)
{
    // A reported BAR is authoritative; use fixed legacy addresses only for gaps.
    if (spaces[0].Type == DeviceIoInvalid) {
        CreateDevicePortIo(&spaces[0], base, 8);
    }
    
    // Some controllers report one of the two entries but omit the other.
    if (spaces[1].Type == DeviceIoInvalid) {
        CreateDevicePortIo(&spaces[1], control, 4);
    }
}

static void
__PciAddLegacyIdePorts(
    _In_    const PciDevice_t* pciDevice,
    _InOut_ BusDevice_t*       device)
{
    // Fixed ports are only meaningful on hosts that expose port I/O directly.
    if (pciDevice->Host->IoResourcePolicy != PciIoResourcePorts) {
        return;
    }
    
    // These fallback addresses belong to IDE controllers, not other hardware.
    if (pciDevice->Header->Class != PCI_CLASS_STORAGE ||
        pciDevice->Header->Subclass != PCI_STORAGE_SUBCLASS_IDE) {
        return;
    }

    // Firmware may omit IDE BARs in legacy mode. A cleared mode bit means this
    // channel uses its fixed legacy addresses instead of native PCI mappings.
    if ((pciDevice->Header->Interface & 0x1) == 0) {
        __PciAddIdeChannelPorts(&device->IoSpaces[0], 0x1F0, 0x3F6);
    }
    
    // The second channel has its own mode bit and a different pair of ports.
    if ((pciDevice->Header->Interface & 0x4) == 0) {
        __PciAddIdeChannelPorts(&device->IoSpaces[2], 0x170, 0x376);
    }
}
#endif

static oserr_t
__PublishPciDevice(
    _In_ PciDevice_t* pciDevice)
{
    // Setup pci provider
    struct DmDeviceRegistration registration = {
        .Kind = DmDeviceDescriptionBus,
        .Provider = {
            .Operations = &g_pciDeviceProviderOperations,
            .Context = pciDevice
        }
    };

    BusDevice_t* device;
    oserr_t      status;

    device = malloc(sizeof(BusDevice_t));
    // No description can be registered until its storage is available.
    if (device == NULL) {
        return OS_EOOM;
    }

    memset(device, 0, sizeof(BusDevice_t));
    device->Base.Id     = UUID_INVALID;
    device->Base.ParentId = pciDevice->Parent->DeviceId;
    device->Base.Length = sizeof(BusDevice_t);

    device->Base.VendorId  = pciDevice->Header->VendorId;
    device->Base.ProductId = pciDevice->Header->DeviceId;
    device->Base.Class     = PciToDevClass(pciDevice->Header->Class, pciDevice->Header->Subclass);
    device->Base.Subclass  = PciToDevSubClass(pciDevice->Header->Interface);
    device->Base.Identification.Description = strdup(
        PciToString(
            pciDevice->Header->Class,
            pciDevice->Header->Subclass,
            pciDevice->Header->Interface
        )
    );

    device->Type = BusTypePci;
    device->Segment  = (unsigned int)pciDevice->Host->Identification.Segment;
    device->Bus      = pciDevice->Bus;
    device->Slot     = pciDevice->Slot;
    device->Function = pciDevice->Function;

    device->InterruptLine        = pciDevice->InterruptLine;
    device->InterruptPin         = (int)pciDevice->Header->InterruptPin;
    device->InterruptAcpiConform = pciDevice->AcpiConform;

    // The registry outlives firmware data, so copy the name into owned storage.
    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    
    if (__PciAllowsDriverBinding(pciDevice)) {
        // Only devices eligible for generic drivers expose BAR mappings; bridge
        // and handler-owned resources are managed through their own paths.
        PciRegisterBars(pciDevice->Host, device, pciDevice->Resources.Bars);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
        __PciAddLegacyIdePorts(pciDevice, device);
#endif
    }
    
    registration.Description = &device->Base;
    status = DmPublicationAdd(
        &pciDevice->Host->Publication,
        &registration,
        __PciAllowsDriverBinding(pciDevice),
        &pciDevice->DeviceId
    );
    
    // Ownership passes to the publication group only on success; otherwise
    // release the name and any mappings created above.
    if (status != OS_EOK) {
        __PciReleaseIoSpaces(device->IoSpaces);
        free(device->Base.Identification.Description);
        free(device);
        return status;
    }
    
    memcpy(pciDevice->PublishedIo, device->IoSpaces, sizeof(pciDevice->PublishedIo));
    return OS_EOK;
}

static oserr_t
__PublishPciRoot(
    _In_ PciDevice_t* root)
{
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionGeneric };
    Device_t* description;
    oserr_t   status;

    description = calloc(1, sizeof(*description));
    
    // The root needs storage for its description and a name owned by the group.
    if (description == NULL) {
        return OS_EOOM;
    }

    description->Length = sizeof(*description);
    description->Identification.Description = strdup("PCI host");
    
    // Do not register a description with a missing name.
    if (description->Identification.Description == NULL) {
        free(description);
        return OS_EOOM;
    }

    registration.Description = description;
    status = DmPublicationAdd(&root->Host->Publication, &registration, 0, &root->DeviceId);
    
    // The registry takes ownership only after the group accepts this entry.
    if (status != OS_EOK) {
        free(description->Identification.Description);
        free(description);
    }
    return status;
}

static oserr_t
__PciStagePublication(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    // Publish parents first so each child can refer to an assigned parent ID.
    status = device->Parent == NULL ? __PublishPciRoot(device) : __PublishPciDevice(device);
    // Children need their parent's ID, so stop before descending on failure.
    if (status != OS_EOK) {
        return status;
    }

    // Functions with their own bus add children to the same group. Ordinary
    // endpoints have no attachment and need no extra descriptions.
    if (device->Attachment != NULL && device->Handler->Publish != NULL) {
        status = device->Handler->Publish(
            device->Attachment, device, &device->Host->Publication
        );
        
        // The caller removes the entire partial group if any attachment fails.
        if (status != OS_EOK) {
            return status;
        }
    }

    foreach (element, &device->children) {
        status = __PciStagePublication(element->value);
        
        // No drivers may start while part of the host's tree is missing.
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static void
__PciReleasePublishedIo(
    _In_ PciDevice_t* device)
{
    // Release descendants before this device so their mappings stay valid until
    // the whole publication group has been removed.
    foreach (element, &device->children) {
        __PciReleasePublishedIo(element->value);
    }
    __PciReleaseIoSpaces(device->PublishedIo);
}

static int
__PciHasInterruptReservation(
    _In_ const PciDevice_t* device)
{
    // An active reservation anywhere in the tree still needs its provider and
    // resources, so the whole host must remain published.
    if (device->Msi.Active || device->Msix != NULL) {
        return 1;
    }
    
    foreach (element, &device->children) {
        if (__PciHasInterruptReservation(element->value)) {
            return 1;
        }
    }
    return 0;
}

oserr_t
PciUnpublishDevice(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    // A host shares one publication group with its attachments. Removing just
    // one subtree would leave the group's saved IDs pointing at freed objects.
    if (device->Parent != NULL) {
        return OS_EINVALPARAMS;
    }

    // Do not remove providers or BAR mappings while interrupt cleanup may need
    // them. This also leaves the host published so cleanup can be retried.
    PciCriticalSectionEnter();
    if (__PciHasInterruptReservation(device)) {
        PciCriticalSectionLeave();
        return OS_EBUSY;
    }
    PciCriticalSectionLeave();

    status = DmPublicationRemove(&device->Host->Publication);
    // Keep I/O resources until every device entry has released its provider.
    if (status != OS_EOK) {
        return status;
    }
    __PciReleasePublishedIo(device);
    return OS_EOK;
}

oserr_t
PciPublishDevice(
    _In_ PciDevice_t* device)
{
    struct DmPublicationGroup* group = &device->Host->Publication;
    oserr_t                    status;
    oserr_t                    cleanup;

    // Publication must cover the whole host before any driver can start.
    if (device->Parent != NULL) {
        return OS_EINVALPARAMS;
    }

    // Reuse a group only after removal completed; an unfinished removal must
    // not be mixed with descriptions from a new publication attempt.
    if (group->State == DmPublicationRemoved) {
        status = DmPublicationReset(group);
        // Do not start building unless the previous group has been cleared.
        if (status != OS_EOK) {
            return status;
        }
    }

    // Keep existing descriptions when clients may already hold them; repeated
    // calls only retry binding through the publication helper.
    if (group->State != DmPublicationCollecting) {
        return DmPublicationEnableBinding(group);
    }

    status = __PciStagePublication(device);
    // No drivers have started yet, so a failed staging pass can safely remove
    // all entries accumulated for this host.
    if (status != OS_EOK) {
        cleanup = PciUnpublishDevice(device);
        return cleanup == OS_EOK ? status : cleanup;
    }

    status = DmPublicationFinish(group);
    // Finish the complete set before allowing any driver to bind.
    if (status != OS_EOK) {
        return status;
    }
    return DmPublicationEnableBinding(group);
}
