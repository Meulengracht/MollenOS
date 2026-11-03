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
 * A bus callback runs after the device handler and acknowledges upstream
 * hardware. Each group vector holds a reference while it uses the callback.
 * Create, bind and release callbacks outside interrupt handlers. Dispatch calls
 * InterruptBusHandlerInvoke after the device handler returns.
 */

#ifndef __INTERRUPTS_CHAIN_H__
#define __INTERRUPTS_CHAIN_H__

#include <ddk/interrupts/interrupt.h>

struct InterruptBusHandler;

/**
 * @brief Create an empty bus handler for the current process.
 *
 * @param handleOut Receives the bus handler handle.
 * @return OS_EOK on success, or an allocation error.
 */
oserr_t
InterruptBusHandlerCreate(
    _Out_ uuid_t* handleOut);

/**
 * @brief Map a bus callback and its resources into an empty bus handler.
 *
 * @param handle Bus handler created by the calling process.
 * @param resources Callback and resources in the bus process. The callback must
 *        follow the fast interrupt handler rules. It can only use mapped
 *        resources and the supplied function table.
 * @return OS_EOK on success. If binding fails, call InterruptBusHandlerRelease
 *         before discarding the handle. Keep the handle if cleanup needs a retry.
 */
oserr_t
InterruptBusHandlerBind(
    _In_ uuid_t                    handle,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Release a bus handler that no group is using.
 *
 * @param handle Bus handler created by the calling process.
 * @return OS_EBUSY while a group is using it, OS_ENOENT if it does not exist,
 *         or a cleanup status. Keep the handle and retry if cleanup fails.
 */
oserr_t
InterruptBusHandlerRelease(
    _In_ uuid_t handle);

/**
 * @brief Take a reference to a ready callback for the current bus process.
 *
 * @param handle Nonzero bus handler handle created by the calling process.
 * @param handlerOut Receives the callback pointer. Call InterruptBusHandlerPut
 *        after the group's vector has stopped using it.
 * @return OS_EOK, OS_EPERMISSIONS, OS_ENOENT or OS_EBUSY.
 */
oserr_t
InterruptBusHandlerAcquire(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptBusHandler** handlerOut);

/**
 * @brief Drop a group's reference after its handler and callback have finished.
 *
 * @param handler Referenced callback, or NULL if no callback is used.
 */
void
InterruptBusHandlerPut(
    _In_ struct InterruptBusHandler* handler);

/**
 * @brief Run the callback after the device handler and before dispatch returns.
 *
 * The group must hold a reference. Call between InterruptReadEnter and
 * InterruptReadExit so unbinding waits for the callback to finish.
 *
 * @param handler Callback referenced by the group, or NULL if no callback is used.
 */
void
InterruptBusHandlerInvoke(
    _In_ struct InterruptBusHandler* handler);

#endif //!__INTERRUPTS_CHAIN_H__
