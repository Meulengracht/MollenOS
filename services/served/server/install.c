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

#include <ds/mstring.h>
#include <io.h>
#include <stdlib.h>

oserr_t InstallApplication(mstring_t* path)
{

}

void InstallBundledApplications(void)
{
    struct DIR*   setupDir;
    struct DIRENT entry;

    if (opendir("/data/setup", 0, &setupDir)) {
        // directory did not exist, no bundled apps to install
        return;
    }

    while (readdir(setupDir, &entry) == 0) {
        mstring_t* path = mstr_fmt("/data/setup/%s", &entry.d_name[0]);
        oserr_t oserr = InstallApplication(path);
        if (oserr != OsOK) {
            // Not a compatable file, delete it
            char* pathu8 = mstr_u8(path);
            if (unlink(pathu8)) {

            }
            free(pathu8);
        }
        mstr_delete(path);
    }

    // finally remove the directory as installation has completed.
    (void)closedir(setupDir);
    if (unlink("/data/setup")) {

    }
}
