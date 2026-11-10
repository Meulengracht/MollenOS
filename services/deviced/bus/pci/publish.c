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

//#define __TRACE

#include <bus/pci/publish.h>
#include <bus/pci/host-private.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/strings.h>
#include <core/io.h>
#include <device-bus.h>
#include <firmware/pci.h>
#include <ddk/devices/bus.h>
#include <ddk/interrupts/bus.h>
#include <ddk/types/interrupt-controller.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void
__PciAddIdeChannelPorts(
    _InOut_ DeviceIo_t* spaces,
    _In_    size_t      base,
    _In_    size_t      control)
{
    DeviceIo_t io;
    oserr_t    status;

    // Use fixed legacy addresses only where the controller reported no BAR.
    if (spaces[0].Type == DeviceIoInvalid) {
        memset(&io, 0, sizeof(io));
        status = CreateDevicePortIo(&io, base, 8);
        if (status != OS_EOK) {
            ERROR("__PciAddIdeChannelPorts: failed to create data ports at 0x%x (%u)",
                (unsigned)base, status);
        } else {
            spaces[0] = io;
        }
    }

    // Some controllers report one of the two entries but omit the other.
    if (spaces[1].Type == DeviceIoInvalid) {
        memset(&io, 0, sizeof(io));
        status = CreateDevicePortIo(&io, control, 4);
        if (status != OS_EOK) {
            ERROR("__PciAddIdeChannelPorts: failed to create control ports at 0x%x (%u)",
                (unsigned)control, status);
        } else {
            spaces[1] = io;
        }
    }
}

static void
__PciAddLegacyIdePorts(
    _In_    const PciDevice_t* pciDevice,
    _InOut_ BusDevice_t*       device)
{
    // The host must expose port I/O directly to use fixed addresses.
    if (pciDevice->Host->IoResourcePolicy != PciIoResourcePorts) {
        return;
    }

    // Fixed IDE addresses apply only to IDE controllers.
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
    DmDeviceRegistration_t registration = {
        .Kind = DmDeviceDescriptionBus,
        .Bus = {
            .Operations = &g_pciDeviceBusOperations,
            .Context = pciDevice
        }
    };

    BusDevice_t* device;
    oserr_t      status;

    TRACE("__PublishPciDevice(%u:%u:%u.%u)", pciDevice->Host->Identification.Segment,
        pciDevice->Bus, pciDevice->Slot, pciDevice->Function);

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        ERROR("__PublishPciDevice: failed to allocate description for %u:%u:%u.%u (%u)",
            pciDevice->Host->Identification.Segment,
            pciDevice->Bus, pciDevice->Slot, pciDevice->Function, OS_EOOM);
        return OS_EOOM;
    }

    memset(device, 0, sizeof(BusDevice_t));
    device->Base.Id       = UUID_INVALID;
    device->Base.ParentId = pciDevice->Parent->DeviceId;
    device->Base.Length   = sizeof(BusDevice_t);

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

    device->Type     = BusTypePci;
    device->Segment  = (unsigned int)pciDevice->Host->Identification.Segment;
    device->Bus      = pciDevice->Bus;
    device->Slot     = pciDevice->Slot;
    device->Function = pciDevice->Function;

    device->InterruptLine        = pciDevice->InterruptLine;
    device->InterruptPin         = (int)pciDevice->Header->InterruptPin;
    device->InterruptAcpiConform = pciDevice->AcpiConform;

    // The registry frees the description string. PciToString returns static text.
    if (device->Base.Identification.Description == NULL) {
        ERROR("__PublishPciDevice: failed to copy name for %u:%u:%u.%u (%u)",
            pciDevice->Host->Identification.Segment,
            pciDevice->Bus, pciDevice->Slot, pciDevice->Function, OS_EOOM);
        free(device);
        return OS_EOOM;
    }

    if (PciDeviceAllowsDriverBinding(pciDevice)) {
        // Expose BAR mappings for devices that can bind generic drivers.
        // Bridges and devices with bus handlers manage their resources separately.
        PciRegisterBars(pciDevice->Host, device, pciDevice->Resources.Bars);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
        __PciAddLegacyIdePorts(pciDevice, device);
#endif
    }

    // Keep the I/O handles in the PCI inventory if registration fails, so host
    // cleanup can retry releasing them before freeing the PCI inventory entry.
    memcpy(pciDevice->PublishedIo, device->IoSpaces, sizeof(pciDevice->PublishedIo));
    registration.Description = &device->Base;
    status = DmPublicationAdd(
        &pciDevice->Host->Publication.Group,
        &registration,
        PciDeviceAllowsDriverBinding(pciDevice),
        &pciDevice->DeviceId
    );

    // The registry frees registered descriptions. Host cleanup releases I/O
    // handles even when registration fails.
    if (status != OS_EOK) {
        ERROR("__PublishPciDevice: DmPublicationAdd failed for %u:%u:%u.%u (%u)",
            pciDevice->Host->Identification.Segment,
            pciDevice->Bus, pciDevice->Slot, pciDevice->Function, status);
        free(device->Base.Identification.Description);
        free(device);
        return status;
    }

    return OS_EOK;
}

static oserr_t
__PublishPciRoot(
    _In_ PciDevice_t* root)
{
    DmDeviceRegistration_t registration = { .Kind = DmDeviceDescriptionGeneric };
    Device_t*              description;
    oserr_t                status;

    description = calloc(1, sizeof(Device_t));

    if (description == NULL) {
        ERROR("__PublishPciRoot: failed to allocate description for host %u segment %u (%u)",
            root->Host->Identification.HostId, root->Host->Identification.Segment, OS_EOOM);
        return OS_EOOM;
    }

    description->Length = sizeof(Device_t);
    description->Identification.Description = strdup("PCI host");

    if (description->Identification.Description == NULL) {
        ERROR("__PublishPciRoot: failed to copy name for host %u segment %u (%u)",
            root->Host->Identification.HostId, root->Host->Identification.Segment, OS_EOOM);
        free(description);
        return OS_EOOM;
    }

    registration.Description = description;
    status = DmPublicationAdd(
        &root->Host->Publication.Group,
        &registration,
        0,
        &root->DeviceId
    );

    // If the group rejects this entry, we must free its description here.
    if (status != OS_EOK) {
        ERROR("__PublishPciRoot: DmPublicationAdd failed for host %u segment %u (%u)",
            root->Host->Identification.HostId, root->Host->Identification.Segment, status);
        free(description->Identification.Description);
        free(description);
        return status;
    }

    return OS_EOK;
}

static oserr_t
__PciRetryAttachments(
    _In_ PciDevice_t* device)
{
    struct DmStartupResult result;
    oserr_t                status;

    __DmStartupFromStatus(device->AttachmentStatus, &result);
    if (result.Outcome == DmStartupRetry) {
        status = device->Handler->Attach(device, &device->Resources, &device->Attachment);
        device->AttachmentStatus = status;
        
        __DmStartupFromStatus(status, &result);
        if (result.Outcome == DmStartupRetry) {
            TRACE("__PciRetryAttachments: Attach pending for %u:%u:%u.%u (%u)",
                device->Host->Identification.Segment,
                device->Bus, device->Slot, device->Function, status);
            return status;
        }
        if (status != OS_EOK) {
            ERROR("__PciRetryAttachments: Attach failed for %u:%u:%u.%u (%u)",
                device->Host->Identification.Segment,
                device->Bus, device->Slot, device->Function, status);
        }
    }

    foreach (element, &device->Children) {
        status = __PciRetryAttachments(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }

    return OS_EOK;
}

static oserr_t
__PciStagePublication(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    // Publish parents first so each child can refer to an assigned parent ID.
    if (device->Parent == NULL) {
        status = __PublishPciRoot(device);
    } else {
        status = __PublishPciDevice(device);
    }
    if (status != OS_EOK) {
        return status;
    }

    // Functions with their own bus add children to the same group. Ordinary
    // endpoints have no attachment and need no extra descriptions.
    if (device->Attachment != NULL && device->Handler->Publish != NULL) {
        status = device->Handler->Publish(
            device->Attachment,
            device,
            &device->Host->Publication.Group
        );

        // The caller removes the entire partial group if any attachment fails.
        if (status != OS_EOK) {
            ERROR("__PciStagePublication: handler Publish failed for %u:%u:%u.%u (%u)",
                device->Host->Identification.Segment,
                device->Bus, device->Slot, device->Function, status);
            return status;
        }
    }

    foreach (element, &device->Children) {
        status = __PciStagePublication(element->value);

        // No drivers may start while part of the host's tree is missing.
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__PciReleasePublishedIo(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    // The publication group must be removed before releasing I/O handles.
    // Release children before their parent.
    foreach (element, &device->Children) {
        status = __PciReleasePublishedIo(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = DmIoDestroy(device->PublishedIo, 6);
    if (status != OS_EOK) {
        WARNING("__PciReleasePublishedIo: I/O cleanup incomplete for device %u at %u:%u:%u.%u (%u)",
            device->DeviceId, device->Host->Identification.Segment,
            device->Bus, device->Slot, device->Function, status);
        return status;
    }
    return OS_EOK;
}

static void
__PciPrepareHardware(
    _In_  PciDevice_t*            device,
    _Out_ struct DmStartupResult* result)
{
    if (device->Attachment != NULL && device->Handler->PrepareHardware != NULL) {
        PciCriticalSectionEnter();
        device->Handler->PrepareHardware(device->Attachment, result);
        PciCriticalSectionLeave();

        if (result->Outcome != DmStartupComplete) {
            ERROR("__PciPrepareHardware: device %u at %u:%u:%u.%u not ready (%u); descriptions kept",
                device->DeviceId, device->Host->Identification.Segment,
                device->Bus, device->Slot, device->Function, result->Status);
            return;
        }
    }

    foreach (element, &device->Children) {
        __PciPrepareHardware(element->value, result);
        if (result->Outcome != DmStartupComplete) {
            return;
        }
    }
    __DmStartupFromStatus(OS_EOK, result);
}

static int
__PciHasInterruptReservation(
    _In_ const PciDevice_t* device)
{
    // Interrupt reservations need the PCI callbacks and resources during cleanup.
    // Any active reservation keeps the whole host published.
    if (device->Msi.Active || device->Msix != NULL) {
        return 1;
    }

    foreach (element, &device->Children) {
        if (__PciHasInterruptReservation(element->value)) {
            return 1;
        }
    }
    return 0;
}

static oserr_t
__PciPrepareAttachmentRemoval(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    TRACE("__PciPrepareAttachmentRemoval(device=%u, bus=%u, slot=%u, function=%u)",
          device->DeviceId, device->Bus, device->Slot, device->Function);
    if (device->Attachment != NULL && device->Handler->PrepareRemoval != NULL) {
        status = device->Handler->PrepareRemoval(device->Attachment);
        if (status != OS_EOK) {
            WARNING("__PciPrepareAttachmentRemoval: PrepareRemoval failed for device %u at %u:%u:%u.%u (%u)",
                device->DeviceId, device->Host->Identification.Segment,
                device->Bus, device->Slot, device->Function, status);
            return status;
        }
    }
    foreach (element, &device->Children) {
        status = __PciPrepareAttachmentRemoval(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
PciUnpublishDevice(
    _In_ PciDevice_t* device)
{
    struct PciPublication* publication = &device->Host->Publication;
    oserr_t                status;

    TRACE("PciUnpublishDevice(host=%u)", device->Host->Identification.HostId);

    // A host shares one publication group with its attachments. Removing just
    // one subtree would leave the group's saved IDs pointing at freed objects.
    if (device->Parent != NULL) {
        WARNING("PciUnpublishDevice: device %u at %u:%u:%u.%u is not a host root (%u)",
            device->DeviceId, device->Host->Identification.Segment,
            device->Bus, device->Slot, device->Function, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    // Interrupt cleanup must finish before removing PCI callbacks or BAR mappings.
    // Leave the host published when cleanup still needs to be retried.
    PciCriticalSectionEnter();
    if (__PciHasInterruptReservation(device)) {
        PciCriticalSectionLeave();
        WARNING("PciUnpublishDevice: host %u segment %u has active interrupt reservations (%u)",
            device->Host->Identification.HostId, device->Host->Identification.Segment, OS_EBUSY);
        return OS_EBUSY;
    }
    status = __PciPrepareAttachmentRemoval(device);
    PciCriticalSectionLeave();
    if (status != OS_EOK) {
        return status;
    }

    // A kernel interrupt allocation made before PCI reservation has no PCI resources.
    // It can still be released after this publication is removed.
    publication->CleanupPending = 1;
    status = DmPublicationRemove(&publication->Group);
    // Keep I/O resources until every device entry has finished its release callback.
    if (status != OS_EOK) {
        WARNING("PciUnpublishDevice: publication removal incomplete for host %u segment %u (%u)",
            device->Host->Identification.HostId, device->Host->Identification.Segment, status);
        return status;
    }

    status = __PciReleasePublishedIo(device);
    if (status != OS_EOK) {
        return status;
    }

    // Both the registry entries and I/O handles have been removed, so a failed
    // host may start again. Unknown configuration stays until host teardown.
    publication->CleanupPending = 0;
    if (device->Host->Status.State == PciHostFailed) {
        __PciHostSetState(device->Host, PciHostReady, OS_EOK);
    }
    return OS_EOK;
}

void
PciPublishDevice(
    _In_  PciDevice_t*            device,
    _Out_ struct DmStartupResult* result)
{
    struct PciPublication*     publication = &device->Host->Publication;
    struct DmPublicationGroup* group = &publication->Group;
    oserr_t                    status;
    oserr_t                    cleanup;

    InterruptMessageControllerIdentity_t query = {
        .Version = 1,
        .Kind = InterruptMessageControllerKindPlatform
    };

    TRACE("PciPublishDevice(host=%u)", device->Host->Identification.HostId);

    if (device->Host->Status.State == PciHostRemoving) {
        TRACE("PciPublishDevice: host %u segment %u removal pending (%u)",
            device->Host->Identification.HostId, device->Host->Identification.Segment, OS_EBUSY);
        __DmStartupRetryResult(OS_EBUSY, result);
        return;
    }
    if (device->Host->Status.State == PciHostScanIncomplete) {
        ERROR("PciPublishDevice: scan failed for host %u segment %u (%u)",
            device->Host->Identification.HostId, device->Host->Identification.Segment,
            device->Host->Status.Error);
        __DmStartupFromStatus(device->Host->Status.Error, result);
        return;
    }

    // Publication must cover the whole host before any driver can start.
    if (device->Parent != NULL) {
        WARNING("PciPublishDevice: device %u at %u:%u:%u.%u is not a host root (%u)",
            device->DeviceId, device->Host->Identification.Segment,
            device->Bus, device->Slot, device->Function, OS_EINVALPARAMS);
        __DmStartupFromStatus(OS_EINVALPARAMS, result);
        return;
    }

    // Finish removing the old group before adding descriptions for a new attempt.
    if (publication->CleanupPending) {
        status = PciUnpublishDevice(device);
        if (status != OS_EOK) {
            __DmStartupRetryResult(status, result);
            return;
        }
    }

    // Register the complete host before preparing shared hardware. If adding a
    // description fails, remove the partial group. Hardware preparation failures
    // leave the descriptions registered so a retry can use the same IDs.
    if (group->State == DmPublicationCollecting) {
        status = PciAssignHostBars(device);
        if (status != OS_EOK) {
            __DmStartupFromStatus(status, result);
            return;
        }

        // Finish attaching children before adding their descriptions to the group.
        status = __PciRetryAttachments(device);
        if (status != OS_EOK) {
            __DmStartupFromStatus(status, result);
            return;
        }

        status = __PciStagePublication(device);
        if (status != OS_EOK) {
            // Removal preparation may fail before DmPublicationRemove runs.
            // Mark cleanup pending so a retry removes the partial tree first.
            publication->CleanupPending = 1;

            cleanup = PciUnpublishDevice(device);
            if (cleanup != OS_EOK) {
                __DmStartupRetryResult(cleanup, result);
                return;
            }

            __DmStartupFromStatus(status, result);
            return;
        }

        status = DmPublicationFinish(group);
        if (status != OS_EOK) {
            ERROR("PciPublishDevice: DmPublicationFinish failed for host %u segment %u (%u)",
                device->Host->Identification.HostId, device->Host->Identification.Segment, status);
            __DmStartupFromStatus(status, result);
            return;
        }
    }

    // Retry hardware preparation against the same descriptions. No new drivers
    // may bind until every attachment in this host is ready.
    if (device->Host->InterruptControllerId == UUID_INVALID && PciIsMsiAvailable()) {
        if (device->Host->Firmware != NULL) {
            query.Kind = InterruptMessageControllerKindDeviceTree;
            query.FirmwareNode = device->Host->Firmware->NodeOffset;
        }

        status = DeviceInterruptMessageControllerResolve(
            &query,
            &device->Host->InterruptControllerId
        );
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            // A supported interrupt controller may not have registered yet.
            if (status == OS_ENOENT) {
                TRACE("PciPublishDevice: interrupt controller pending for host %u segment %u (%u)",
                    device->Host->Identification.HostId, device->Host->Identification.Segment, status);
                __DmStartupRetryResult(status, result);
                return;
            }
            ERROR("PciPublishDevice: DeviceInterruptMessageControllerResolve failed for host %u segment %u (%u)",
                device->Host->Identification.HostId, device->Host->Identification.Segment, status);
            __DmStartupFromStatus(status, result);
            return;
        }
    }

    __PciPrepareHardware(device, result);
    if (result->Outcome != DmStartupComplete) {
        return;
    }

    status = DmPublicationEnableBinding(group);
    if (status != OS_EOK) {
        ERROR("PciPublishDevice: DmPublicationEnableBinding failed for host %u segment %u (%u)",
            device->Host->Identification.HostId, device->Host->Identification.Segment, status);
        __DmStartupFromStatus(status, result);
        return;
    }
    __DmStartupFromStatus(OS_EOK, result);
}
