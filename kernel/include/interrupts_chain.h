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
 * A provider callback runs after the device handler and acknowledges upstream
 * hardware. Each group keeps the callback alive while one of its vectors uses it.
 * Create, Bind, and Release run outside interrupt handlers. Dispatch calls Invoke.
 */

#ifndef __INTERRUPTS_CHAIN_H__
#define __INTERRUPTS_CHAIN_H__

#include <ddk/interrupt.h>

struct InterruptCompletion;

/**
 * @brief Create an empty completion for the current process.
 *
 * @param handleOut Receives the completion handle.
 * @return OS_EOK on success, or an allocation error.
 */
oserr_t
InterruptCompletionCreate(
    _Out_ uuid_t* handleOut);

/**
 * @brief Map a provider callback into an empty completion.
 *
 * @param handle Completion owned by the caller.
 * @param resources Callback and resources in the provider's process. The callback
 *        follows the same rules as a fast interrupt handler: it can use its
 *        mapped resources and supplied function table, but not other process data
 *        or unmapped functions.
 * @return OS_EOK on success. If binding fails, call Release before discarding
 *         the handle because cleanup may need to be retried.
 */
oserr_t
InterruptCompletionBind(
    _In_ uuid_t                    handle,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Release a completion that no group is using.
 *
 * @param handle Completion owned by the caller.
 * @return OS_EBUSY while a group is using it, OS_ENOENT if it does not exist,
 *         or a cleanup status. Keep the handle and retry if cleanup fails.
 */
oserr_t
InterruptCompletionRelease(
    _In_ uuid_t handle);

/**
 * @brief Take a reference to a ready callback owned by the current provider.
 *
 * @param handle Nonzero completion handle.
 * @param completionOut Receives the callback pointer. The caller must call Put
 *        once the group's vector has stopped using it.
 * @return OS_EOK, OS_EPERMISSIONS, OS_ENOENT or OS_EBUSY.
 */
oserr_t
InterruptCompletionAcquire(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptCompletion** completionOut);

/**
 * @brief Drop a group's reference after its handler and callback have finished.
 *
 * @param completion Referenced callback, or NULL for no completion.
 */
void
InterruptCompletionPut(
    _In_ struct InterruptCompletion* completion);

/**
 * @brief Run the callback after the device handler and before dispatch returns.
 *
 * Call between InterruptReadEnter and InterruptReadExit. This makes unbinding
 * wait for the callback to finish. The group must still hold its reference.
 *
 * @param completion Callback retained by the group, or NULL for no completion.
 */
void
InterruptCompletionInvoke(
    _In_ struct InterruptCompletion* completion);
#endif
