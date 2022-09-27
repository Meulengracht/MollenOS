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

#include <os/mollenos.h>

static oserr_t __EnsurePaths(void)
{


    return OsOK;
}

static oserr_t __MountApplications(void)
{

    return OsOK;
}

static oserr_t __StartServices(void)
{

    return OsOK;
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
