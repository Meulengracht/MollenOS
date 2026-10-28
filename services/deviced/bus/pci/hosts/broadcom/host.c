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

#include <ddk/utils.h>
#include <stdlib.h>

#include <bus/pci/host-private.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <core/io.h>

oserr_t
BcmPciHostCreate(
    _In_  const struct FdtPciHost* firmware,
    _Out_ PciHost_t**              hostOut)
{
    const struct BcmPciVariant* variant;
    struct BcmPciHost*          controller;
    PciHost_t*                  host;
    oserr_t                     status;
    oserr_t                     cleanup;

    *hostOut = NULL;
    
    variant = BcmPciGetVariant(firmware->Type);
    if (variant == NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (firmware->EcamLength < variant->RegisterLength) {
        return OS_EINVALPARAMS;
    }

    host = calloc(1, sizeof(PciHost_t) + sizeof(struct BcmPciHost));
    if (host == NULL) {
        return OS_EOOM;
    }

    status = BcmPciMapRegisters(
        &host->IoSpace,
        &host->IoAcquired,
        firmware->EcamBase,
        firmware->EcamLength
    );
    if (status != OS_EOK) {
        goto error;
    }

    controller = (struct BcmPciHost*)(host + 1);

    status = BcmPciInitialize(host, controller, firmware);
    if (status != OS_EOK) {
        goto error;
    }

    *hostOut = host;
    return OS_EOK;

error:
    // Initialization keeps its context installed if controller cleanup failed.
    // Return the host so the caller can retry destruction before releasing it.
    if (host->OpContext != NULL) {
        *hostOut = host;
        return status;
    }
    
    cleanup = DmIoRelease(&host->IoSpace, &host->IoAcquired);
    if (cleanup != OS_EOK) {
        // Keep the I/O state available for another destruction attempt.
        *hostOut = host;
    } else {
        free(host);
    }
    return status;
}

