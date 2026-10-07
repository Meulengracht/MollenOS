/**
 * Copyright 2017, Philip Meulengracht
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

#include <ddk/device.h>
#include <os/types/device.h>

enum DmDeviceDescriptionKind {
    DmDeviceDescriptionGeneric,
    DmDeviceDescriptionBus,
    DmDeviceDescriptionUsb,
    DmDeviceDescriptionPlatform
};

/**
 * @brief Handles requests for a device owned by a bus or another service module.
 * The operations must remain valid until Release returns. All callbacks run
 * without the device registry lock. Control and AccessRegister are optional.
 * Retain and Release are required when a provider is supplied.
 */
struct DmDeviceProviderOperations {
    /**
     * @brief Adds one owner reference. Failure must leave ownership unchanged.
     */
    oserr_t (*Retain)(
        _In_ void* context);

    /**
     * @brief Drops the registry's reference after its last request has ended.
     */
    void (*Release)(
        _In_ void* context);

    /**
     * @brief Handles a control request, or returns OS_ENOTSUPPORTED.
     */
    oserr_t (*Control)(
        _In_    void*               context,
        _In_    enum OSIOCtlRequest request,
        _InOut_ void*               buffer,
        _In_    size_t              length);

    /**
     * @brief Reads or writes a register. The provider validates the operation.
     */
    oserr_t (*AccessRegister)(
        _In_    void*        context,
        _In_    int          direction,
        _In_    unsigned int reg,
        _InOut_ size_t*      value,
        _In_    size_t       width);
};

/**
 * @brief Connects a registered description to the object handling requests.
 */
struct DmDeviceProvider {
    const struct DmDeviceProviderOperations* Operations;
    void*                                    Context;
};

/**
 * @brief Supplies a description and an optional provider for registration.
 * The registry copies this structure. It owns Description only on success and
 * retains its own provider reference. On failure, the caller keeps both.
 * A provider is absent when Operations and Context are both NULL.
 */
struct DmDeviceRegistration {
    Device_t*                     Description;
    enum DmDeviceDescriptionKind  Kind;
    struct DmDeviceProvider       Provider;
};

#endif
