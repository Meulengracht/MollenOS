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
 */

#include <os/mollenos.h>
#include <os/osdefs.h>
#include <errno.h>
#include <assert.h>

const int g_errorCodeTable[__OS_ECOUNT] = {
    EOK,
    EINVAL,        // OS_EUNKNOWN
    EEXIST,        // OS_EEXISTS
    ENOENT,        // OS_ENOENT
    EINVAL,        // OS_EINVALPARAMS
    EACCES,        // OS_EPERMISSIONS
    ETIME,         // OS_ETIMEOUT
    EINTR,         // OS_EINTERRUPTED
    ENOTSUP,       // OS_ENOTSUPPORTED
    ENOMEM,        // OS_EOOM
    EBUSY,        // OS_EBUSY
    ECANCELED,    // OS_EINCOMPLETE
    ECANCELED,    // OS_ECANCELLED
    ECANCELED,    // OS_EBLOCKED
    EINPROGRESS,  // OS_EINPROGRESS,
    EINPROGRESS,  // OS_ESCSTARTED
    ENXIO,        // OS_EFORKED
    EOVERFLOW,    // OS_EOVERFLOW
    ENOBUFS,      // OS_EBUFFER

    ENOTDIR,      // OS_ENOTDIR
    EISDIR,       // OS_EISDIR
    ENOLINK,      // OS_ELINKINVAL
    EMLINK,       // OS_ELINKS
    ENOENT,       // OS_EDIRNOTEMPTY
    ENODEV,       // OS_EDEVFAULT
    
    EPROTOTYPE,   // OS_EPROTOCOL
    ECONNREFUSED, // OS_ECONNREFUSED
    ECONNABORTED, // OS_ECONNABORTED
    EHOSTUNREACH, // OS_EHOSTUNREACHABLE
    ENOTCONN,     // OS_ENOTCONNECTED
    EISCONN       // OS_EISCONNECTED
};

int
OsErrToErrNo(
        _In_ oserr_t code)
{
    int errnoCode;
    if (code >= __OS_ECOUNT) {
        _set_errno(EINVAL);
        return -1;
    }

    errnoCode = g_errorCodeTable[code];
    _set_errno(errnoCode);
    return code == OS_EOK ? 0 : -1;
}
