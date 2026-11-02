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

#include <ddk/utils.h>
#include <bus/pci/device.h>
#include <bus/pci/config.h>
#include <bus/pci/host-private.h>
#include <bus/pci/msix.h>
#include <os/memory.h>

static uint32_t
__MsixDeviceReadConfig(
    _In_ void*  context,
    _In_ size_t offset,
    _In_ size_t width)
{
    return PciDeviceRead(context, offset, width);
}

static void
__MsixDeviceWriteConfig(
    _In_ void*    context,
    _In_ size_t   offset,
    _In_ uint32_t value,
    _In_ size_t   width)
{
    PciDeviceWrite(context, offset, value, width);
}

// The MSI-X layout code must validate the BAR index before this check.
// Mapping also requires a published resource with the same CPU address.
static oserr_t
__ValidateMsixRegion(
    _In_ PciDevice_t*                device,
    _In_ const struct PciMsixRegion* region)
{
    DeviceIo_t* io = &device->PublishedIo[region->Bar];

    if (io->Type != DeviceIoMemoryBased || io->Id == UUID_INVALID) {
        WARNING("__ValidateMsixRegion: %u:%u:%u.%u BAR%u has no published memory resource",
            device->Host->Identification.Segment, device->Bus, device->Slot,
            device->Function, region->Bar);
        return OS_ENOTSUPPORTED;
    }
    if (io->Access.Memory.PhysicalBase != device->Resources.Bars[region->Bar].CpuAddress) {
        ERROR("__ValidateMsixRegion: %u:%u:%u.%u BAR%u no longer matches its published base",
            device->Host->Identification.Segment, device->Bus, device->Slot,
            device->Function, region->Bar);
        return OS_EINVALPARAMS;
    }

    if (region->Offset >= io->Access.Memory.Length ||
        region->Length > io->Access.Memory.Length - region->Offset) {
        ERROR("__ValidateMsixRegion: %u:%u:%u.%u BAR%u region at 0x%x of %llu bytes "
              "is outside the published resource",
            device->Host->Identification.Segment, device->Bus, device->Slot,
            device->Function, region->Bar, region->Offset,
            (unsigned long long)region->Length);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__MsixDeviceMap(
    _In_  void*                       context,
    _In_  const struct PciMsixRegion*  region,
    _Out_ void**                      mapping)
{
    PciDevice_t* device = context;
    oserr_t      status;

    TRACE("__MsixDeviceMap(bar=%u, offset=0x%llx, length=%llu)", region->Bar,
        (unsigned long long)region->Offset, (unsigned long long)region->Length);

    status = __ValidateMsixRegion(device, region);
    if (status != OS_EOK) {
        return status;
    }

    status = MapDeviceIo(
        device->PublishedIo[region->Bar].Id,
        region->Offset,
        region->Length,
        mapping
    );
    if (status != OS_EOK) {
        ERROR("__MsixDeviceMap: %u:%u:%u.%u MapDeviceIo failed for BAR%u "
              "at 0x%x of %llu bytes (%u)",
            device->Host->Identification.Segment, device->Bus, device->Slot,
            device->Function, region->Bar, region->Offset,
            (unsigned long long)region->Length, status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__MsixDeviceUnmap(
    _In_ void*  context,
    _In_ void*  mapping,
    _In_ size_t length)
{
    PciDevice_t* device = context;
    uintptr_t    address = (uintptr_t)mapping;
    size_t       offset = address % MemoryPageSize();
    oserr_t      status;

    TRACE("__MsixDeviceUnmap(mapping=%p, length=%llu)", mapping, (unsigned long long)length);

    // The region may start within a page. Free from the page boundary and
    // include the bytes before the region in the mapping length.
    status = MemoryFree((void*)(address - offset), length + offset);
    if (status != OS_EOK) {
        ERROR("__MsixDeviceUnmap: %u:%u:%u.%u MemoryFree failed for mapping %p "
              "of %llu bytes (%u)",
            device->Host->Identification.Segment, device->Bus, device->Slot,
            device->Function, mapping, (unsigned long long)length, status);
        return status;
    }
    return OS_EOK;
}

static uint32_t
__MsixDeviceReadTable(
    _In_ void*  mapping,
    _In_ size_t offset)
{
    uint32_t value;

    mb();
    value = *(volatile uint32_t*)((uintptr_t)mapping + offset);
    mb();
    return value;
}

static void
__MsixDeviceWriteTable(
    _In_ void*    mapping,
    _In_ size_t   offset,
    _In_ uint32_t value)
{
    mb();
    *(volatile uint32_t*)((uintptr_t)mapping + offset) = value;
    mb();
}

static struct MsixDeviceOperations
__MsixDeviceAccess(
    _In_ PciDevice_t* device)
{
    struct MsixDeviceOperations access = {
        .Config = {
            device,
            __MsixDeviceReadConfig,
            __MsixDeviceWriteConfig
        },
        .Bars = device->Resources.Bars,
        .Map = __MsixDeviceMap,
        .Unmap = __MsixDeviceUnmap,
        .Read = __MsixDeviceReadTable,
        .Write = __MsixDeviceWriteTable
    };
    return access;
}

oserr_t
PciMsixDeviceProbe(
    _In_ PciDevice_t* device)
{
    struct MsixDeviceOperations access = __MsixDeviceAccess(device);
    struct PciMsixLayout        layout;
    oserr_t                     status;

    TRACE("PciMsixDeviceProbe(%u:%u:%u.%u)", device->Host->Identification.Segment,
        device->Bus, device->Slot, device->Function);

    status = PciMsixProbe(&access, &layout);
    if (status != OS_EOK) {
        if (status == OS_ENOTSUPPORTED) {
            WARNING("PciMsixDeviceProbe: %u:%u:%u.%u MSI-X is unavailable (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot,
                device->Function, status);
        } else {
            ERROR("PciMsixDeviceProbe: %u:%u:%u.%u PciMsixProbe failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot,
                device->Function, status);
        }
        return status;
    }

    return __ValidateMsixRegion(device, &layout.Table);
}

oserr_t
PciMsixDeviceControl(
    _In_ PciDevice_t*                      device,
    _In_ const struct PciInterruptRequest* request)
{
    struct MsixDeviceOperations access = __MsixDeviceAccess(device);
    oserr_t                     status;

    status = PciMsixControl(&access, &device->Msix, request);
    if (status != OS_EOK) {
        if (status == OS_ENOTSUPPORTED || status == OS_EBUSY || status == OS_ENOENT) {
            WARNING("PciMsixDeviceControl: %u:%u:%u.%u MSI-X operation %u "
                    "allocation %u vector %u count %u rejected (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot,
                device->Function, request->Operation, request->Allocation,
                request->Index, request->Count, status);
        } else {
            ERROR("PciMsixDeviceControl: %u:%u:%u.%u PciMsixControl operation %u "
                  "allocation %u vector %u count %u failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot,
                device->Function, request->Operation, request->Allocation,
                request->Index, request->Count, status);
        }
        return status;
    }
    return OS_EOK;
}
