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

#include <io.h>
#include <os/mollenos.h>
#include <served/application.h>
#include <served/state.h>

/**
 * Server paths
 * /apps/<symlinks>
 * /data/setup
 * /data/served/state.json
 * /data/served/apps/<name>.pack
 * /data/served/mount/<name>
 */
static oserr_t __EnsurePaths(void)
{
    int mode = FILE_PERMISSION_READ | FILE_PERMISSION_EXECUTE | FILE_PERMISSION_OWNER_WRITE;
    if (mkdir("/apps", mode)) {
        return OsError;
    }

    if (mkdir("/data/served", mode)) {
        return OsError;
    }

    if (mkdir("/data/served/apps", mode)) {
        return OsError;
    }

    if (mkdir("/data/served/mount", mode)) {
        return OsError;
    }
    return OsOK;
}

static oserr_t __MountApplications(void)
{
    struct State* state = State();
    oserr_t       oserr = OsOK;

    StateLock();
    foreach(i, &state->Applications) {
        oserr = ApplicationMount((struct Application*)i);
        if (oserr != OsOK) {
            // TODO ERROR report
        }
    }
    StateUnlock();
    return oserr;
}

static oserr_t __StartServices(void)
{
    struct State* state = State();
    oserr_t       oserr = OsOK;

    StateLock();
    foreach(i, &state->Applications) {
        oserr = ApplicationStartServices((struct Application*)i);
        if (oserr != OsOK) {
            // TODO ERROR report
        }
    }
    StateUnlock();
    return oserr;
}

oserr_t ServerLoad(void)
{
    oserr_t oserr;

    oserr = __EnsurePaths();
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = __MountApplications();
    if (oserr != OsOK) {
        return oserr;
    }

    return __StartServices();
}
