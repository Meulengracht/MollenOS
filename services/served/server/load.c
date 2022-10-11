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

#include <errno.h>
#include <io.h>
#include <os/services/file.h>
#include <served/application.h>
#include <served/state.h>

static int __CreateDirectoryIfNotExists(
        _In_ const char* path)
{
    int mode   = FILE_PERMISSION_READ | FILE_PERMISSION_EXECUTE | FILE_PERMISSION_OWNER_WRITE;
    int status = mkdir(path, mode);
    if (status && errno != EEXIST) {
        return status;
    }
    return 0;
}

/**
 * Server paths
 * /apps/<symlinks>
 * /data/setup
 * /data/served/state.json
 * /data/served/apps/<name>.pack
 * /data/served/mount/<name>
 * /data/served/cache/<name>
 */
oserr_t ServerEnsurePaths(void)
{
    if (__CreateDirectoryIfNotExists("/apps")) {
        return OsError;
    }

    if (__CreateDirectoryIfNotExists("/data/served")) {
        return OsError;
    }

    if (__CreateDirectoryIfNotExists("/data/served/apps")) {
        return OsError;
    }

    if (__CreateDirectoryIfNotExists("/data/served/mount")) {
        return OsError;
    }

    if (__CreateDirectoryIfNotExists("/data/served/cache")) {
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

    oserr = __MountApplications();
    if (oserr != OsOK) {
        return oserr;
    }

    return __StartServices();
}
