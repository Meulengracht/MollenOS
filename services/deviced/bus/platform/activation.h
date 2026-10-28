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

#ifndef __DEVICED_PLATFORM_ACTIVATION_H__
#define __DEVICED_PLATFORM_ACTIVATION_H__

#include <firmware/platform-runtime.h>

struct PlatformBus;

/**
 * @brief Hardware operations for a clock, reset or power dependency.
 * Acquire returns a token only on success and undoes all changes on failure.
 * Release consumes the token only on success. Errors leave it valid for retry.
 * The callbacks handle resources shared between tokens. For example, a clock
 * must stay enabled until its last user releases it.
 * Callbacks run under the platform bus lock: they may block, but must not call
 * platform APIs or acquire locks that call back into the bus. Context and the
 * operations table must remain valid until registration ends. Firmware code
 * interprets property bytes before passing validated selectors to callbacks.
 */
struct PlatformDependencyOperations {
    /**
     * @brief Enable a dependency and create a token for this consumer.
     *
     * @param context Hardware state owned by the registered driver.
     * @param dependency Validated hardware node and resource selector.
     * @param token Receives the token on success.
     * @return OS_EOK on success, or a driver error with all changes undone.
     */
    oserr_t (*Acquire)(
        _In_  void*                               context,
        _In_  const struct FdtPlatformDependency* dependency,
        _Out_ void**                              token);

    /**
     * @brief Release one consumer's dependency token.
     *
     * @param context Hardware state owned by the registered driver.
     * @param token Token from Acquire. Remains valid if release fails.
     * @return OS_EOK when released, or a driver error that allows retry.
     */
    oserr_t (*Release)(
        _In_ void* context,
        _In_ void* token);
};

/**
 * @brief Register the runtime owner of one hardware dependency node.
 * Registration does not acquire resources or start waiting consumers. A failed
 * DevicePrepare can be retried after the required driver has registered.
 *
 * @param bus Native bus whose firmware tree contains providerNode.
 * @param providerNode Exact firmware node offset, not a phandle or device ID.
 * @param operations Table with both callbacks populated. Must remain valid until unregister.
 * @param context Hardware state owned by the registered driver.
 * @return OS_EOK, OS_EEXISTS for a duplicate owner, or validation/removal error.
 */
oserr_t
PlatformDependencyRegister(
    _In_ struct PlatformBus*                        bus,
    _In_ uint32_t                                   providerNode,
    _In_ const struct PlatformDependencyOperations* operations,
    _In_ void*                                      context);

/**
 * @brief Remove a dependency owner only after all activation tokens are released.
 * Callers must serialize this operation with final bus destruction. Free the
 * hardware context only after successful removal.
 *
 * @param bus Native bus containing the registered node.
 * @param providerNode Registered node offset.
 * @return OS_EOK when removed, OS_EBUSY while consumers hold tokens, or OS_ENOENT
 *         if no owner is registered.
 */
oserr_t
PlatformDependencyUnregister(
    _In_ struct PlatformBus* bus,
    _In_ uint32_t            providerNode);

#endif //!__DEVICED_PLATFORM_ACTIVATION_H__
