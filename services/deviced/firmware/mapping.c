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
 */

#include <ddk/firmware.h>
#include <ddk/utils.h>

#include "tree-private.h"

static struct {
    const void*       Blob;
    size_t            Length;
    struct FdtTree*   Tree;
    DmStartupResult_t Startup;
} g_firmware = { 
    .Startup.Outcome = DmStartupRetry
};

oserr_t
FirmwareShutdown(void)
{
    oserr_t status;

    // The service must serialize initialization and shutdown. Before unmapping,
    // every reference must be released.
    if (g_firmware.Tree != NULL && atomic_load(&g_firmware.Tree->References) != 1) {
        return OS_EBUSY;
    }

    if (g_firmware.Blob != NULL) {
        status = FirmwareTableUnmap(g_firmware.Blob, g_firmware.Length);
        if (status != OS_EOK) {
            return status;
        }
    }

    FdtTreeRelease(g_firmware.Tree);

    g_firmware.Tree = NULL;
    g_firmware.Blob = NULL;
    g_firmware.Length = 0;

    g_firmware.Startup.Outcome = DmStartupRetry;
    g_firmware.Startup.Status  = OS_EOK;
    return OS_EOK;
}

static void
__FirmwareCleanupFailure(
    _Out_ DmStartupResult_t* result)
{
    DmStartupResult_t setup = g_firmware.Startup;
    oserr_t           cleanup;

    cleanup = FirmwareShutdown();
    // Keep the parse result after cleanup to decide whether startup can retry.
    // A direct FirmwareShutdown call resets this result.
    g_firmware.Startup = setup;
    if (cleanup != OS_EOK) {
        ERROR("FirmwareInitialize: mapping cleanup must be retried (%u)", cleanup);
        __DmStartupRetryResult(cleanup, result);
        return;
    }

    *result = setup;
}

void
FirmwareInitialize(
    _Out_ DmStartupResult_t* result)
{
    OSFirmwareTableKey_t   key = { .Source = OSFIRMWARE_DEVICETREE };
    oserr_t                status;

    // Finish unmapping a failed parse before trying to map the table again.
    if (g_firmware.Blob != NULL && g_firmware.Tree == NULL) {
        __FirmwareCleanupFailure(result);
        if (g_firmware.Blob != NULL || result->Outcome != DmStartupRetry) {
            return;
        }
    }
    if (g_firmware.Startup.Outcome != DmStartupRetry) {
        *result = g_firmware.Startup;
        return;
    }

    status = FirmwareTableMap(&key, &g_firmware.Blob, &g_firmware.Length);
    __DmStartupFromStatus(status, &g_firmware.Startup);
    if (status != OS_EOK) {
        if (status == OS_ENOENT) {
            // Firmware without a device tree needs no further initialization here.
            g_firmware.Startup.Outcome = DmStartupComplete;
        }
        *result = g_firmware.Startup;
        return;
    }

    status = FdtTreeCreate(g_firmware.Blob, g_firmware.Length, &g_firmware.Tree);
    __DmStartupFromStatus(status, &g_firmware.Startup);
    if (status != OS_EOK) {
        __FirmwareCleanupFailure(result);
        return;
    }
    
    *result = g_firmware.Startup;
}

void
FirmwareGetStartupResult(
    _Out_ DmStartupResult_t* result)
{
    if (g_firmware.Blob != NULL && g_firmware.Tree == NULL) {
        __DmStartupRetryResult(OS_EBUSY, result);
        return;
    }
    
    *result = g_firmware.Startup;
}

struct FdtTree*
FirmwareGetTree(void)
{
    return g_firmware.Tree;
}
