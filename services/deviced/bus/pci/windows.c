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

#include <bus/pci/host-private.h>
#include <firmware/pci.h>
#include <ddk/pci/acpi.h>
#include <stdlib.h>

oserr_t
PciHostLoadAcpiWindows(
    _In_ PciHost_t*               host,
    _In_ const struct AcpiPciRoot* root)
{
    struct PciAddressWindow* windows = NULL;
    struct AcpiPciQuery      query = { .Type = AcpiPciQueryWindow, .RootId = root->Id };
    union AcpiPciResult      result;
    size_t                   count = root->WindowCount;
    oserr_t                  status;

    if (count > SIZE_MAX / sizeof(struct PciAddressWindow)) {
        return OS_EOVERFLOW;
    }

    if (root->WindowCount) {
        windows = calloc(root->WindowCount, sizeof(struct PciAddressWindow));
        if (!windows) {
            return OS_EOOM;
        }
    }

    for (uint32_t i = 0; i < root->WindowCount; i++) {
        query.Index = i;

        status = AcpiQueryPci(&query, &result);
        if (status != OS_EOK) {
            free(windows);
            return status;
        }
        
        if (!__PciWindowValid(&result.Window)) {
            free(windows);
            return OS_EINVALPARAMS;
        }
        
        windows[i] = result.Window;
    }

    // Only update the host once every query has succeeded. An empty _CRS window
    // list must still be respected, and means that no BARs can be mapped at all,
    // so we never fall back to identity mapping here.
    host->Resources.Windows = windows;
    host->Resources.Count = root->WindowCount;
    host->Resources.Present = 1;
    host->Resources.AssignBars = 0;
    host->AcpiRootId = root->Id;
    return OS_EOK;
}

oserr_t
PciHostLoadDtWindows(
    _In_ PciHost_t* host)
{
    struct PciAddressWindow* windows;
    const struct FdtPciHost* firmware = host->Firmware;

    if (!firmware || host->Resources.Present) {
        return OS_EOK;
    }
    
    if (firmware->WindowCount > sizeof(firmware->Windows) / sizeof(firmware->Windows[0])) {
        return OS_EINVALPARAMS;
    }
    
    windows = calloc(firmware->WindowCount ? firmware->WindowCount : 1, sizeof(struct PciAddressWindow));
    if (!windows) {
        return OS_EOOM;
    }

    for (unsigned int i = 0; i < firmware->WindowCount; i++) {
        windows[i].Space = firmware->Windows[i].Space;
        windows[i].Attributes = firmware->Windows[i].Attributes & PCI_WINDOW_PREFETCHABLE;
        windows[i].BusBase = firmware->Windows[i].BusBase;
        windows[i].PhysicalBase = firmware->Windows[i].PhysicalBase;
        windows[i].Length = firmware->Windows[i].Length;
        if (!__PciWindowValid(&windows[i])) {
            free(windows);
            return OS_EINVALPARAMS;
        }
    }

    // Device-tree hosts keep their existing policy of assigning BARs. We set
    // AssignBars explicitly, as the common resource code does not assume it is
    // allowed just because Windows is set.
    host->Resources.Windows = windows;
    host->Resources.Count = firmware->WindowCount;
    host->Resources.Present = 1;
    host->Resources.AssignBars = 1;
    return OS_EOK;
}
