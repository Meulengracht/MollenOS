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
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#define __TRACE
#define __need_static_assert
#include <ddk/barrier.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <os/device.h>
#include <os/types/device.h>
#include <stddef.h>
#include <string.h>
#include <threads.h>
#include <virtio/virtio.h>

#define PCI_STATUS_OFFSET             0x06
#define PCI_STATUS_CAPABILITIES_LIST  0x10
#define PCI_CAPABILITIES_POINTER      0x34
#define PCI_CAPABILITY_MIN_OFFSET     0x40
#define PCI_CAPABILITY_MAX_OFFSET     0xFC
#define PCI_CAPABILITY_MAX_VISITS     48
#define VIRTIO_PCI_COMMON_CFG_LENGTH  offsetof(VirtioPciCommonConfiguration_t, QueueNotifyData)
#define VIRTIO_PCI_NOTIFY_CFG_LENGTH  2
#define VIRTIO_PCI_ISR_CFG_LENGTH     1
#define VIRTIO_RESET_RETRIES          1000

COMPILE_TIME_ASSERT(sizeof(VirtioPciCapability32_t) == 16);
COMPILE_TIME_ASSERT(sizeof(VirtioPciCapability64_t) == 24);
COMPILE_TIME_ASSERT(sizeof(VirtioPciCommonConfiguration_t) == 60);
COMPILE_TIME_ASSERT(sizeof(VirtioPciNotifyCapability_t) == 4);

static oserr_t
__ReadPciConfig(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint32_t              offset,
    _In_  size_t                width,
    _Out_ size_t*               valueOut)
{
    *valueOut = 0;
    return IoctlDeviceEx(
        transport->Device->Base.Id,
        __DEVICEMANAGER_IOCTL_EXT_READ,
        offset,
        valueOut,
        width
    );
}

oserr_t
VirtioPciRegionRead(
    _In_  const VirtioPciRegion_t* region,
    _In_  uint32_t                 offset,
    _In_  size_t                   width,
    _Out_ uint64_t*                valueOut)
{
    size_t absoluteOffset;

    if (region == NULL || region->IoSpace == NULL || valueOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // Ensure the read is within the bounds of the region
    if (offset > region->Length || width > (region->Length - offset)) {
        return OS_EINVALPARAMS;
    }

    absoluteOffset = (size_t)region->Offset + offset;
    switch (width) {
        case 1:
        case 2:
        case 4:
            *valueOut = ReadDeviceIo(region->IoSpace, absoluteOffset, width);
            break;
        case 8: {
            uint32_t lower = (uint32_t)ReadDeviceIo(region->IoSpace, absoluteOffset, 4);
            uint32_t upper;

            rmb();
            upper = (uint32_t)ReadDeviceIo(region->IoSpace, absoluteOffset + 4, 4);
            *valueOut = ((uint64_t)upper << 32) | lower;
        } break;
        default:
            return OS_EINVALPARAMS;
    }

    return OS_EOK;
}

oserr_t
VirtioPciRegionWrite(
    _In_ const VirtioPciRegion_t* region,
    _In_ uint32_t                 offset,
    _In_ uint64_t                 value,
    _In_ size_t                   width)
{
    size_t  absoluteOffset;
    oserr_t oserr;

    if (region == NULL || region->IoSpace == NULL) {
        return OS_EINVALPARAMS;
    }

    // Ensure the write is within the bounds of the region
    if (offset > region->Length || width > (region->Length - offset)) {
        return OS_EINVALPARAMS;
    }

    absoluteOffset = (size_t)region->Offset + offset;
    switch (width) {
        case 1:
        case 2:
        case 4:
            oserr = WriteDeviceIo(region->IoSpace, absoluteOffset, (size_t)value, width);
            break;
        case 8:
            oserr = WriteDeviceIo(region->IoSpace, absoluteOffset, (uint32_t)value, 4);
            if (oserr != OS_EOK) {
                return oserr;
            }

            wmb();
            return WriteDeviceIo(region->IoSpace, absoluteOffset + 4,
                                 (uint32_t)(value >> 32), 4);
            break;
        default:
            return OS_EINVALPARAMS;
    }
    return oserr;
}

static oserr_t
__ReadCapabilityRegion(
    _In_    VirtioPciTransport_t* transport,
    _In_    uint8_t               capabilityOffset,
    _InOut_ VirtioPciRegion_t*    region)
{
    DeviceIo_t* ioSpace;
    size_t      value;
    size_t      ioLength;
    uint32_t    regionOffset;
    uint32_t    regionLength;
    uint8_t     bar;
    oserr_t     oserr;

    // Read the BAR (Base Address Register) associated with this capability
    oserr = __ReadPciConfig(
        transport,
        capabilityOffset + offsetof(VirtioPciCapability32_t, Bar),
        sizeof(uint8_t),
        &value
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    bar = (uint8_t)value;
    if (bar >= __DEVICEMANAGER_MAX_IOSPACES) {
        ERROR("Capability at offset 0x%02X references invalid BAR %u",
              capabilityOffset, bar);
        return OS_EUNKNOWN;
    }

    // Read the offset of the capability region within the BAR
    oserr = __ReadPciConfig(
        transport,
        capabilityOffset + offsetof(VirtioPciCapability32_t, Offset),
        sizeof(uint32_t),
        &value
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    regionOffset = (uint32_t)value;

    // Read the length of the capability region within the BAR
    oserr = __ReadPciConfig(
        transport,
        capabilityOffset + offsetof(VirtioPciCapability32_t, Length),
        sizeof(uint32_t),
        &value
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    regionLength = (uint32_t)value;

    // Validate the capability region within the BAR
    ioSpace = &transport->Device->IoSpaces[bar];
    if (ioSpace->Type == DeviceIoMemoryBased) {
        ioLength = ioSpace->Access.Memory.Length;
    } else if (ioSpace->Type == DeviceIoPortBased) {
        ioLength = ioSpace->Access.Port.Length;
    } else {
        ERROR("Capability at offset 0x%02X references unavailable BAR %u",
              capabilityOffset, bar);
        return OS_EUNKNOWN;
    }
    
    if (regionLength == 0 || regionOffset > ioLength ||
        regionLength > (ioLength - regionOffset)) {
        ERROR("Capability at offset 0x%02X exceeds BAR %u (offset=0x%x, length=0x%x, barLength=0x%" PRIxIN ")",
              capabilityOffset, bar, regionOffset, regionLength, ioLength);
        return OS_EUNKNOWN;
    }

    region->IoSpace = ioSpace;
    region->Offset  = regionOffset;
    region->Length  = regionLength;
    return OS_EOK;
}

static oserr_t
__ParseVendorDataCapability(
    _In_    VirtioPciTransport_t* transport,
    _In_    uint8_t               capabilityOffset)
{
    oserr_t oserr;
    size_t  value;
    uint8_t capabilityLength;
    uint8_t capabilityType;
    TRACE("__ParseVendorDataCapability(offset=0x%02X)", capabilityOffset);

    // Read the length of the vendor-specific capability
    oserr = __ReadPciConfig(
        transport,
        capabilityOffset + offsetof(VirtioPciCapability32_t, CapabilityLength),
        sizeof(uint8_t),
        &value
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to read vendor-specific capability length at offset 0x%02X", capabilityOffset);
        return oserr;
    }
    capabilityLength = (uint8_t)value;

    // Read the type of the vendor-specific capability
    oserr = __ReadPciConfig(
        transport,
        capabilityOffset + offsetof(VirtioPciCapability32_t, ConfigurationType),
        sizeof(uint8_t),
        &value
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to read vendor-specific capability type at offset 0x%02X", capabilityOffset);
        return oserr;
    }
    capabilityType = (uint8_t)value;

    switch (capabilityType) {
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            if (capabilityLength < (sizeof(VirtioPciCapability32_t) +
                                  sizeof(VirtioPciNotifyCapability_t))) {
                ERROR("Invalid capability length for notify capability at offset 0x%02X", capabilityOffset);
                return OS_EUNKNOWN;
            }
            break;
        case VIRTIO_PCI_CAP_PCI_CFG:
            if (capabilityLength < (sizeof(VirtioPciCapability32_t) +
                                  sizeof(VirtioPciConfigurationCapability_t))) {
                ERROR("Invalid capability length for PCI configuration capability at offset 0x%02X", capabilityOffset);
                return OS_EUNKNOWN;
            }
            break;
        case VIRTIO_PCI_CAP_COMMON_CFG:
        case VIRTIO_PCI_CAP_ISR_CFG:
        case VIRTIO_PCI_CAP_DEVICE_CFG:
            break;
        default:
            // Unknown/unsupported capability type, skip it
            TRACE("Unknown/unsupported capability type 0x%02X at offset 0x%02X", capabilityType, capabilityOffset);
            return OS_EOK;
    }

    // Ensure the capability length is valid and the region has not been acquired yet
    if (capabilityLength < sizeof(VirtioPciCapability32_t)) {
        ERROR("Capability length is smaller than the minimum VirtioPciCapability32_t size at offset 0x%02X", capabilityOffset);
        return OS_EUNKNOWN;
    }

    // Ensure the capability fits within the PCI configuration space and has not been acquired yet
    if (capabilityOffset > (UINT8_MAX + 1U - capabilityLength)) {
        ERROR("Capability at offset 0x%02X does not fit within the PCI configuration space", capabilityOffset);
        return OS_EUNKNOWN;
    }

    // Acquire the capability region if it has not been acquired yet
    if (transport->Regions[capabilityType - 1].IoSpace != NULL) {
        WARNING("Capability at offset 0x%02X has already been acquired", capabilityOffset);
        return OS_EOK;
    }

    // Read the capability region into the transport's corresponding region structure.
    oserr = __ReadCapabilityRegion(
        transport,
        capabilityOffset,
        &transport->Regions[capabilityType - 1]
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to read capability region at offset 0x%02X", capabilityOffset);
        return oserr;
    }

    // Read the notify capability if present
    if (capabilityType == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        oserr = __ReadPciConfig(
            transport,
            capabilityOffset + sizeof(VirtioPciCapability32_t) +
                offsetof(VirtioPciNotifyCapability_t, NotifyOffMultiplier),
            sizeof(uint32_t),
            &value
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read notify capability at offset 0x%02X", capabilityOffset);
            return oserr;
        }
        transport->NotifyOffsetMultiplier = (uint32_t)value;
    }
    return OS_EOK;
}

static oserr_t
__ParseCapabilities(
    _In_ VirtioPciTransport_t* transport)
{
    uint8_t visited[256] = { 0 };
    size_t  value;
    uint8_t capabilityOffset;
    int     visits = 0;
    oserr_t oserr;
    TRACE("__ParseCapabilities()");

    // Read the PCI status register to check if the capabilities list is available.
    oserr = __ReadPciConfig(transport, PCI_STATUS_OFFSET, 2, &value);
    if (oserr != OS_EOK) {
        ERROR("Failed to read PCI status register.");
        return oserr;
    }
    
    if (!(value & PCI_STATUS_CAPABILITIES_LIST)) {
        WARNING("PCI capabilities list not available.");
        return OS_ENOTSUPPORTED;
    }

    // Read the initial capability pointer from the PCI configuration space.
    oserr = __ReadPciConfig(transport, PCI_CAPABILITIES_POINTER, 1, &value);
    if (oserr != OS_EOK) {
        ERROR("Failed to read initial PCI capability pointer.");
        return oserr;
    }
    capabilityOffset = (uint8_t)(value & ~3U);

    while (capabilityOffset != 0 && visits++ < PCI_CAPABILITY_MAX_VISITS) {
        uint8_t capabilityId;
        uint8_t nextOffset;

        // The capability offset must be within boundaries of the 
        // PCI configuration space and not previously visited.
        if (capabilityOffset < PCI_CAPABILITY_MIN_OFFSET ||
            capabilityOffset > PCI_CAPABILITY_MAX_OFFSET ||
            visited[capabilityOffset]) {
            return OS_EUNKNOWN;
        }
        visited[capabilityOffset] = 1;

        // Read the vendor-specific capability ID
        oserr = __ReadPciConfig(
            transport,
            capabilityOffset + offsetof(VirtioPciCapability32_t, Vendor),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        capabilityId = (uint8_t)value;

        // Read the next capability offset
        oserr = __ReadPciConfig(
            transport,
            capabilityOffset + offsetof(VirtioPciCapability32_t, CapabilityNext),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        nextOffset = (uint8_t)(value & ~3U);

        // Parse the vendor-specific capability if present
        TRACE("Found capability at offset 0x%02X with ID 0x%02X", capabilityOffset, capabilityId);
        if (capabilityId == VIRTIO_PCI_CAP_VENDOR_SPECIFIC) {
            oserr = __ParseVendorDataCapability(transport, capabilityOffset);
            if (oserr != OS_EOK) {
                return oserr;
            }
        }
        capabilityOffset = nextOffset;
    }

    // Verify that all required capability bars have been acquired and are valid
    // The required ones are
    // - Common configuration
    // - Notification configuration
    // - ISR configuration
    if (capabilityOffset != 0 ||
        transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1].IoSpace == NULL ||
        transport->Regions[VIRTIO_PCI_CAP_NOTIFY_CFG - 1].IoSpace == NULL ||
        transport->Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].IoSpace == NULL ||
        transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1].Length < VIRTIO_PCI_COMMON_CFG_LENGTH ||
        transport->Regions[VIRTIO_PCI_CAP_NOTIFY_CFG - 1].Length < VIRTIO_PCI_NOTIFY_CFG_LENGTH ||
        transport->Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].Length < VIRTIO_PCI_ISR_CFG_LENGTH) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__AcquireCapabilityBars(
    _In_ VirtioPciTransport_t* transport)
{
    for (int i = 0; i < VIRTIO_PCI_CAP_COUNT; i++) {
        DeviceIo_t* ioSpace = transport->Regions[i].IoSpace;
        int         bar;
        if (ioSpace == NULL) {
            continue;
        }

        bar = (int)(ioSpace - &transport->Device->IoSpaces[0]);
        if (!(transport->AcquiredBars & (1U << bar))) {
            oserr_t oserr = AcquireDeviceIo(ioSpace);
            if (oserr != OS_EOK) {
                return oserr;
            }
            transport->AcquiredBars |= (uint8_t)(1U << bar);
        }
    }
    return OS_EOK;
}

static oserr_t
__ReadStatus(
    _In_  VirtioPciTransport_t* transport,
    _Out_ uint8_t*              statusOut)
{
    uint64_t value;
    oserr_t  oserr;

    oserr = VirtioPciRegionRead(
        &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
        offsetof(VirtioPciCommonConfiguration_t, DeviceStatus),
        sizeof(uint8_t),
        &value
    );
    if (oserr == OS_EOK) {
        *statusOut = (uint8_t)value;
    }
    return oserr;
}

static oserr_t
__WriteStatus(
    _In_ VirtioPciTransport_t* transport,
    _In_ uint8_t               status)
{
    return VirtioPciRegionWrite(
        &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
        offsetof(VirtioPciCommonConfiguration_t, DeviceStatus),
        status,
        sizeof(uint8_t)
    );
}

static oserr_t
__AddStatus(
    _In_ VirtioPciTransport_t* transport,
    _In_ uint8_t               statusBits)
{
    uint8_t status;
    oserr_t oserr = __ReadStatus(transport, &status);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return __WriteStatus(transport, status | statusBits);
}

static oserr_t
__ReadFeatures(
    _In_  VirtioPciTransport_t* transport,
    _Out_ uint64_t*             featuresOut)
{
    const VirtioPciRegion_t* common   = &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1];
    uint64_t                 features = 0;
    uint64_t                 value;
    oserr_t                  oserr;

    for (uint32_t selector = 0; selector < 2; selector++) {
        oserr = VirtioPciRegionWrite(
            common,
            offsetof(VirtioPciCommonConfiguration_t, DeviceFeatureSelect),
            selector,
            sizeof(uint32_t)
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        
        oserr = VirtioPciRegionRead(
            common,
            offsetof(VirtioPciCommonConfiguration_t, DeviceFeature),
            sizeof(uint32_t),
            &value
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        features |= ((uint64_t)(uint32_t)value) << (selector * 32);
    }

    *featuresOut = features;
    return OS_EOK;
}

static oserr_t
__WriteFeatures(
    _In_ VirtioPciTransport_t* transport,
    _In_ uint64_t              features)
{
    const VirtioPciRegion_t* common = &transport->Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1];
    oserr_t                  oserr;

    for (uint32_t selector = 0; selector < 2; selector++) {
        oserr = VirtioPciRegionWrite(
            common,
            offsetof(VirtioPciCommonConfiguration_t, DriverFeatureSelect),
            selector,
            sizeof(uint32_t)
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        
        oserr = VirtioPciRegionWrite(
            common,
            offsetof(VirtioPciCommonConfiguration_t, DriverFeature),
            (size_t)(uint32_t)(features >> (selector * 32)),
            sizeof(uint32_t)
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
    }
    return OS_EOK;
}

oserr_t
VirtioPciTransportInitialize(
    _In_  BusDevice_t*          device,
    _Out_ VirtioPciTransport_t* transport)
{
    unsigned int controlFlags = __DEVICEMANAGER_IOCTL_ENABLE |
            __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE;
    oserr_t oserr;
    TRACE("VirtioPciTransportInitialize()");

    if (device == NULL || transport == NULL) {
        return OS_EINVALPARAMS;
    }

    memset(transport, 0, sizeof(VirtioPciTransport_t));
    transport->Device = device;
    spinlock_init(&transport->ConfigurationLock);

    oserr = __ParseCapabilities(transport);
    if (oserr != OS_EOK) {
        ERROR("VirtioPciTransportInitialize: Failed to parse capabilities: %d", oserr);
        goto error;
    }

    oserr = __AcquireCapabilityBars(transport);
    if (oserr != OS_EOK) {
        ERROR("VirtioPciTransportInitialize: Failed to acquire capability bars: %d", oserr);
        goto error;
    }

    for (int bar = 0; bar < __DEVICEMANAGER_MAX_IOSPACES; bar++) {
        if (!(transport->AcquiredBars & (1U << bar))) {
            continue;
        }
        
        if (device->IoSpaces[bar].Type == DeviceIoMemoryBased) {
            controlFlags |= __DEVICEMANAGER_IOCTL_MMIO_ENABLE;
        } else if (device->IoSpaces[bar].Type == DeviceIoPortBased) {
            controlFlags |= __DEVICEMANAGER_IOCTL_IO_ENABLE;
        }
    }

    oserr = OSDeviceIOCtl(
        device->Base.Id,
        OSIOCTLREQUEST_BUS_CONTROL,
        &(struct OSIOCtlBusControl) {
            .Flags = controlFlags
        },
        sizeof(struct OSIOCtlBusControl)
    );
    if (oserr != OS_EOK) {
        ERROR("VirtioPciTransportInitialize: Failed to enable device: %d", oserr);
        goto error;
    }
    return OS_EOK;

error:
    VirtioPciTransportDestroy(transport);
    return oserr;
}

void
VirtioPciTransportDestroy(
    _In_ VirtioPciTransport_t* transport)
{
    if (transport == NULL || transport->Device == NULL) {
        return;
    }
    
    if (transport->ActiveQueues != 0) {
        WARNING("VirtioPciTransportDestroy called with %u active queues",
                transport->ActiveQueues);
        return;
    }

    for (int bar = 0; bar < __DEVICEMANAGER_MAX_IOSPACES; bar++) {
        if (transport->AcquiredBars & (1U << bar)) {
            ReleaseDeviceIo(&transport->Device->IoSpaces[bar]);
        }
    }
    memset(transport, 0, sizeof(VirtioPciTransport_t));
}

oserr_t
VirtioPciReset(
    _In_ VirtioPciTransport_t* transport)
{
    uint8_t status;
    oserr_t oserr;
    TRACE("VirtioPciReset: starting");

    if (transport == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __WriteStatus(transport, 0);
    if (oserr != OS_EOK) {
        ERROR("VirtioPciReset: failed to write status: %d", oserr);
        return oserr;
    }

    for (int retry = 0; retry < VIRTIO_RESET_RETRIES; retry++) {
        oserr = __ReadStatus(transport, &status);
        if (oserr != OS_EOK || status == 0) {
            if (oserr == OS_EOK) {
                transport->DeviceFeatures = 0;
                transport->DriverFeatures = 0;
                transport->ResetGeneration++;
            }
            TRACE("VirtioPciReset: completed with status=0x%x, oserr=%d", status, oserr);
            return oserr;
        }
        thrd_yield();
    }
    ERROR("VirtioPciReset: failed after %d retries", VIRTIO_RESET_RETRIES);
    return OS_EDEVFAULT;
}

oserr_t
VirtioPciNegotiateFeatures(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint64_t              supportedFeatures,
    _In_  uint64_t              requiredFeatures,
    _Out_ uint64_t*             negotiatedFeaturesOut)
{
    uint64_t negotiatedFeatures;
    uint8_t  status;
    oserr_t  oserr;
    TRACE("VirtioPciNegotiateFeatures: supportedFeatures=0x%llx, requiredFeatures=0x%llx",
          supportedFeatures, requiredFeatures);

    if (transport == NULL || negotiatedFeaturesOut == NULL ||
        (requiredFeatures & ~supportedFeatures) != 0) {
        return OS_EINVALPARAMS;
    }

    supportedFeatures |= VIRTIO_F_VERSION_1;
    requiredFeatures  |= VIRTIO_F_VERSION_1;

    oserr = VirtioPciReset(transport);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    oserr = __AddStatus(transport, VIRTIO_STATUS_ACKNOWLEDGE);
    if (oserr != OS_EOK) {
        goto error;
    }
    
    oserr = __AddStatus(transport, VIRTIO_STATUS_DRIVER);
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = __ReadFeatures(transport, &transport->DeviceFeatures);
    if (oserr != OS_EOK) {
        goto error;
    }
    if ((transport->DeviceFeatures & requiredFeatures) != requiredFeatures) {
        oserr = OS_ENOTSUPPORTED;
        goto error;
    }

    negotiatedFeatures = transport->DeviceFeatures & supportedFeatures;
    oserr = __WriteFeatures(transport, negotiatedFeatures);
    if (oserr != OS_EOK) {
        goto error;
    }
    
    oserr = __AddStatus(transport, VIRTIO_STATUS_FEATURES_OK);
    if (oserr != OS_EOK) {
        goto error;
    }
    
    oserr = __ReadStatus(transport, &status);
    if (oserr != OS_EOK) {
        goto error;
    }
    if (!(status & VIRTIO_STATUS_FEATURES_OK) ||
        (status & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED))) {
        oserr = OS_ENOTSUPPORTED;
        goto error;
    }

    transport->DriverFeatures = negotiatedFeatures;
    *negotiatedFeaturesOut = negotiatedFeatures;
    return OS_EOK;

error:
    ERROR("VirtioPciNegotiateFeatures: failed to negotiate features");
    VirtioPciSetFailed(transport);
    return oserr;
}

oserr_t
VirtioPciFinishInitialization(
    _In_ VirtioPciTransport_t* transport)
{
    uint8_t status;
    oserr_t oserr;
    TRACE("VirtioPciFinishInitialization: starting");

    if (transport == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __AddStatus(transport, VIRTIO_STATUS_DRIVER_OK);
    if (oserr != OS_EOK) {
        ERROR("VirtioPciFinishInitialization: failed to add DRIVER_OK status");
        return oserr;
    }
    
    oserr = __ReadStatus(transport, &status);
    if (oserr != OS_EOK) {
        ERROR("VirtioPciFinishInitialization: failed to read status");
        return oserr;
    }
    
    if (!(status & VIRTIO_STATUS_DRIVER_OK) ||
        (status & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED))) {
        ERROR("VirtioPciFinishInitialization: device needs reset or failed");
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

void
VirtioPciSetFailed(
    _In_ VirtioPciTransport_t* transport)
{
    if (transport == NULL) {
        return;
    }

    if (__AddStatus(transport, VIRTIO_STATUS_FAILED) != OS_EOK) {
        WARNING("Failed to set VIRTIO_STATUS_FAILED");
    }
}

oserr_t
VirtioPciReadIsrStatus(
    _In_  VirtioPciTransport_t* transport,
    _Out_ uint8_t*              statusOut)
{
    uint64_t value;
    oserr_t  oserr;

    if (transport == NULL || statusOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = VirtioPciRegionRead(
        &transport->Regions[VIRTIO_PCI_CAP_ISR_CFG - 1],
        0,
        sizeof(uint8_t),
        &value
    );
    if (oserr == OS_EOK) {
        *statusOut = (uint8_t)value;
    }
    return oserr;
}
