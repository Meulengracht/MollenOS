/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Support
 * - Standard Socket IO Implementation
 */

#include <ddk/services/net.h>
#include <errno.h>
#include <internal/_io.h>
#include <io.h>

int socketpair(int domain, int type, int protocol, int* iods)
{
    if (!iods) {
        _set_errno(EINVAL);
        return -1;
    }
    
    iods[0] = socket(domain, type, protocol);
    if (iods[0] == -1) {
        return -1;
    }
    
    iods[1] = socket(domain, type, protocol);
    if (iods[1] == -1) {
        close(iods[1]);
        return -1;
    }
    
    // PairSockets(iods[0], iods[1]);
    return 0;
}
