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
 * Virtio network device implementation
 * 
 * Handles device initialization, configuration reading, and link status management.
 */

#include <ddk/utils.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "virtio-net.h"

static void
__DescribeDevice(
    _InOut_ VirtioNetDevice_t* device)
{
    // initialize the device information structure
    device->Info = (struct ctt_netadapter_info){
        .port_count = 1,
        .min_version = 2,
        .max_version = 2,
        .framing = CTT_NETADAPTER_FRAMING_ETHERNET,
        .medium = CTT_NETADAPTER_MEDIUM_VIRTUAL,
        .min_mtu = VIRTIO_NET_MTU,
        .max_mtu = VIRTIO_NET_MTU,
        .current_mtu = VIRTIO_NET_MTU,
        .max_frame_size = VIRTIO_NET_FRAME_SIZE,
        .buffer_alignment = 1,
        .max_pools = VIRTIO_NET_POOLS,
        .max_pool_bytes = VIRTIO_NET_POOL_BYTES,
        .max_registered_bytes = 2 * VIRTIO_NET_POOL_BYTES,
        .max_slots_per_pool = VIRTIO_NET_SLOTS,
        .max_queue_pairs = 1,
        .max_segments = 1,
        .max_batch_size = VIRTIO_NET_BATCH,
        .max_pending_batches = VIRTIO_NET_WINDOW,
        .max_outstanding_tx = VIRTIO_NET_SLOTS,
        .max_outstanding_rx = VIRTIO_NET_SLOTS,
        .max_unacked_completions = VIRTIO_NET_JOURNAL,
        .min_rx_slots = 4
    };

    // initialize the link state
    device->Link.sequence = 1;
    device->Link.duplex = CTT_NETADAPTER_DUPLEX_UNKNOWN;
}

/**
 * @brief reads mac and link status as one coherent snapshot
 * 
 * @param device the virtio net device
 * @param mac output buffer for the MAC address, must be at least 6 bytes in size
 * @param linkUpOut output flag indicating if the link is up
 * @return OS_EOK on success, error code otherwise
 */
static oserr_t
__ReadMacAndLinkStatus(
    _InOut_ VirtioNetDevice_t* device,
    _Out_ uint8_t              mac[6],
    _Out_ int*                 linkUpOut)
{
    uint64_t before, after, value;
    int      linkUp;
    oserr_t  status;

    // Read the configuration generation before accessing the MAC and link status.
    status = VirtioPciRegionRead(
        common, 
        offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration), 
        sizeof(uint8_t), 
        &before
    );
    if (status != OS_EOK) {
        return status;
    }
    for (int i = 0; i < 6; ++i) {
        status = VirtioPciRegionRead(config, i, 1, &value);
        if (status != OS_EOK) {
            return status;
        }
        mac[i] = (uint8_t)value;
    }

    linkUp = 1;
    if (device->Features & VIRTIO_NET_F_STATUS) {
        status = VirtioPciRegionRead(config, 6, 2, &value);
        if (status != OS_EOK) {
            return status;
        }
        linkUp = (value & 1) != 0;
    }

    status = VirtioPciRegionRead(
        common,
        offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration), 
        sizeof(uint8_t), 
        &after
    );
    if (status != OS_EOK) {
        return status;
    }
    if (before != after) {
        return OS_EBUSY;
    }
    if ((mac[0] & 1) || !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) {
        return OS_EPROTOCOL;
    }
    *linkUpOut = linkUp;
    return OS_EOK;
}

oserr_t
VirtioNetReadConfiguration(
    _InOut_ VirtioNetDevice_t* device)
{
    VirtioPciRegion_t* common = &device->Transport.Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1];
    VirtioPciRegion_t* config = &device->Transport.Regions[VIRTIO_PCI_CAP_DEVICE_CFG - 1];
    
    for (int retry = 0; retry < 8; ++retry) {
        enum ctt_netadapter_link_status link;
        oserr_t                         status;
        uint8_t                         mac[6];
        int                             linkUp;
        
        status = __ReadMacAndLinkStatus(device, mac, &linkUp);
        if (status == OS_EBUSY) {
            // Try again, the device state changed
            continue;
        } else if (status != OS_EOK) {
            return status;
        }

        // Update the device's MAC and link status based on the values read
        device->Info.permanent_mac = (struct ctt_netadapter_mac){
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        };
        device->Info.current_mac = device->Info.permanent_mac;
        
        link = linkUp ? CTT_NETADAPTER_LINK_STATUS_UP : CTT_NETADAPTER_LINK_STATUS_DOWN;
        if (device->Link.status != link) {
            // Link status has changed, update the sequence and notify if active
            if (device->Link.sequence == UINT64_MAX) {
                // wtf??
                return OS_EOVERFLOW;
            }
            
            device->Link.sequence++;
            device->Link.status = link;
            if (device->Session.Active) {
                ctt_netadapter_event_link_changed_single(
                    VirtioNetServer(),
                    device->Session.Owner,
                    &device->Session.Identity,
                    &device->Link
                );
            }
        }
        return OS_EOK;
    }
    return OS_EBUSY;
}

void
VirtioNetBusDeviceDestroy(
        _In_ BusDevice_t* busDevice)
{
    // from_sys_device duplicates identification strings independently of the
    // bus descriptor. Release both levels on every attachment/teardown path.
    if (busDevice == NULL) {
        return;
    }
    
    free(busDevice->Base.Identification.Description);
    free(busDevice->Base.Identification.Manufacturer);
    free(busDevice->Base.Identification.Product);
    free(busDevice->Base.Identification.Revision);
    free(busDevice->Base.Identification.Serial);
    free(busDevice);
}

VirtioNetDevice_t*
VirtioNetDeviceCreate(
        _In_ BusDevice_t* busDevice)
{
    VirtioNetDevice_t* device = calloc(1, sizeof(*device));
    oserr_t            status;
    
    if (device == NULL) {
        VirtioNetBusDeviceDestroy(busDevice);
        return NULL;
    }
    
    device->BusDevice = busDevice;
    device->EventDescriptor = -1;
    device->InterruptId = UUID_INVALID;
    device->Metadata.ID = UUID_INVALID;
    ELEMENT_INIT(&device->Header, (void*)(uintptr_t)busDevice->Base.Id, device);
    
    __DescribeDevice(device);

    status = VirtioPciTransportInitialize(busDevice, &device->Transport);
    if (status != OS_EOK) {
        goto error;
    }
    status = VirtioPciNegotiateFeatures(
        &device->Transport,
        VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS,
        VIRTIO_NET_F_MAC,
        &device->Features
    );
    if (status != OS_EOK) {
        goto error;
    }
    
    status = VirtioNetReadConfiguration(device);
    if (status != OS_EOK) {
        goto error;
    }
    
    // One cache-line-sized metadata cell per lifetime pool slot contains the
    // Virtio header plus zero TX padding. It is never shared with netd.
    status = SHMCreate(
        &(SHM_t){
            .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            .Conformity = OSMEMORYCONFORMITY_BITS32,
            .Size = VIRTIO_NET_POOLS * VIRTIO_NET_SLOTS * VIRTIO_NET_METADATA_STRIDE
        },
        &device->Metadata
    );
    if (status != OS_EOK) {
        goto error;
    }
    
    status = SHMGetSGTable(&device->Metadata, &device->MetadataSg, -1);
    if (status != OS_EOK) {
        goto error;
    }
    
    if (device->MetadataSg.Count != 1 ||
        device->MetadataSg.Entries[0].Length < SHMBufferLength(&device->Metadata)) {
        status = OS_ENOTSUPPORTED;
        goto error;
    }
    
    status = VirtioNetRegisterInterrupt(device);
    if (status != OS_EOK) {
        goto error;
    }
    
    NOTICE("virtio-net ready device=%u", busDevice->Base.Id);
    return device;
error:
    ERROR("virtio-net initialization failed: %u", status);
    // A failure before queues exist cannot leave packet DMA behind. Once queue
    // allocation is introduced, Destroy retains memory if reset cannot be proven.
    (void)VirtioNetDeviceDestroy(device);
    return NULL;
}

oserr_t
VirtioNetDeviceDestroy(
        _In_ VirtioNetDevice_t* device)
{
    if (device == NULL) {
        return OS_EOK;
    }
    
    if (device->Transport.Device) {
        oserr_t status = VirtioNetQueuesReset(device);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    VirtioNetUnregisterInterrupt(device);
    VirtioNetPoolsDetach(device);
    
    if (device->Metadata.Payload) {
        OSHandleDestroy(&device->Metadata);
    }
    
    free(device->MetadataSg.Entries);
    VirtioPciTransportDestroy(&device->Transport);
    VirtioNetBusDeviceDestroy(device->BusDevice);
    free(device);
    return OS_EOK;
}
