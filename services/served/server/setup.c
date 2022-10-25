/**
 * Copyright 2022, Philip Meulengracht
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

#define __TRACE

#include <ddk/utils.h>
#include <served/install.h>
#include <served/state.h>
#include <served/server.h>
#include <stdlib.h>

void served_server_setup_job(void* arguments, void* cancellationToken)
{
    _CRT_UNUSED(arguments);
    _CRT_UNUSED(cancellationToken);

    // Ensure server paths exists before doing anything. This doesn't
    // require any previous action, and does not hurt to ensure we do.
    oserr_t oserr = ServerEnsurePaths();
    if (oserr != OS_EOK) {
        // WHAT the hell, corrupt state.
        ERROR("served_server_setup_job failed to create necessary paths: %u", oserr);
        exit(-1);
    }

    // The first thing we want to determine is the system state. Check for the
    // presence of state.json, and determine the action from here. If
    // /data/served/state.json is not present, then we check for
    // /data/setup and launch the system install job.
    oserr = StateLoad();
    if (oserr != OS_EOK) {
        // WHAT the hell, corrupt state.
        ERROR("served_server_setup_job failed to load server state: %u", oserr);
        exit(-1);
    }

    StateLock();
    if (State()->FirstBoot) {
        InstallBundledApplications();
        State()->FirstBoot = false;
    }
    StateUnlock();

    // State was loaded, initialize served from state
    oserr = ServerLoad();
    if (oserr != OS_EOK) {
        ERROR("served_server_setup_job failed to initialize server: %u", oserr);
        exit(-1);
    }
}
