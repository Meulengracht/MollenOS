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
 *
 * Device descriptions and the service code that handles their requests.
 */

#ifndef __DEVICED_DEVICE_PROVIDER_H__
#define __DEVICED_DEVICE_PROVIDER_H__

#include <ddk/devices/device.h>
#include <os/types/device.h>

// Forward declarations
struct DmDmaDescription;

enum DmDeviceDescriptionKind {
    DmDeviceDescriptionGeneric,
    DmDeviceDescriptionBus,
    DmDeviceDescriptionUsb,
    DmDeviceDescriptionPlatform
};

/**
 * @brief Handles requests for a device owned by a bus or another service module.
 * The operations must remain valid until Release returns. All callbacks run
 * without the device registry lock. Control, AccessRegister and PrepareDma are
 * optional. Both reference callbacks are required when Operations is supplied.
 */
struct DmDeviceProviderOperations {
    /**
     * @brief Adds one owner reference. Failure must leave ownership unchanged.
     *
     * @param context Owner state to keep alive for the registry.
     * @return OS_EOK on success, or an owner error without adding a reference.
     */
    oserr_t (*AddReference)(
        _In_ void* context);

    /**
     * @brief Drops the registry's reference after its last request has ended.
     *
     * @param context Owner state from the matching reference increment.
     */
    void (*Release)(
        _In_ void* context);

    /**
     * @brief Handles a control request, or returns OS_ENOTSUPPORTED.
     *
     * @param context Owner state for the target device.
     * @param principal Process identity verified by the transport, or UUID_INVALID.
     * @param request Control operation to perform.
     * @param buffer Request and result storage, interpreted according to request.
     * @param length Buffer size in bytes.
     * @return OS_EOK on success, OS_ENOTSUPPORTED for an unsupported operation,
     *         or an error from the device owner.
     */
    oserr_t (*Control)(
        _In_    void*               context,
        _In_    uuid_t              principal,
        _In_    enum OSIOCtlRequest request,
        _InOut_ void*               buffer,
        _In_    size_t              length);

    /**
     * @brief Reads or writes a register. The device owner validates the operation.
     *
     * @param context Owner state for the target device.
     * @param direction Read or write operation from the device request.
     * @param reg Register offset.
     * @param value Value to write, or storage for the value read.
     * @param width Access size in bytes.
     * @return OS_EOK on success, or a validation or register-access error.
     */
    oserr_t (*AccessRegister)(
        _In_    void*        context,
        _In_    int          direction,
        _In_    unsigned int reg,
        _InOut_ size_t*      value,
        _In_    size_t       width);

    /**
     * @brief Copies the configured DMA path for this device.
     *
     * Do not allocate hardware resources, enable hardware or change pending
     * flags. When ReleaseDma is supplied, this callback may hold an activation
     * guard on success. Failed preparation must leave no guard. Supply RAM ranges,
     * a registered host identity and an established cache policy. The core assigns DeviceId.
     * The owning bus must exclude reset while preparing and using this result.
     * If activation requires an owner, check a supplied principal before taking
     * the guard.
     *
     * @param context Owner state kept alive by the registry request.
     * @param principal Process identity verified by the transport, or UUID_INVALID
     *                  for a trusted internal query.
     * @param description Receives configured DMA ranges, host and cache policy.
     * @return OS_EOK on success, or an owner error with no activation guard held.
     */
    oserr_t (*PrepareDma)(
        _In_  void*                    context,
        _In_  uuid_t                   principal,
        _Out_ struct DmDmaDescription* description);

    /**
     * @brief Releases a guard acquired by a successful PrepareDma, if supplied.
     * Called exactly once after each successful PrepareDma, even when the registry
     * cannot record the lease. Runs outside the registry lock while a request or
     * DMA lease keeps the device alive. Must not fail or perform hardware teardown.
     * Consumers must stop DMA before releasing their lease. Leave this callback
     * NULL when PrepareDma only describes the DMA path and holds no guard.
     * The callback must not call back into device removal.
     *
     * @param context Owner state from the matching PrepareDma call.
     */
    void (*ReleaseDma)(
        _In_ void* context);
};

/**
 * @brief Connects a registered description to the object handling requests.
 */
struct DmDeviceProvider {
    const struct DmDeviceProviderOperations* Operations;
    void*                                    Context;
};

/**
 * @brief Supplies a description and optional request callbacks for registration.
 * The registry copies this structure. It owns Description only on success and
 * holds its own reference to the device owner. On failure, the caller keeps both.
 * Request callbacks are absent when Operations and Context are both NULL.
 */
struct DmDeviceRegistration {
    Device_t*                     Description;
    enum DmDeviceDescriptionKind  Kind;
    struct DmDeviceProvider       Provider;
};

#endif
