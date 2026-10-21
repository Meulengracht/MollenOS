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

static oserr_t
__ValidateMsixRegion(
    _In_ PciDevice_t*                device,
    _In_ const struct PciMsixRegion* region)
{
    DeviceIo_t* io = &device->PublishedIo[region->Bar];

    if (io->Type != DeviceIoMemoryBased || io->Id == UUID_INVALID) {
        return OS_ENOTSUPPORTED;
    }
    if (io->Access.Memory.PhysicalBase != device->Resources.Bars[region->Bar].CpuAddress) {
        return OS_EINVALPARAMS;
    }

    if (region->Offset >= io->Access.Memory.Length ||
        region->Length > io->Access.Memory.Length - region->Offset) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__MsixDeviceMap(
    _In_  void*                       context,
    _In_  const struct PciMsixRegion* region,
    _Out_ void**                      mapping)
{
    PciDevice_t* device = context;
    oserr_t      status;

    status = __ValidateMsixRegion(device, region);
    if (status != OS_EOK) {
        return status;
    }

    return MapDeviceIo(
        device->PublishedIo[region->Bar].Id,
        region->Offset,
        region->Length,
        mapping
    );
}

static oserr_t
__MsixDeviceUnmap(
    _In_ void*  context,
    _In_ void*  mapping,
    _In_ size_t length)
{
    uintptr_t address = (uintptr_t)mapping;
    size_t    offset = address % MemoryPageSize();

    (void)context;
    return MemoryFree((void*)(address - offset), length + offset);
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

static struct PciMsixAccess
__MsixDeviceAccess(
    _In_ PciDevice_t* device)
{
    struct PciMsixAccess access = {
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
    struct PciMsixAccess access = __MsixDeviceAccess(device);
    struct PciMsixLayout layout;
    oserr_t              status;

    status = PciMsixProbe(&access, &layout);
    if (status != OS_EOK) {
        return status;
    }

    return __ValidateMsixRegion(device, &layout.Table);
}

oserr_t
PciMsixDeviceControl(
    _In_ PciDevice_t*                      device,
    _In_ const struct PciInterruptRequest* request)
{
    struct PciMsixAccess access = __MsixDeviceAccess(device);
    return PciMsixControl(&access, &device->Msix, request);
}
