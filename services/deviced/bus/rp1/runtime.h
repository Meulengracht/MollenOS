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
#include <ddk/types/pci-interrupt.h>

/**
 * @brief Prepare all messages after host publication has resolved its domain.
 * 
 * @param bus Attached bus, under the PCI lock. Partial state remains owned by it on failure.
 * @return OS_EOK when child interrupts and DMA descriptions are usable.
 */
__EXTERN oserr_t
Rp1RuntimeStart(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Stop and free runtime resources after children/providers are removed.
 * Called with the PCI lock held. Failure retains all still-needed resources.
 * 
 * @param bus Owning attachment.
 * @return OS_EOK, OS_EBUSY for subscriptions, or a retryable cleanup error.
 */
__EXTERN oserr_t
Rp1RuntimeStop(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Exclude new subscriptions before removing the publication group.
 * 
 * @param bus Owning attachment, under the PCI lock.
 * @return OS_EBUSY while a child still owns a subscription; otherwise OS_EOK.
 */
__EXTERN oserr_t
Rp1RuntimePrepareRemoval(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Execute a child's interrupt request under the PCI lock.
 * 
 * @param child Retained provider context.
 * @param principal Kernel-authenticated caller identity, never a reply endpoint.
 * @param request Validated fixed-size request buffer.
 * @return Operation status, retaining ownership on incomplete cleanup.
 */
__EXTERN oserr_t
Rp1RuntimeControl(
    _InOut_ struct Rp1Child*                 child,
    _In_    uuid_t                           principal,
    _InOut_ struct PciInterruptRequest*     request);

#endif
