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

#include <firmware/bcm.h>
#include <firmware/resources.h>
#include <string.h>

oserr_t
FdtResolvePciMsi(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciMsi* msi)
{
    struct FdtResources provider;
    struct FdtResources parent;
    enum FdtPciHostType type;
    uint64_t doorbellBase = 0;
    uint64_t doorbellLength = 0;
    uint32_t msiOffset = 0;
    struct FdtPciMsi result = { 0 };
    oserr_t status;

    if (host == NULL || msi == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->MsiParent == NULL) {
        return OS_ENOENT;
    }
    if (host->MsiParentLength != 4 || !FdtReadBe32(host->MsiParent)) {
        return OS_EINVALPARAMS;
    }
    status = FdtFindResources(host->Blob, host->BlobLength, FdtReadBe32(host->MsiParent), &provider);
    if (status != OS_EOK) {
        return status;
    }
    if (!provider.IsMsiController || provider.MsiCells != 0 ||
        (!FdtCompatible(&provider.View, "brcm,bcm2712-mip") && !FdtPciHostType(&provider.View, &type))) {
        return OS_ENOTSUPPORTED;
    }
    if (provider.RegisterStatus != OS_EOK) {
        return provider.RegisterStatus;
    }
    result.Controller = provider.Phandle;
    result.IsMip = FdtCompatible(&provider.View, "brcm,bcm2712-mip");
    result.RegisterBase = provider.PhysicalBase;
    result.RegisterLength = provider.PhysicalLength;
    if (FdtCompatible(&provider.View, "brcm,bcm2712-mip")) {
        status = FdtScalar(&provider.View, "brcm,msi-offset", &msiOffset);
        if ((status != OS_EOK && status != OS_ENOENT) ||
            provider.RegLength != (provider.ParentAddressCells + provider.ParentSizeCells) * 8 ||
            FdtRawRegister(&provider, 1, &doorbellBase, &doorbellLength) != OS_EOK) {
            return OS_EINVALPARAMS;
        }
        if (provider.MsiRangesLength != 20 || provider.PhysicalLength < 0xc0 ||
            !doorbellLength || doorbellLength - 1 > UINT64_MAX - doorbellBase) {
            return OS_EINVALPARAMS;
        }
        status = FdtFindResources(host->Blob, host->BlobLength, FdtReadBe32(provider.MsiRanges), &parent);
        if (status != OS_EOK) {
            return status;
        }
        status = FdtGicInterrupt(&parent, provider.MsiRanges + 4, &result.Interrupt);
        if (status != OS_EOK) {
            return status;
        }
        result.InterruptCount = FdtReadBe32(provider.MsiRanges + 16);
        result.Offset = msiOffset;
        // MIP has 64 message bits. Keep the declared SPI base and message offset
        // separate: message n delivers base + offset + n.
        if (FdtReadBe32(provider.MsiRanges + 12) != 1 || !result.InterruptCount ||
            result.Offset >= 64 || result.InterruptCount > 64 - result.Offset ||
            result.InterruptCount - 1 + result.Offset > 1019U - (uint32_t)result.Interrupt.Line) {
            return OS_EINVALPARAMS;
        }
        result.DoorbellBase = doorbellBase;
        result.DoorbellLength = doorbellLength;
    } else {
        struct FdtPciHost source = *host;

        source.InterruptParent = provider.InterruptParent;
        source.Interrupts = provider.Interrupts;
        source.InterruptsLength = provider.InterruptsLength;
        source.InterruptsExtended = provider.InterruptsExtended;
        source.InterruptsExtendedLength = provider.InterruptsExtendedLength;
        source.InterruptNames = provider.InterruptNames;
        source.InterruptNamesLength = provider.InterruptNamesLength;
        status = FdtResolvePciNamedInterrupt(&source, "msi", &result.Interrupt);
        if (status != OS_EOK) {
            return status;
        }
    }
    *msi = result;
    return OS_EOK;
}
oserr_t
FdtResolvePciDependencies(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciDependencies* dependencies)
{
    struct FdtResources provider;
    uint32_t clockFrequency = 0;
    struct FdtPciDependencies result = { 0 };
    oserr_t status;

    if (host == NULL || dependencies == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->Resets != NULL) {
        uint32_t offset = 0;
        uint32_t index = 0;
        uint32_t names;
        uint32_t rescal;
        uint32_t bridge;
        const uint8_t* arguments;

        if (host->ResetsLength == 0 || (host->ResetsLength & 3)) {
            return OS_EINVALPARAMS;
        }
        status = FdtNameIndex(host->ResetNames, host->ResetNamesLength, "rescal", &rescal, &names);
        if (status != OS_EOK && status != OS_ENOENT) {
            return status;
        }
        status = FdtNameIndex(host->ResetNames, host->ResetNamesLength, "bridge", &bridge, &names);
        if (status != OS_EOK && status != OS_ENOENT) {
            return status;
        }
        while (offset < host->ResetsLength) {
            status = FdtNextReference(host->Blob, host->BlobLength,
                host->Resets, host->ResetsLength, "#reset-cells", 0,
                &offset, &provider, &arguments);
            if (status != OS_EOK) {
                return status;
            }
            if (index == rescal) {
                if (provider.RegisterStatus != OS_EOK || !FdtCompatible(&provider.View, "brcm,bcm7216-pcie-sata-rescal") || provider.ResetCells != 0 || provider.PhysicalLength < 12) {
                    return OS_ENOTSUPPORTED;
                }
                result.ResetBase = provider.PhysicalBase;
                result.ResetLength = provider.PhysicalLength;
            } else if (index == bridge) {
                if (provider.RegisterStatus != OS_EOK || !FdtCompatible(&provider.View, "brcm,brcmstb-reset") || provider.ResetCells != 1 ||
                    provider.PhysicalLength < 0x18 || provider.PhysicalLength % 0x18) {
                    return OS_ENOTSUPPORTED;
                }
                result.BridgeResetId = FdtReadBe32(arguments);
                // Each bank has SET, CLEAR, STATUS and three reserved words.
                // Counting only SET/CLEAR would accept selectors beyond reg.
                if (result.BridgeResetId / 32 >= provider.PhysicalLength / 0x18) {
                    return OS_EINVALPARAMS;
                }
                result.BridgeResetController = provider.Phandle;
                result.BridgeResetBase = provider.PhysicalBase;
                result.BridgeResetLength = provider.PhysicalLength;
            } else {
                return OS_ENOTSUPPORTED;
            }
            index++;
        }
        if (index != names) {
            return OS_EINVALPARAMS;
        }
    } else if (host->ResetNamesLength) {
        return OS_EINVALPARAMS;
    }
    if (host->Clocks != NULL) {
        if (host->ClocksLength != 4 || host->ClockNamesLength != 8 ||
            host->ClockNames == NULL || memcmp(host->ClockNames, "sw_pcie", 8) != 0) {
            return OS_ENOTSUPPORTED;
        }
        status = FdtFindResources(host->Blob, host->BlobLength,
                FdtReadBe32(host->Clocks), &provider);
        if (status != OS_EOK) {
            return status;
        }
        if (FdtScalar(&provider.View, "clock-frequency", &clockFrequency) != OS_EOK ||
            !FdtCompatible(&provider.View, "fixed-clock") || !provider.HasClockCells || provider.ClockCells != 0 ||
            clockFrequency == 0) {
            return OS_ENOTSUPPORTED;
        }
        result.ClockFrequency = clockFrequency;
    }
    *dependencies = result;
    return OS_EOK;
}

int
FdtBcmHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type)
{
    if (FdtCompatible(node, "brcm,bcm2712-pcie")) {
        *type = FdtPciHostBcm2712;
        return 1;
    }
    if (FdtCompatible(node, "brcm,bcm2711-pcie")) {
        *type = FdtPciHostBcm2711;
        return 1;
    }
    return 0;
}

oserr_t
FdtBcmHostPolicy(
    _In_ const struct FdtNode* node,
    _In_ struct FdtPciHost* host)
{
    const uint8_t* value;
    uint32_t length;
    oserr_t status;

    status = FdtScalar(node, "max-link-speed", &host->Link.MaxSpeed);
    if ((status != OS_EOK && status != OS_ENOENT) ||
        (status == OS_EOK && (!host->Link.MaxSpeed || host->Link.MaxSpeed > 6))) {
        return OS_EINVALPARAMS;
    }
    status = FdtScalar(node, "num-lanes", &host->Link.Lanes);
    if ((status != OS_EOK && status != OS_ENOENT) ||
        (status == OS_EOK && (!host->Link.Lanes || host->Link.Lanes > 16 ||
         (host->Link.Lanes & (host->Link.Lanes - 1))))) {
        return OS_EINVALPARAMS;
    }
    value = FdtProperty(node, "aspm-no-l0s", &length);
    if (value && length) {
        return OS_EINVALPARAMS;
    }
    host->Link.NoL0s = value != NULL;
    value = FdtProperty(node, "brcm,enable-ssc", &length);
    if (value && length) {
        return OS_EINVALPARAMS;
    }
    host->Link.EnableSsc = value != NULL;
    value = FdtProperty(node, "brcm,clkreq-mode", &length);
    if (value != NULL) {
        if (!((length == 5 && !memcmp(value, "safe", 5)) ||
              (length == 8 && !memcmp(value, "default", 8)) ||
              (length == 7 && !memcmp(value, "no-l1ss", 7)))) {
            return OS_EINVALPARAMS;
        }
        host->Link.ClkreqMode = (const char*)value;
    }
    value = FdtProperty(node, "brcm,scb-sizes", &length);
    host->ScbSize = value == NULL ? 0 : length == 8 ? FdtReadCells(value, 2) : UINT64_MAX;
    return OS_EOK;
}
