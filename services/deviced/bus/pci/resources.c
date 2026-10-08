/**
 * Copyright 2026, Philip Meulengracht
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

#include <bus/pci/bars.h>
#include <bus/pci/host-private.h>
#include <ddk/busdevice.h>
#include <ddk/utils.h>

static void
__CreateBarResource(
    _In_  PciHost_t*           bus,
    _Out_ DeviceIo_t*          resource,
    _In_  const struct PciBar* bar)
{
    uint64_t physical = bar->CpuAddress;
    uint64_t length = bar->Size;

    if (bar->State != PciBarAssigned) {
        return;
    }
    
    if (length > SIZE_MAX || physical > UINTPTR_MAX || length - 1 > UINTPTR_MAX - physical) {
        return;
    }
    
    if (bar->Space == 1) {
        if (bus->IoResourcePolicy == PciIoResourcePorts) {
            if (physical > UINT16_MAX || length - 1 > UINT16_MAX - physical) {
                return;
            }
            CreateDevicePortIo(resource, (uint16_t)physical, (size_t)length);
        } else {
            CreateDeviceMemoryIo(resource, (uintptr_t)physical, (size_t)length);
        }
    } else {
        CreateDeviceMemoryIo(resource, (uintptr_t)physical, (size_t)length);
    }
}

void
PciRegisterBars(
    _In_ PciHost_t*          host,
    _In_ BusDevice_t*        device,
    _In_ const struct PciBar bars[6])
{
    for (unsigned int i = 0; i < 6; i++) {
        __CreateBarResource(host, &device->IoSpaces[i], &bars[i]);
    }
}

void
PciDiagnoseBars(
    _In_ PciHost_t*          host,
    _In_ const BusDevice_t*  device,
    _In_ const struct PciBar bars[6])
{
    for (unsigned int i = 0; i < 6; i++) {
        if (bars[i].State == PciBarUnassigned) {
            WARNING("PCI %u:%u:%u.%u BAR%u is unassigned (size 0x%llx); firmware-assigned BAR required",
                host->Identification.Segment, device->Bus, device->Slot, device->Function,
                i, (unsigned long long)bars[i].Size);
        } else if (bars[i].State == PciBarOutsideWindow) {
            WARNING("PCI BAR is outside host windows (segment %u)", host->Identification.Segment);
        } else if (bars[i].State == PciBarInvalid) {
            WARNING("PCI %u:%u:%u.%u BAR%u has an invalid encoding or extent",
                host->Identification.Segment, device->Bus, device->Slot, device->Function, i);
        }
    }
}

void
PciReadBars(
    _In_ PciHost_t*   host,
    _In_ BusDevice_t* device,
    _In_ uint32_t     headerType)
{
    struct PciBar bars[6];

    PciProbeBars(host, device, headerType, bars);
    PciDiagnoseBars(host, device, bars);
    PciRegisterBars(host, device, bars);
}
