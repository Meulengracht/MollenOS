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
 */

#include "private.h"
#include <ddk/utils.h>

static struct PlatformBus* g_platformBus;
static oserr_t             g_platformStartupFailure;

void
PlatformEnumerate(
    _Out_ struct DmStartupResult* result)
{
    struct FdtTree* tree = FirmwareGetTree();
    oserr_t         status;

    if (tree == NULL) {
        FirmwareGetStartupResult(result);
        TRACE("PlatformEnumerate: no firmware tree, startup outcome %u (%u)",
              result->Outcome, result->Status);
        return;
    }

    if (g_platformStartupFailure != OS_EOK) {
        __DmStartupFromStatus(g_platformStartupFailure, result);
        TRACE("PlatformEnumerate: skipping platform startup after an earlier failure (%u)",
              g_platformStartupFailure);
        return;
    }

    if (g_platformBus == NULL) {
        status = PlatformBusCreate(tree, &g_platformBus);
        __DmStartupFromStatus(status, result);
        
        if (status != OS_EOK) {
            ERROR("PlatformEnumerate: PlatformBusCreate failed (%u)", status);
            if (result->Outcome == DmStartupFailed) {
                g_platformStartupFailure = result->Status;
            }
            return;
        }
    } else {
        __DmStartupFromStatus(OS_EOK, result);
    }

    if (result->Outcome == DmStartupComplete) {
        status = PlatformBusPublish(g_platformBus);
        if (status != OS_EOK) {
            if (g_platformBus->Removing ||
                g_platformBus->Publication.State == DmPublicationRemoving) {
                WARNING("PlatformEnumerate: platform bus %u publication is waiting for removal (%u)",
                        g_platformBus->DeviceId, status);
            } else {
                ERROR("PlatformEnumerate: PlatformBusPublish failed for platform bus %u (%u)",
                      g_platformBus->DeviceId, status);
            }
        }

        __DmStartupFromStatus(status, result);
        if (g_platformBus->Publication.State == DmPublicationRemoving) {
            // Retry until rollback removes the partially published devices.
            result->Outcome = DmStartupRetry;
        }
    }

    if (result->Outcome == DmStartupFailed) {
        g_platformStartupFailure = result->Status;
    }
}
