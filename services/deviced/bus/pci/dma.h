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

#ifndef __DEVICED_PCI_DMA_H__
#define __DEVICED_PCI_DMA_H__

#include <bus/pci/host.h>
#include <dma-types.h>

struct PciDmaDescription;

/**
 * @brief Copied address ranges and cache policy for a host or its RP1 child.
 *
 * Host identifies the PCI controller. Each range's DeviceBase is a PCI address
 * for a host query and an RP1 address for a child query. PhysicalBase means a CPU
 * physical address. These are addressable regions, not allocated buffers or a
 * measurement of installed RAM. All values are stored directly in the structure.
 */
struct PciDmaDescription {
    struct PciHostIdentification Host;
    enum DmDmaCachePolicy        CachePolicy;
    DmDmaMap_t                   Map;
};

/**
 * @brief Describe the DMA address rules that apply to this host.
 *
 * Hosts with their own DMA controller report the address translation they have
 * set up. Hosts found through ACPI report the rules the kernel read from the
 * firmware and checked at boot. This query does not allow a device to start DMA
 * (bus mastering stays off) and does not clear the DMA pending state.
 *
 * @param host Live host. The owner must exclude setup, reset and destruction
 *             while querying and using the result, as for other host operations.
 * @param description Complete copy on success. Unchanged on failure. HostId is
 *                    UUID_INVALID until PciHostRegister succeeds. A saved copy
 *                    does not keep the host alive or remain valid after reset.
 * @return OS_EOK, OS_ENOTSUPPORTED if the backend supplies no DMA policy,
 *         OS_EBUSY while translation is unavailable, OS_ENOENT for no RAM,
 *         or an error describing invalid arguments or ranges.
 */
__EXTERN oserr_t
PciHostGetDmaDescription(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description);

/**
 * @brief Describe the coherent, untranslated DMA address range of x86 PCI hosts.
 *
 * @param host Host using this backend callback. Currently unused.
 * @param description Must be non-NULL. Receives coherent cache policy and one
 *                    range with equal device and CPU physical addresses.
 *                    Host identification is left zero for the common wrapper
 *                    to fill in. This range does not describe installed RAM.
 * @return OS_EOK on x86, or OS_ENOTSUPPORTED on other architectures with
 *         description unchanged.
 */
__EXTERN oserr_t
PciHostGetIdentityDma(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description);

/**
 * @brief Turn the DMA rules stored with an ACPI host into address ranges.
 *
 * The host must already have its root information loaded, which happens before
 * enumeration. The ranges describe memory that devices can reach directly, up
 * to the address limit the kernel reported. The firmware is not asked again, and
 * no bus-master register is changed. A host with no stored rules gets no DMA.
 * The host identification is filled in afterwards by PciHostGetDmaDescription,
 * once this function has succeeded.
 *
 * @return OS_EOK on success. OS_ENOTSUPPORTED if the stored rules are missing
 *         or do not allow direct addressing. OS_EINVALPARAMS if an argument,
 *         the status or the flags are invalid. The error the kernel stored
 *         during discovery is returned as is. On failure description is not
 *         changed.
 */
__EXTERN oserr_t
PciHostGetAcpiDma(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description);

#endif //!__DEVICED_PCI_DMA_H__
