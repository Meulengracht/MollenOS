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

#ifndef __DEVICED_DEVICE_BUS_H__
#define __DEVICED_DEVICE_BUS_H__

#include <ddk/devices/device.h>
#include <os/types/device.h>
#include <dma-types.h>

typedef enum DmDeviceDescriptionKind {
    DmDeviceDescriptionGeneric,
    DmDeviceDescriptionBus,
    DmDeviceDescriptionUsb,
    DmDeviceDescriptionPlatform
} DmDeviceDescriptionKind_t;

typedef struct DmDeviceBusOperations {
    /**
     * @brief Increments the bus driver's reference count for this device so the
     * context pointer stays valid while the device is registered. If this fails,
     * the reference count must stay unchanged.
     *
     * @param context Bus-specific state passed when registering the device.
     * @return OS_EOK on success, or an error code from the bus code.
     */
    oserr_t (*AddReference)(
        _In_ void* context);

    /**
     * @brief Drops the device manager's reference once the device is removed and
     * all in-flight requests have finished.
     *
     * @param context Bus-specific state passed to AddReference.
     */
    void (*Release)(
        _In_ void* context);

    /**
     * @brief Handles a device control (ioctl) request from a driver or service.
     *
     * @param context Bus-specific state for the target device.
     * @param principal Calling process ID verified by the IPC server, or
     *                  UUID_INVALID when called internally by deviced.
     * @param request Control request code to run.
     * @param buffer Input and output buffer whose layout depends on request.
     * @param length Size of buffer in bytes.
     * @return OS_EOK on success, OS_ENOTSUPPORTED if the request is unknown,
     *         or a bus-specific error.
     */
    oserr_t (*Control)(
        _In_    void*               context,
        _In_    uuid_t              principal,
        _In_    enum OSIOCtlRequest request,
        _InOut_ void*               buffer,
        _In_    size_t              length);

    /**
     * @brief Stop all hardware under this host from reading or writing memory.
     * OS_EOK means no new memory access can start and none that already started
     * can finish. This includes devices without a driver and hardware shared
     * between devices. The stopped state is kept until the host is deliberately
     * set up again. After an error the stop may only be partly done.
     * This is separate from the normal DeviceStop. NULL means the bus cannot do it.
     * @param context Registered host state, kept alive by a request count.
     * @return OS_EOK only when everything has stopped, otherwise a stop error.
     */
    oserr_t (*StopDma)(
        _In_ void* context);

    /**
     * @brief Reads or writes a device configuration register.
     * The bus code is responsible for checking that the offset and access width
     * are valid for this device.
     *
     * @param context Bus-specific state for the target device.
     * @param direction Whether to read or write the register.
     * @param reg Register offset.
     * @param value Holds the value to write, or receives the value read.
     * @param width Access size in bytes.
     * @return OS_EOK on success, or a validation or hardware access error.
     */
    oserr_t (*AccessRegister)(
        _In_    void*        context,
        _In_    int          direction,
        _In_    unsigned int reg,
        _InOut_ size_t*      value,
        _In_    size_t       width);

    /**
     * @brief Reads a device's DMA settings without starting hardware or drivers.
     *
     * Used during device setup (such as publication) when deviced needs to know
     * which RAM ranges, parent host, address limit, and cache policy a device
     * uses before a driver loads. Do not power on hardware, prepare driver
     * resources, or bump any active DMA reference count here. The device
     * manager keeps context valid for the length of this call and fills in
     * description->DeviceId after it returns. Callers that need the host routing
     * to stay fixed across multiple steps must guard against host changes on
     * their own.
     *
     * @param context Bus-specific state kept alive by the in-flight request.
     * @param description Filled in with the DMA ranges, host ID, address limit,
     *                    and cache policy when returning OS_EOK. ReleaseDma is
     *                    never called after DescribeDma.
     * @return OS_EOK on success, OS_ENOTSUPPORTED if the device has no DMA route,
     *         or a bus error. On failure, hardware state and reference counts
     *         stay untouched.
     */
    oserr_t (*DescribeDma)(
        _In_  void*               context,
        _Out_ DmDmaDescription_t* description);

    /**
     * @brief Prepares a device for active DMA and returns its DMA settings.
     *
     * Drivers call into this path when they are ready to set up DMA buffers.
     * Unlike DescribeDma, this may take a usage reference (paired with
     * ReleaseDma) so the bus keeps clocks, power, or shared state active while
     * DMA is in use. Do not allocate hardware resources, enable the device, or
     * clear pending setup flags here. If ReleaseDma is set, you may take a busy
     * reference on success, and if you fail you must leave no reference behind.
     * Fill in the RAM ranges, a registered HostId, AddressLimit, and a known
     * CachePolicy (the device manager sets DeviceId). If the bus restricts DMA
     * setup to the driver that prepared the device, check principal before
     * taking any reference. The bus code must also prevent a device reset while
     * PrepareDma is running and until ReleaseDma drops the reference.
     *
     * @param context Bus-specific state kept alive by the in-flight request.
     * @param principal Calling process ID verified by the IPC server, or
     *                  UUID_INVALID for a trusted internal call.
     * @param description Filled in with the DMA ranges, host ID, address limit,
     *                    and cache policy on success.
     * @return OS_EOK on success, or an error with no DMA usage reference held.
     */
    oserr_t (*PrepareDma)(
        _In_  void*                    context,
        _In_  uuid_t                   principal,
        _Out_ struct DmDmaDescription* description);

    /**
     * @brief Drops the DMA usage reference taken by a successful PrepareDma.
     *
     * Called once for every PrepareDma that returned OS_EOK, even if the device
     * manager could not record the lease (for example, because removal started
     * mid-call). Runs outside the device registry lock while the active request
     * or lease still keeps context alive. Must never fail, power down hardware
     * directly, or call back into device removal. Callers are responsible for
     * stopping DMA transfers before releasing their lease. Leave this NULL if
     * PrepareDma only reads settings and takes no reference.
     *
     * @param context Bus-specific state from the matching PrepareDma call.
     */
    void (*ReleaseDma)(
        _In_ void* context);
} DmDeviceBusOperations_t;

typedef struct DmDeviceBus {
    const DmDeviceBusOperations_t* Operations;
    void*                          Context;
} DmDeviceBus_t;

typedef struct DmDeviceRegistration {
    Device_t*                  Description;
    DmDeviceDescriptionKind_t  Kind;
    DmDeviceBus_t              Bus;
} DmDeviceRegistration_t;

#endif //!__DEVICED_DEVICE_BUS_H__
