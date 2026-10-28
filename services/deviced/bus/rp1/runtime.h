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

#ifndef __DEVICED_RP1_RUNTIME_H__
#define __DEVICED_RP1_RUNTIME_H__

#include <bus/rp1/rp1.h>
#include <ddk/types/interrupt-source.h>

/**
 * @brief Prepare shared hardware after descriptions are registered and the host
 * message domain is resolved, before PCI enables driver binding. Repeated calls
 * reuse a ready runtime while it accepts requests from child devices.
 *
 * @param bus Attached bus, with the PCI lock and bus->Lock held in that order.
 *            The bus owns any resources left after failed setup or cleanup.
 * @return OS_EOK when child interrupts and DMA descriptions are usable.
 */
__EXTERN oserr_t
Rp1RuntimeStart(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Stop and free runtime resources after child devices and their interrupt
 * interfaces are removed.
 * Hold the PCI lock, then bus->Lock. Failure keeps all resources still in use.
 * A bus with no runtime needs only bus->Lock during creation rollback.
 *
 * @param bus Bus that owns the runtime resources.
 * @return OS_EOK, OS_EBUSY for subscriptions, or a retryable cleanup error.
 */
__EXTERN oserr_t
Rp1RuntimeStop(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Block new subscriptions before removing the publication group. Existing
 * subscriptions must be released before this call can block new requests.
 *
 * @param bus Attached bus, with the PCI lock and bus->Lock held in that order.
 * @return OS_EBUSY while a child still owns a subscription, otherwise OS_EOK.
 */
__EXTERN oserr_t
Rp1RuntimePrepareRemoval(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Execute a child's interrupt request with its bus lock held.
 *
 * @param child Child context that must stay valid for this request, with
 *              child->Bus->Lock held.
 * @param principal Kernel-authenticated caller identity, never a reply endpoint.
 * @param request Validated fixed-size request buffer.
 * @return Operation status. Incomplete cleanup leaves resource ownership unchanged.
 */
__EXTERN oserr_t
Rp1RuntimeControl(
    _InOut_ struct Rp1Child*                     child,
    _In_    uuid_t                               principal,
    _InOut_ struct DeviceInterruptSourceRequest* request);

#endif
