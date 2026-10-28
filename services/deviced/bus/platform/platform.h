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

#ifndef __DEVICED_PLATFORM_H__
#define __DEVICED_PLATFORM_H__

#include <os/osdefs.h>
#include <startup.h>

struct PlatformBus;
struct FdtTree;

/**
 * @brief Validate and copy platform devices under the root and simple-bus nodes.
 *
 * @param tree Firmware tree, which must be non-NULL. The bus holds a reference
 *             until destruction succeeds.
 * @param busOut Required output for the unpublished inventory. Set to NULL on
 *               failure when both arguments are valid.
 * @return OS_EOK or an argument, firmware description, or allocation error.
 *         No hardware or registry changes occur.
 */
__EXTERN oserr_t
PlatformBusCreate(
    _In_  struct FdtTree*      tree,
    _Out_ struct PlatformBus** busOut);

/**
 * @brief Register the complete inventory before enabling compatible matching.
 *
 * @param bus Inventory to publish. Calls for one bus must be serialized.
 * @return OS_EOK or a publication error. Binding failures keep devices registered
 *         for retry. Failed registration starts removal before any binding
 *         starts. If removal fails, a later call retries it first.
 */
__EXTERN oserr_t
PlatformBusPublish(
    _InOut_ struct PlatformBus* bus);

/**
 * @brief Remove children before parents and release the bus's I/O resources.
 *
 * @param bus Inventory whose clients have released interrupts and stopped their
 *            devices. NULL is allowed.
 * @return OS_EOK when freed or bus is NULL. On failure the caller still owns bus
 *         and must retry after resolving the failure.
 */
__EXTERN oserr_t
PlatformBusDestroy(
    _In_ struct PlatformBus* bus);

/**
 * @brief Collect platform devices once and retry publication on later calls.
 *
 * Terminal startup failure is remembered for the service lifetime. Firmware
 * initialization must run first. If the tree is unavailable, return the firmware
 * startup outcome.
 * @return Complete on success or absence, retry for pending setup or rollback,
 *         or terminal failure for an unusable description.
 */
__EXTERN struct DmStartupResult
PlatformEnumerate(void);

#endif
