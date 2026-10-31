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

// forward declarations
typedef struct DmDmaMap DmDmaMap_t;

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
 * @brief Describe the RAM ranges reachable by an enabled direct RP1 child.
 *
 * The result describes firmware DMA windows. The caller must check installed
 * RAM, cache policy and programmed hardware before using it for DMA. This
 * function does not allocate memory, publish devices or change readiness flags.
 *
 * @param host Decoded PCI host description, kept alive throughout the call.
 *             Its DmaWindows include PCI-to-CPU translations through parent buses.
 *             Only windows classified as RAM contribute to the result.
 * @param childNode Exact firmware node offset from an enumerated RP1 child.
 * @param map Receives the complete copied mapping only on success. Aliases are
 *            included; holes, peer resources and MSI doorbells are excluded.
 * @return OS_EOK, OS_ENOENT for an absent/disabled child or no reachable RAM,
 *         OS_EINVALPARAMS for malformed/ambiguous descriptions, or
 *         OS_ENOTSUPPORTED for absent DMA ranges, IOMMU paths or insufficient
 *         map capacity.
 */
__EXTERN oserr_t
FdtResolveRp1Dma(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 childNode,
    _Out_ DmDmaMap_t*              map);

/**
 * @brief Resolve the RP1 child path using the supplied PCI-to-RAM map.
 *
 * A running host must supply windows that match its programmed DMA settings.
 * The child path uses the same parent-bus and unsupported-path checks as
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
    _In_  const DmDmaMap_t*        parent,
    _Out_ DmDmaMap_t*              map);

#endif //!__FIRMWARE_RP1_H__
