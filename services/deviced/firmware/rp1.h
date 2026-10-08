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

#ifndef __FIRMWARE_RP1_H__
#define __FIRMWARE_RP1_H__

#include <firmware/pci.h>
#include <bus/pci/bars.h>

struct FdtDmaMap;

#define FDT_RP1_MAX_REGISTERS 8
#define FDT_RP1_MAX_INTERRUPTS 8
#define FDT_RP1_INTERRUPT_COUNT 61

/** 
 * @brief A CPU physical address range for an RP1 device's registers.
 * The range has been checked to fit inside memory assigned to RP1 through a
 * PCI Base Address Register (BAR), which describes a device's address range.
 */
struct FdtRp1Range {
    uint64_t Base;
    uint64_t Length;
};

/** 
 * @brief An interrupt handled by the RP1 I/O controller.
 * Controller is its firmware ID (phandle). Number is an RP1 interrupt number,
 * not a line on the Arm interrupt controller or a PCI device's interrupt pin.
 */
struct FdtRp1Interrupt {
    uint32_t Controller;
    uint32_t Number;
    uint32_t Type; // 1: signal changes from low to high; 4: signal stays high
};

/** 
 * @brief Describes one enabled device attached to the RP1 I/O controller.
 * Strings point into the original firmware data, which must stay mapped and
 * unchanged while they are used. NodeOffset locates the original node in the
 * tree's structure block, where callers can also read this device's clock,
 * reset, direct memory access (DMA), and pin settings.
 */
struct FdtRp1Device {
    const char* Name;
    
    const char* Compatible;
    uint32_t    CompatibleLength;
    
    uint32_t NodeOffset;
    uint32_t Controller;
    
    struct FdtRp1Range Registers[FDT_RP1_MAX_REGISTERS];
    uint32_t           RegisterCount;

    struct FdtRp1Interrupt Interrupts[FDT_RP1_MAX_INTERRUPTS];
    uint32_t               InterruptCount;
};

typedef void (*FdtRp1DeviceFn)(const struct FdtRp1Device*, void*);

/**
 * @brief Find devices directly attached to the RP1 controller under this host.
 *
 * Checks all children and the controller's firmware ID before calling callback.
 * Disabled nodes are skipped. Malformed or unsupported resources cause an error
 * before any callbacks, so callers do not receive an incomplete device list.
 *
 * @param host PCI host whose RP1 controller should be examined.
 * @param bars Six PCI BAR descriptions. Only assigned memory ranges are used,
 *             and their addresses must be CPU physical addresses.
 * @param callback Called with each device's temporary description; copy it if
 *                 needed later. Its strings still point into host->Blob.
 * @param context Caller data passed to callback.
 * @return OS_EOK on success, OS_ENOENT if no RP1 controller is described, or
 *         an error for invalid, ambiguous, or unsupported resource descriptions.
 */
__EXTERN oserr_t
FdtEnumerateRp1Children(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _In_ FdtRp1DeviceFn callback,
    _In_ void* context);

/**
 * @brief Describe the RAM apertures reachable by an enabled direct RP1 child.
 *
 * @param host Retained, decoded PCI host description. Its DmaWindows already
 *             express PCI-to-CPU translations through the host's ancestors.
 *             Only windows classified as RAM contribute to the result.
 * @param childNode Exact firmware node offset from an enumerated RP1 child.
 * @param map Receives the complete copied mapping only on success. Aliases are
 *            retained; holes, peer resources and MSI doorbells are excluded.
 * @return OS_EOK, OS_ENOENT for an absent/disabled child or no reachable RAM,
 *         OS_EINVALPARAMS for malformed/ambiguous descriptions, or
 *         OS_ENOTSUPPORTED for absent DMA ranges, IOMMU paths or capacity.
 *         Does not validate installed RAM, cache policy or programmed hardware,
 *         allocate memory, publish devices or change readiness flags.
 */
__EXTERN oserr_t
FdtResolveRp1Dma(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 childNode,
    _Out_ struct FdtDmaMap*        map);

/**
 * @brief Resolve the RP1 child path using the supplied PCI-to-RAM map.
 *
 * A running host can supply its configured windows here, instead of treating
 * firmware's desired host settings as proof that those settings were applied.
 * The child path uses the same ancestry and unsupported-path checks as
 * FdtResolveRp1Dma. No host DMA windows are read from firmware by this function.
 *
 * @param host Firmware used to locate this host and its RP1 child bus.
 * @param childNode Exact node offset of an enabled direct RP1 child.
 * @param parent PCI addresses mapped to physical RAM by the selected host.
 * @param map Complete child-to-RAM map on success; unchanged on failure. May
 *            alias parent. Does not allocate memory or establish cache policy.
 * @return Same errors as FdtResolveRp1Dma and FdtComposeDmaRanges.
 */
__EXTERN oserr_t
FdtComposeRp1Dma(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 childNode,
    _In_  const struct FdtDmaMap*  parent,
    _Out_ struct FdtDmaMap*        map);


#endif //!__FIRMWARE_RP1_H__
