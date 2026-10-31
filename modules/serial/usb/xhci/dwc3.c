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
 */

#include <ddk/devices/platform.h>
#include <string.h>

#include "dwc3.h"

#define DWC3_XHCI_LENGTH          0x8000U
#define DWC3_GLOBAL_BASE          0xc000U
#define DWC3_GLOBAL_LENGTH        0x1000U
#define DWC3_GSNPSID_OFFSET       0x120U
#define DWC3_GSNPSID_MASK         0xffff0000U
#define DWC3_GSNPSID_VALUE        0x55330000U
#define DWC3_GHWPARAMS0_OFFSET    0x140U
#define DWC3_GHWPARAMS0_MODE_MASK 0x3U
#define DWC3_GHWPARAMS0_MODE_HOST 1U

struct __Dwc3Setting {
    size_t   Offset;
    uint32_t Mask;
    uint32_t Value;
};

static const struct __Dwc3Setting g_rp1C0Settings[] = {
    // GCTL: host mode, clock gating and scale-down configuration.
    { 0x110, (3U << 12) | (3U << 4) | (1U << 3), 1U << 12 },
    // GUSB2PHYCFG and GUSB3PIPECTL: wake the integrated PHYs.
    { 0x200, 1U << 6, 0 },
    { 0x2c0, (1U << 17) | (1U << 28), 1U << 28 },
    // GSBUSCFG1, GUCTL and GTXTHRCFG: RP1 C0 AXI and scheduling settings.
    { 0x104, 15U << 8, 7U << 8 },
    { 0x11c, (1U << 19) | (7U << 15), (1U << 19) | (7U << 15) },
    { 0x108, (255U << 16) | (15U << 24) | (1U << 29),
             (8U << 16) | (2U << 24) | (1U << 29) }
};

_Static_assert(sizeof(g_rp1C0Settings) / sizeof(g_rp1C0Settings[0]) ==
    sizeof(((struct DWC3State*)0)->Original) / sizeof(uint32_t),
    "DWC3 rollback storage must cover every setting");

int
Dwc3IsDevice(
    _In_ const Device_t* device)
{
    const PlatformDevice_t* platform = (const PlatformDevice_t*)device;
    size_t                  offset = 0;

    if (device->Length != sizeof(*platform) || !PlatformDeviceValidate(platform)) {
        return 0;
    }

    while (offset < platform->CompatibleLength) {
        if (!strcmp(platform->Compatibles + offset, "snps,dwc3")) {
            return 1;
        }
        offset += strlen(platform->Compatibles + offset) + 1;
    }

    return 0;
}

static oserr_t
__WriteSetting(
    _In_ DeviceIo_t*                 registers,
    _In_ const struct __Dwc3Setting* setting,
    _In_ uint32_t                    bits)
{
    size_t   offset = DWC3_GLOBAL_BASE + setting->Offset;
    uint32_t value;
    oserr_t  status;

    value = (uint32_t)ReadDeviceIo(registers, offset, 4);
    value = (value & ~setting->Mask) | (bits & setting->Mask);
    status = WriteDeviceIo(registers, offset, value, 4);
    if (status != OS_EOK) {
        return status;
    }
    
    if ((ReadDeviceIo(registers, offset, 4) & setting->Mask) != (value & setting->Mask)) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
Dwc3Prepare(
    _InOut_ struct DWC3State*             integration,
    _In_    const struct DeviceResources* resources,
    _In_    DeviceIo_t*                   registers,
    _Out_   size_t*                       length)
{
    const struct DeviceParentPciIdentity* parent = &resources->ParentPci;
    uint32_t                              identity;
    uint32_t                              mode;
    oserr_t                               status;

    // snps,dwc3 is the IP, not the SoC integration. Until another
    // profile is implemented, error out.
    if (parent->VendorId != 0x1de4 || parent->ProductId != 0x0001 || parent->Revision != 2) {
        return OS_ENOTSUPPORTED;
    }

    if (resources->Count != 1 || registers->Type != DeviceIoMemoryBased ||
        registers->Access.Memory.Length < DWC3_GLOBAL_BASE + DWC3_GLOBAL_LENGTH) {
        return OS_ENOTSUPPORTED;
    }

    if (integration->Registers != NULL) {
        return OS_EBUSY;
    }

    // Accept only Synopsys DWC3 cores that report host mode in GHWPARAMS0.
    identity = (uint32_t)ReadDeviceIo(
        registers,
        DWC3_GLOBAL_BASE + DWC3_GSNPSID_OFFSET,
        4
    );
    mode = (uint32_t)ReadDeviceIo(
        registers,
        DWC3_GLOBAL_BASE + DWC3_GHWPARAMS0_OFFSET,
        4
    );
    if ((identity & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_VALUE ||
        (mode & DWC3_GHWPARAMS0_MODE_MASK) != DWC3_GHWPARAMS0_MODE_HOST) {
        return OS_ENOTSUPPORTED;
    }

    integration->Registers = registers;
    for (unsigned int i = 0; i < SIZEOF_ARRAY(g_rp1C0Settings); i++) {
        const struct __Dwc3Setting* setting = &g_rp1C0Settings[i];
        
        integration->Original[i] = (uint32_t)ReadDeviceIo(
            registers,
            DWC3_GLOBAL_BASE + setting->Offset,
            4
        );

        // Even a failed write may have changed hardware. Record ownership first.
        integration->Changed = i + 1;
        status = __WriteSetting(registers, setting, setting->Value);
        if (status != OS_EOK) {
            return status;
        }
    }

    *length = DWC3_XHCI_LENGTH;
    return OS_EOK;
}

oserr_t
Dwc3Release(
    _InOut_ struct DWC3State* integration)
{
    while (integration->Changed != 0) {
        unsigned int i = integration->Changed - 1;
        oserr_t      status;
        
        status = __WriteSetting(
            integration->Registers,
            &g_rp1C0Settings[i],
            integration->Original[i]
        );
        if (status != OS_EOK) {
            return status;
        }
        
        integration->Changed--;
    }
    integration->Registers = NULL;
    return OS_EOK;
}
