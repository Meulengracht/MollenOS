/**
 * MollenOS
 *
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

#ifndef __RP1_ETHERNET_H__
#define __RP1_ETHERNET_H__

#include <os/types/device.h>

// Forward declarations
struct Rp1Child;
struct Rp1Ethernet;

/**
 * @brief Check whether the firmware compatible list identifies RP1's GEM MAC.
 *
 * @param child RP1 inventory entry with a validated compatible list.
 * @return Nonzero for GEM.
 */
int
Rp1EthernetIsDevice(
    _In_ const struct Rp1Child* child);

/**
 * @brief Check preparation before adding an Ethernet DMA or IRQ reference.
 *
 * @param child Inventory entry. The caller must hold the bus lock.
 * @param principal Verified process, or zero for a trusted internal DMA query.
 * @return OS_EOK for a prepared GEM or a non-Ethernet child. Otherwise, a state error.
 */
oserr_t
Rp1EthernetStatus(
    _In_ const struct Rp1Child* child,
    _In_ uuid_t                 principal);

/**
 * @brief Handle GEM preparation and PHY reset requests.
 * The caller must hold the RP1 bus lock.
 * Preparation maps the clock and optional GPIO blocks, then enables Ethernet
 * clocks. Before using MDIO, the driver must request PHY reset separately.
 * Before Stop, halt the MAC and release its DMA and interrupt references.
 * If cleanup fails, the child keeps the saved register values for retry.
 *
 * @param child Registered GEM child on a ready RP1 C0 bus.
 * @param principal Driver process verified by the transport.
 * @param request Preparation or PHY reset request.
 * @param buffer Exact request structure.
 * @param length Size of buffer.
 * @return OS_EOK or a validation or hardware error. Retry failed cleanup.
 */
oserr_t
Rp1EthernetControl(
    _InOut_ struct Rp1Child*    child,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _In_    const void*         buffer,
    _In_    size_t              length);

/**
 * @brief Restore registers and release mappings after the MAC has stopped.
 * The caller must stop the MAC engine before calling. DMA and interrupt
 * references must be released first, since their cleanup may still need the
 * Ethernet clocks.
 *
 * @param child GEM child, with the bus lock held and no active MAC accesses.
 * @return OS_EOK or a cleanup error. Keep child and retry on error.
 */
oserr_t
Rp1EthernetRelease(
    _InOut_ struct Rp1Child* child);

#endif //!__RP1_ETHERNET_H__
