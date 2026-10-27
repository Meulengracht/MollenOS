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

#ifndef __DEVICED_CORE_IO_H__
#define __DEVICED_CORE_IO_H__

#include <ddk/io.h>

/**
 * @brief Release a service mapping before destroying its registration.
 *
 * @param space Registration owned by the caller, or an empty slot.
 * @param acquired Nonzero if this process has an active mapping. Cleared after
 *                 the mapping is released, even if destruction then fails.
 * @return OS_EOK after clearing the slot, or the first cleanup error. Keep the
 *         slot and mapping flag until cleanup succeeds so the caller can retry.
 */
__EXTERN oserr_t
DmIoRelease(
    _InOut_ DeviceIo_t* space,
    _InOut_ int*        acquired);

/**
 * @brief Register a complete MMIO range without mapping or enabling it.
 *
 * @param io Receives the registration. The caller serializes access, must not
 *           overwrite a live registration and keeps the slot until destruction succeeds.
 * @param base Physical start of the validated firmware range.
 * @param length Nonzero range length in bytes.
 * @return OS_EOK when registered, OS_ENOTSUPPORTED when the range is empty or
 *         exceeds this process's address or size limits, or a registration
 *         error. Failure leaves io unchanged.
 */
__EXTERN oserr_t
DmIoCreateMemory(
    _Out_   DeviceIo_t* io,
    _In_    uint64_t    base,
    _In_    uint64_t    length);

/**
 * @brief Destroy registered I/O slots in order, clearing each successful slot.
 *
 * @param spaces Zeroed or registered slots whose clients have stopped. Release
 *               any mappings owned by this process before calling.
 * @param count Number of slots, including empty slots.
 * @return OS_EOK when empty, or the first destruction error. Failed and later
 *         slots stay unchanged so their owner can retry before freeing storage.
 */
__EXTERN oserr_t
DmIoDestroy(
    _InOut_ DeviceIo_t* spaces,
    _In_    size_t      count);

#endif
