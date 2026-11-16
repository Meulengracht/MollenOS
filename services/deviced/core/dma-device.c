/**
 * Copyright 2026, Philip Meulengracht
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
 * Device-level DMA description queries and DMA lease management.
 */

#include <core/dma.h>
#include <ddk/utils.h>
#include <limits.h>
#include <stdlib.h>

#include "device-private.h"

oserr_t
DmDeviceDescribeDma(
    _In_  uuid_t              deviceId,
    _Out_ DmDmaDescription_t* descriptionOut)
{
    DmDevice_t*                    device;
    const DmDeviceBusOperations_t* operations;
    DmDmaDescription_t             description = { 0 };
    oserr_t                        status;

    if (descriptionOut == NULL) {
        ERROR("DmDeviceDescribeDma: missing output for device %u", deviceId);
        return OS_EINVALPARAMS;
    }

    status = DmDeviceRequestAcquire(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmDeviceDescribeDma: request for device %u failed: %u", deviceId, status);
        return status;
    }

    operations = device->Bus.Operations;
    if (operations == NULL || operations->DescribeDma == NULL) {
        DmDeviceRequestRelease(device);
        return OS_ENOTSUPPORTED;
    }

    // The active request keeps device and its bus context alive while
    // DescribeDma runs, without preparing hardware or taking a DMA lease.
    // Only write to descriptionOut if the callback succeeds.
    status = operations->DescribeDma(device->Bus.Context, &description);
    if (status != OS_EOK) {
        WARNING("DmDeviceDescribeDma: DescribeDma device %u failed: %u", deviceId, status);
        DmDeviceRequestRelease(device);
        return status;
    }

    // Device removal may have started while DescribeDma ran without the lock.
    // Only copy the result out if the device is still active.
    usched_mtx_lock(&g_dmDevicesLock);
    if (device->Removing) {
        status = OS_EBUSY;
    } else {
        description.DeviceId = deviceId;
        *descriptionOut = description;
    }
    usched_mtx_unlock(&g_dmDevicesLock);
    DmDeviceRequestRelease(device);
    return status;
}

static oserr_t
__DevicePrepareDma(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut)
{
    DmDevice_t*                    device;
    const DmDeviceBusOperations_t* operations;
    struct DmDmaLease*             lease;
    oserr_t                        status;
    TRACE("__DevicePrepareDma(device=%u)", deviceId);

    // Require an empty (NULL) lease slot. Overwriting an existing pointer
    // would leak that lease and permanently block device removal.
    if (leaseOut == NULL) {
        WARNING("__DevicePrepareDma: missing lease output for device %u", deviceId);
        return OS_EINVALPARAMS;
    }
    if (*leaseOut != NULL) {
        WARNING("__DevicePrepareDma: lease output for device %u is occupied", deviceId);
        return OS_EBUSY;
    }

    status = DmDeviceRequestAcquire(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("__DevicePrepareDma: could not acquire device %u: %u", deviceId, status);
        return status;
    }

    operations = device->Bus.Operations;
    if (operations == NULL || operations->PrepareDma == NULL) {
        TRACE("__DevicePrepareDma: device %u has no DMA preparation callback", deviceId);
        DmDeviceRequestRelease(device);
        return OS_ENOTSUPPORTED;
    }

    lease = calloc(1, sizeof(struct DmDmaLease));
    if (lease == NULL) {
        ERROR("__DevicePrepareDma: failed to allocate lease for device %u", deviceId);
        DmDeviceRequestRelease(device);
        return OS_EOOM;
    }

    // DmDeviceRequestAcquire already dropped g_dmDevicesLock, so the bus
    // callback can take its own locks while ActiveRequests keeps the device
    // alive. Hand the lease back to the caller only after DmaLeases increases.
    status = operations->PrepareDma(
        device->Bus.Context,
        principal,
        &lease->Description
    );
    if (status != OS_EOK) {
        WARNING("__DevicePrepareDma: dma for device %u failed: %u", deviceId, status);
        DmDeviceRequestRelease(device);
        free(lease);
        return status;
    }

    // Swap our temporary request for a DMA lease under the lock. If removal
    // started during PrepareDma, undo the bus preparation and keep *leaseOut NULL.
    usched_mtx_lock(&g_dmDevicesLock);
    if (device->Removing) {
        WARNING("__DevicePrepareDma: device %u removal started during preparation", deviceId);
        status = OS_EBUSY;
    } else if (device->DmaLeases == UINT_MAX) {
        WARNING("__DevicePrepareDma: lease count overflow for device %u", deviceId);
        status = OS_EOVERFLOW;
    } else {
        device->DmaLeases++;
        lease->Device = device;
        lease->Description.DeviceId = deviceId;
        device->ActiveRequests--;
    }
    usched_mtx_unlock(&g_dmDevicesLock);

    if (status != OS_EOK) {
        // Keep ActiveRequests incremented until ReleaseDma finishes, so another
        // thread removing the device cannot free Bus.Context in between.
        if (operations->ReleaseDma != NULL) {
            operations->ReleaseDma(device->Bus.Context);
        }

        DmDeviceRequestRelease(device);
        free(lease);
        return status;
    }

    *leaseOut = lease;
    TRACE("__DevicePrepareDma: committed lease for device %u", deviceId);
    return OS_EOK;
}

oserr_t
DmDevicePrepareDmaForPrincipal(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut)
{
    if (principal == UUID_INVALID) {
        WARNING("DmDevicePrepareDmaForPrincipal: invalid principal %u for device %u",
                principal, deviceId);
        return OS_EPERMISSIONS;
    }
    return __DevicePrepareDma(deviceId, principal, leaseOut);
}

const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease)
{
    // The stored description stays valid until DmDmaLeaseRelease frees it.
    return lease == NULL ? NULL : &lease->Description;
}

void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease)
{
    struct DmDmaLease* current;

    // Clear the caller's pointer right away so calling release twice on the
    // same slot is safe. We skip looking up the device by ID because release
    // still has to work after removal has started.
    if (lease == NULL || *lease == NULL) {
        return;
    }

    current = *lease;
    *lease = NULL;

    // DmaLeases is still nonzero, so the device entry stays alive while
    // ReleaseDma runs.
    if (current->Device->Bus.Operations->ReleaseDma != NULL) {
        current->Device->Bus.Operations->ReleaseDma(current->Device->Bus.Context);
    }

    usched_mtx_lock(&g_dmDevicesLock);
    TRACE("DmDmaLeaseRelease: releasing lease for device %u", current->Device->Device->Id);
    current->Device->DmaLeases--;
    usched_mtx_unlock(&g_dmDevicesLock);

    // Once DmaLeases drops, another thread may destroy current->Device. Only
    // touch current from here on.
    free(current);
}

