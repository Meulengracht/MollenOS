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

static struct PlatformBus* g_platformBus;
static oserr_t             g_platformStartupFailure;

struct DmStartupResult
PlatformEnumerate(
    void)
{
    struct FdtTree*        tree = FirmwareGetTree();
    struct DmStartupResult result;
    oserr_t                status;

    if (tree == NULL) {
        return FirmwareGetStartupResult();
    }
    if (g_platformStartupFailure != OS_EOK) {
        return __DmStartupFromStatus(g_platformStartupFailure);
    }

    if (g_platformBus == NULL) {
        status = PlatformBusCreate(tree, &g_platformBus);
        result = __DmStartupFromStatus(status);
    } else {
        result = __DmStartupFromStatus(OS_EOK);
    }

    if (result.Outcome == DmStartupComplete) {
        result = __DmStartupFromStatus(PlatformBusPublish(g_platformBus));
        if (g_platformBus->Publication.State == DmPublicationRemoving) {
            // Retry until rollback removes the partially published devices.
            result.Outcome = DmStartupRetry;
        }
    }

    if (result.Outcome == DmStartupFailed) {
        g_platformStartupFailure = result.Status;
    }
    return result;
}
