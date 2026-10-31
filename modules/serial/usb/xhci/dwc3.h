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

#ifndef __USB_XHCI_DWC3_H__
#define __USB_XHCI_DWC3_H__

#include <ddk/resources.h>

/**
 * Keep the register mapping valid until all changed settings are restored.
 * Changed includes a write that failed verification, so partial preparation and
 * failed restoration leave the state needed for another cleanup attempt.
 */
struct DWC3State {
    DeviceIo_t*  Registers;
    uint32_t     Original[6];
    unsigned int Changed;
};

/**
 * @brief Select DWC3 from the original, validated firmware compatible list.
 * @param device Driver's device description.
 * @return Nonzero when the device requires DWC3 integration.
 */
extern int
Dwc3IsDevice(
    _In_ const Device_t* device);

/**
 * @brief Prepare the supported DWC3 integration and expose its xHCI window.
 * @param integration Zero-initialized state that remains valid if preparation fails.
 * @param resources Provider resources and verified parent identity.
 * @param registers Mapped register window; keep it valid until release succeeds.
 * @param length Receives the bounded xHCI extent on success.
 * @return OS_ENOTSUPPORTED for unsupported integrations, or IO status.
 */
extern oserr_t
Dwc3Prepare(
    _InOut_ struct DWC3State*             integration,
    _In_    const struct DeviceResources* resources,
    _In_    DeviceIo_t*                   registers,
    _Out_   size_t*                       length);

/**
 * @brief Restore changed wrapper settings after xHCI and its interrupts stop.
 * @param integration State whose mapping and provider activation remain live.
 * @return IO status; failure leaves state intact and requires retry before unmapping.
 */
extern oserr_t
Dwc3Release(
    _InOut_ struct DWC3State* integration);

#endif
