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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

//#define __TRACE

#include <assert.h>
#include <bus/pci/discovery.h>
#include <bus/pci/host.h>
#include <bus/rp1/rp1.h>
#include <bus/platform/platform.h>
#include <firmware/tree.h>
#include "devices.h"
#include "discover.h"
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/job.h>
#include <time.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

static const struct PciFunctionHandler* const g_pciFunctionHandlers[] = {
    &g_rp1PciHandler
};

static void
__RecoverDevices(
    _Out_ struct DmStartupResult* result)
{
    struct DmStartupResult next;

    __DmStartupFromStatus(OS_EOK, result);

#ifdef __OSCONFIG_DEVICETREE_SUPPORT
    FirmwareInitialize(&next);
    if (next.Outcome == DmStartupRetry) {
        WARNING(
            "__RecoverDevices: firmware initialization needs another attempt (%u)",
            next.Status
        );
    } else if (next.Outcome == DmStartupFailed) {
        ERROR(
            "__RecoverDevices: firmware initialization failed (%u)",
            next.Status
        );
    }
    __DmStartupMerge(result, &next);
#endif

    BusRecoverAndRepublish(&next);
    if (next.Outcome == DmStartupRetry) {
        WARNING(
            "__RecoverDevices: bus recovery and republication needs another attempt (%u)",
            next.Status
        );
    } else if (next.Outcome == DmStartupFailed) {
        ERROR(
            "__RecoverDevices: bus recovery and republication failed (%u)",
            next.Status
        );
    }
    __DmStartupMerge(result, &next);
    
#ifdef __OSCONFIG_DEVICETREE_SUPPORT
    // PCI failure must not prevent independent platform devices from publishing.
    PlatformEnumerate(&next);
    if (next.Outcome == DmStartupRetry) {
        WARNING(
            "__RecoverDevices: platform enumeration needs another attempt (%u)",
            next.Status
        );
    } else if (next.Outcome == DmStartupFailed) {
        ERROR(
            "__RecoverDevices: platform enumeration failed (%u)",
            next.Status
        );
    }
    __DmStartupMerge(result, &next);
#endif
}

static void
__RecoverDeviceStartup(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct timespec        deadline;
    int                    status;
    unsigned int           delay = 1;
    struct DmStartupResult result;

    _CRT_UNUSED(argument);

    // One service job retries startup. Sleeping lets device requests and driver
    // discovery run between attempts, even when startup keeps failing.
    while (!usched_is_cancelled(cancellationToken)) {
        status = timespec_get(&deadline, TIME_UTC);
        if (status != TIME_UTC) {
            ERROR(
                "__RecoverDeviceStartup: timespec_get could not read the UTC clock (%i)",
                status
            );
            return;
        }

        deadline.tv_sec += delay;
        status = usched_job_sleep(&deadline);
        if (status != 0) {
            ERROR(
                "__RecoverDeviceStartup: usched_job_sleep could not schedule a %u-second delay (%i)",
                delay,
                status
            );
            return;
        }

        if (usched_is_cancelled(cancellationToken)) {
            return;
        }

        __RecoverDevices(&result);
        if (result.Outcome != DmStartupRetry) {
            if (result.Outcome == DmStartupFailed) {
                ERROR(
                    "__RecoverDeviceStartup: device startup cannot complete with the current configuration (%u)",
                    result.Status
                );
            }
            return;
        }
        WARNING(
            "__RecoverDeviceStartup: device startup incomplete; retry delay was %u seconds (%u)",
            delay,
            result.Status
        );
        if (delay < 30) {
            delay *= 2;
            if (delay > 30) {
                delay = 30;
            }
        }
    }
}

void
ServiceInitialize(
    _In_ struct ServiceStartupOptions* startupOptions)
{
    int                    status;
    uuid_t                 jobId;
    struct DmStartupResult result;

    TRACE("ServiceInitialize()");

    // Register the device service interface.
    status = gracht_server_register_protocol(
        startupOptions->Server,
        &sys_device_server_protocol
    );
    if (status != 0) {
        ERROR(
            "ServiceInitialize: device service protocol registration failed (%i)",
            status
        );
    }

    // Register the protocol used to send requests to drivers.
    status = gracht_client_register_protocol(GetGrachtClient(), &ctt_driver_client_protocol);
    if (status != 0) {
        ERROR(
            "ServiceInitialize: driver client protocol registration failed (%i)",
            status
        );
    }

    // Initialize device tracking and driver discovery before enumerating buses.
    PciInitialize(g_pciFunctionHandlers, SIZEOF_ARRAY(g_pciFunctionHandlers));
    DmDevicesInitialize();
    DmDiscoverInitialize();

    TRACE("ServiceInitialize: enumerating buses");
    __RecoverDevices(&result);
    if (result.Outcome == DmStartupRetry) {
        WARNING(
            "ServiceInitialize: device startup incomplete; scheduling retry (%u)",
            result.Status
        );
        jobId = usched_job_queue(__RecoverDeviceStartup, NULL);
        if (jobId == UUID_INVALID) {
            ERROR("ServiceInitialize: failed to queue device startup retry");
        }
    } else if (result.Outcome == DmStartupFailed) {
        ERROR(
            "ServiceInitialize: device startup cannot complete with the current configuration (%u)",
            result.Status
        );
    }
}
