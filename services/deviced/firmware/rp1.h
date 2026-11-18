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
#include <dma-types.h>

#define FDT_RP1_MAX_REGISTERS   8
#define FDT_RP1_MAX_INTERRUPTS  8
#define FDT_RP1_INTERRUPT_COUNT 61

struct FdtRp1Range {
    uint64_t Base;
    uint64_t Length;
};

struct FdtRp1Interrupt {
    uint32_t Controller;
    uint32_t Number;
    // 1: signal changes from low to high; 4: signal stays high
    uint32_t Type;
};

struct FdtRp1Device {
    const char*            Name;
    const char*            Compatible;
    uint32_t               CompatibleLength;
    uint32_t               NodeOffset;
    uint32_t               Controller;
    struct FdtRp1Range     Registers[FDT_RP1_MAX_REGISTERS];
    uint32_t               RegisterCount;
    struct FdtRp1Interrupt Interrupts[FDT_RP1_MAX_INTERRUPTS];
    uint32_t               InterruptCount;
};

/**
 * @brief Process a temporary description of an enabled RP1 child.
 *
 * @param device Description valid for this call. Its strings remain valid while
 *               the original firmware tree stays mapped and unchanged.
 * @param context Caller data passed to FdtEnumerateRp1Children.
 */
typedef void (*FdtRp1DeviceFn)(
    _In_ const struct FdtRp1Device* device,
    _In_ void*                      context);

/**
 * @brief Find devices directly attached to the RP1 controller under this host.
 *
 * Checks all children and the controller's firmware ID before calling callback.
 * Disabled nodes are skipped. Malformed or unsupported resources cause an error
 * before any callbacks, so callers do not receive an incomplete device list.
 *
 * @param host PCI host whose RP1 controller should be examined.
 * @param ranges CPU physical memory ranges assigned to this RP1 endpoint by
 *               the bus owner. Firmware descriptions alone do not assign memory.
 *               Every child register range must fit inside one assigned range.
 * @param rangeCount Number of entries in ranges; zero permits NULL.
 * @param callback Called with each device's temporary description; copy it if
 *                 needed later. Keep host->Tree alive while using its strings.
 * @param context Caller data passed to callback.
 * @return OS_EOK on success, OS_ENOENT if no RP1 controller is described, or
 *         an error for invalid, ambiguous, or unsupported resource descriptions.
 */
__EXTERN oserr_t
FdtEnumerateRp1Children(
    _In_ const struct FdtPciHost*  host,
    _In_ const struct FdtRp1Range* ranges,
    _In_ uint32_t                  rangeCount,
    _In_ FdtRp1DeviceFn            callback,
    _In_ void*                     context);

/**
 * @brief Find the RAM ranges reachable by one enabled RP1 peripheral device.
 *
 * RP1 is the I/O hub connected over PCIe on Raspberry Pi 5. This reads the
 * PCI host's firmware DMA windows to RAM, then applies RP1's own dma-ranges
 * to produce the peripheral's DMA map. Does not inspect or change live
 * hardware, allocate buffers, or set cache policies.
 *
 * @param host      Decoded PCI host record, kept alive for the call. Only its
 *                  RAM DMA windows contribute to the result.
 * @param childNode Node offset of an enumerated RP1 peripheral.
 * @param map       Receives the sorted child-to-RAM map on success. Unchanged
 *                  on error. Gaps, peer PCI windows, and MSI write targets are
 *                  excluded.
 * @return OS_EOK on success, OS_ENOENT if the child is missing/disabled or no
 *         RAM is reachable, OS_EINVALPARAMS for malformed descriptions, or
 *         OS_ENOTSUPPORTED for missing dma-ranges, IOMMU paths, or full maps.
 */
__EXTERN oserr_t
FdtResolveRp1Dma(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 childNode,
    _Out_ FdtDmaMap_t*             map);

/**
 * @brief Combine an RP1 peripheral's bus windows with a supplied PCI-to-RAM map.
 *
 * Unlike FdtResolveRp1Dma, this uses the PCI-to-RAM ranges supplied in @p parent
 * instead of reading them from the PCI host's firmware description. Live bus
 * drivers pass the windows actually programmed into the PCI host controller.
 * Applies the same direct-path checks above the RP1 child.
 *
 * @param host      PCI host record used only to locate the RP1 node in the tree.
 * @param childNode Node offset of an enabled RP1 peripheral.
 * @param parent    PCI-to-RAM ranges active on the parent PCI host.
 * @param map       Receives the combined child-to-RAM map on success. May point
 *                  to the same struct as @p parent. Unchanged on error.
 * @return Same status codes as FdtResolveRp1Dma and FdtDmaCompose.
 */
__EXTERN oserr_t
FdtComposeRp1Dma(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 childNode,
    _In_  const FdtDmaMap_t*       parent,
    _Out_ FdtDmaMap_t*             map);

#endif //!__FIRMWARE_RP1_H__
