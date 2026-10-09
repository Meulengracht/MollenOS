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
    _In_  const struct FdtPciHost* host,
    _Out_ struct FdtPciMsi*        msi)
{
    struct FdtResources provider;
    struct FdtResources parent;
    enum FdtPciHostType type;
    uint64_t            doorbellBase = 0;
    uint64_t            doorbellLength = 0;
    uint32_t            providerHandle;
    uint32_t            parentHandle;
    uint32_t            rangeType;
    uint32_t            msiOffset = 0;
    struct FdtPciMsi    result = { 0 };
    oserr_t             status;

    // Read the route for a Message Signaled Interrupt (MSI), where a device
    // asks for attention by sending a message instead of using a separate wire.
    // Keep the output unchanged unless every part of that route is valid.
    if (host == NULL || msi == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->MsiParent == NULL) {
        return OS_ENOENT;
    }
    if (host->MsiParentLength != 4) {
        return OS_EINVALPARAMS;
    }

    // The four-byte value names the firmware entry that provides MSI routing.
    providerHandle = FdtReadBe32(host->MsiParent);
    if (providerHandle == 0) {
        return OS_EINVALPARAMS;
    }

    status = FdtFindResources(host->Blob, host->BlobLength, providerHandle, &provider);
    if (status != OS_EOK) {
        return status;
    }
    // The parent must be an MSI controller that does not need extra address cells.
    if (!provider.IsMsiController || provider.MsiCells != 0) {
        return OS_ENOTSUPPORTED;
    }

    result.IsMip = FdtCompatible(&provider.View, "brcm,bcm2712-mip");
    if (!result.IsMip && !FdtPciHostType(&provider.View, &type)) {
        return OS_ENOTSUPPORTED;
    }
    if (provider.RegisterStatus != OS_EOK) {
        return provider.RegisterStatus;
    }

    result.Controller = provider.Phandle;
    result.RegisterBase = provider.PhysicalBase;
    result.RegisterLength = provider.PhysicalLength;

    if (result.IsMip) {
        status = FdtScalar(&provider.View, "brcm,msi-offset", &msiOffset);
        if (status != OS_EOK && status != OS_ENOENT) {
            return OS_EINVALPARAMS;
        }
        
        if (provider.RegLength != (provider.ParentAddressCells + provider.ParentSizeCells) * 8) {
            return OS_EINVALPARAMS;
        }
        
        status = FdtRawRegister(&provider, 1, &doorbellBase, &doorbellLength);
        if (status != OS_EOK) {
            return OS_EINVALPARAMS;
        }
        if (provider.MsiRangesLength != 20 || provider.PhysicalLength < 0xc0) {
            return OS_EINVALPARAMS;
        }
        if (doorbellLength == 0 || doorbellLength - 1 > UINT64_MAX - doorbellBase) {
            return OS_EINVALPARAMS;
        }
        
        parentHandle = FdtReadBe32(provider.MsiRanges);
        status = FdtFindResources(host->Blob, host->BlobLength, parentHandle, &parent);
        if (status != OS_EOK) {
            return status;
        }
        
        status = FdtGicInterrupt(&parent, provider.MsiRanges + 4, &result.Interrupt);
        if (status != OS_EOK) {
            return status;
        }
        result.InterruptCount = FdtReadBe32(provider.MsiRanges + 16);
        result.Offset = msiOffset;
        
        // The Broadcom MIP block turns a device's MSI message value into an
        // interrupt for the Arm system. Firmware supplies the first interrupt
        // number and a message offset, so check that every possible message fits.
        rangeType = FdtReadBe32(provider.MsiRanges + 12);
        if (rangeType != 1) {
            return OS_EINVALPARAMS;
        }
        if (result.InterruptCount == 0 || result.Offset >= 64) {
            return OS_EINVALPARAMS;
        }
        if (result.InterruptCount > 64 - result.Offset) {
            return OS_EINVALPARAMS;
        }
        if (result.InterruptCount - 1 + result.Offset >
            1019U - (uint32_t)result.Interrupt.Line) {
            return OS_EINVALPARAMS;
        }
        
        result.DoorbellBase = doorbellBase;
        result.DoorbellLength = doorbellLength;
    } else {
        struct FdtPciHost source = *host;

        // This controller stores the shared "msi" interrupt route. Use a copy
        // of the PCI host description with the controller's interrupt fields.
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
    struct FdtResources       provider;
    uint32_t                  clockFrequency = 0;
    struct FdtPciDependencies result = { 0 };
    int                       clockNameMatches;
    oserr_t                   status;

    // Collect the reset controls and clock needed by this host from firmware.
    // Build a temporary result so the caller never receives a partial answer.
    if (host == NULL || dependencies == NULL) {
        return OS_EINVALPARAMS;
    }

    if (host->Resets != NULL) {
        uint32_t       offset = 0;
        uint32_t       index = 0;
        uint32_t       names;
        uint32_t       rescal;
        uint32_t       bridge;
        const uint8_t* arguments;

        // Reset references are stored as whole 32-bit values.
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
            // Follow each firmware reference to learn which registers and
            // reset number belong to that entry.
            status = FdtNextReference(host->Blob, host->BlobLength,
                host->Resets, host->ResetsLength, "#reset-cells", 0,
                &offset, &provider, &arguments);
            if (status != OS_EOK) {
                return status;
            }
            if (index == rescal) {
                // The calibration block has no reset number and needs at
                // least three 32-bit registers.
                if (provider.RegisterStatus != OS_EOK) {
                    return OS_ENOTSUPPORTED;
                }

                if (!FdtCompatible(&provider.View, "brcm,bcm7216-pcie-sata-rescal")) {
                    return OS_ENOTSUPPORTED;
                }

                if (provider.ResetCells != 0 || provider.PhysicalLength < 12) {
                    return OS_ENOTSUPPORTED;
                }
                result.ResetBase = provider.PhysicalBase;
                result.ResetLength = provider.PhysicalLength;
            } else if (index == bridge) {
                // Bridge reset entries carry one reset number. Its registers
                // are arranged in complete groups of 0x18 bytes.
                if (provider.RegisterStatus != OS_EOK) {
                    return OS_ENOTSUPPORTED;
                }

                if (!FdtCompatible(&provider.View, "brcm,brcmstb-reset")) {
                    return OS_ENOTSUPPORTED;
                }
                if (provider.ResetCells != 1 || provider.PhysicalLength < 0x18) {
                    return OS_ENOTSUPPORTED;
                }
                if (provider.PhysicalLength % 0x18) {
                    return OS_ENOTSUPPORTED;
                }

                result.BridgeResetId = FdtReadBe32(arguments);
                // Each group of 32 reset signals uses six 32-bit registers:
                // SET, CLEAR, STATUS, and three reserved locations. Counting
                // only SET/CLEAR would allow reset IDs beyond the mapped range.
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
        // Names without matching reset references cannot be applied safely.
        return OS_EINVALPARAMS;
    }

    if (host->Clocks != NULL) {
        // This driver supports one clock input, identified by the name sw_pcie.
        if (host->ClocksLength != 4 || host->ClockNamesLength != 8) {
            return OS_ENOTSUPPORTED;
        }
        if (host->ClockNames == NULL) {
            return OS_ENOTSUPPORTED;
        }

        clockNameMatches = memcmp(host->ClockNames, "sw_pcie", 8) == 0;
        if (!clockNameMatches) {
            return OS_ENOTSUPPORTED;
        }

        status = FdtFindResources(
            host->Blob,
            host->BlobLength,
            FdtReadBe32(host->Clocks),
            &provider
        );
        if (status != OS_EOK) {
            return status;
        }

        status = FdtScalar(&provider.View, "clock-frequency", &clockFrequency);
        if (status != OS_EOK) {
            return OS_ENOTSUPPORTED;
        }

        // The clock must be a fixed-rate source with no extra address values.
        if (!FdtCompatible(&provider.View, "fixed-clock") || !provider.HasClockCells) {
            return OS_ENOTSUPPORTED;
        }
        if (provider.ClockCells != 0 || clockFrequency == 0) {
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
    // Match the firmware name to the chip-specific register layout.
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
    _In_ struct FdtPciHost*    host)
{
    const uint8_t* value;
    uint32_t       length;
    int            modeIsValid;
    oserr_t        status;

    // Read optional link settings and reject values this driver cannot use.
    // Firmware may omit these settings, so absence is different from an error.
    status = FdtScalar(node, "max-link-speed", &host->Link.MaxSpeed);
    if (status != OS_EOK && status != OS_ENOENT) {
        return OS_EINVALPARAMS;
    }
    if (status == OS_EOK && (!host->Link.MaxSpeed || host->Link.MaxSpeed > 6)) {
        return OS_EINVALPARAMS;
    }
    
    status = FdtScalar(node, "num-lanes", &host->Link.Lanes);
    if (status != OS_EOK && status != OS_ENOENT) {
        return OS_EINVALPARAMS;
    }
    if (status == OS_EOK && (!host->Link.Lanes || host->Link.Lanes > 16)) {
        return OS_EINVALPARAMS;
    }
    
    // A power-of-two lane count is required by the PCIe link setup.
    if (status == OS_EOK && (host->Link.Lanes & (host->Link.Lanes - 1))) {
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
        // Accept only the three policy names understood by this driver.
        modeIsValid = 0;
        if (length == 5) {
            modeIsValid = memcmp(value, "safe", 5) == 0;
        } else if (length == 8) {
            modeIsValid = memcmp(value, "default", 8) == 0;
        } else if (length == 7) {
            modeIsValid = memcmp(value, "no-l1ss", 7) == 0;
        }
        if (!modeIsValid) {
            return OS_EINVALPARAMS;
        }
        host->Link.ClkreqMode = (const char*)value;
    }
    
    value = FdtProperty(node, "brcm,scb-sizes", &length);
    // This property is either absent or exactly two 32-bit size values.
    if (value == NULL) {
        host->ScbSize = 0;
    } else if (length == 8) {
        host->ScbSize = FdtReadCells(value, 2);
    } else {
        host->ScbSize = UINT64_MAX;
    }
    return OS_EOK;
}
