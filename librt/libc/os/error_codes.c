/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * ErrorCode Definitions & Structures
 * - This header describes the base errors-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <os/osdefs.h>
#include <errno.h>
#include <assert.h>

const int g_errorCodeTable[OsErrorCodeCount] = {
    EOK,
    EINVAL,       // OsError                 Error - Generic
    EEXIST,       // OsExists                Error - Resource already exists
    ENOENT,       // OsNotExists             Error - Resource does not exist
    EINVAL,       // OsInvalidParameters     Error - Bad parameters given
    EACCES,       // OsInvalidPermissions    Error - Bad permissions
    ETIME,        // OsTimeout               Error - Operation timeout
    EINTR,        // OsInterrupted           Error - Operation was interrupted
    ENOTSUP,      // OsNotSupported          Error - Feature not supported
    ENOMEM,       // OsOutOfMemory           Error - Out of memory
    EBUSY,        // OsBusy                  Error - Resource is busy or contended
    ECANCELED,    // OsIncomplete            Error - Operation only completed partially
    ECANCELED,    // OsCancelled             Error - Operation was cancelled
    ECANCELED,    // OsBlocked               Error - Operating was blocked
    EINPROGRESS,  // OsInProgress,           Error - Operation was in progress
    
    ENOLINK,      // OsDeleted               Error - Resource was deleted
    ENOTDIR,      // OsPathIsNotDirectory    Error - Path is not a directory
    EISDIR,       // OsPathIsDirectory       Error - Path is a directory
    ENOLINK,      // OsLinkInvalid           Error - Bad link
    ENOENT,       // OsDirectoryNotEmpty     Error - Directory is not empty
    ENODEV,       // OsDeviceError           Error - Device error occurred during operation
    
    EPROTOTYPE,   // OsInvalidProtocol       Error - Protocol was invalid
    ECONNREFUSED, // OsConnectionRefused     Error - Connection was refused
    
    ECONNABORTED, // OsConnectionAborted     Error - Connection was aborted
    EHOSTUNREACH, // OsHostUnreachable       Error - Host could not be reached
    ENOTCONN,     // OsNotConnected          Error - Not connected
    EINPROGRESS,  // OsConnectionInProgress  Error - Connection already in progress
    EISCONN       // OsAlreadyConnected      Error - Already connected
};

int
OsErrToErrNo(
        _In_ oserr_t code)
{
    int errnoCode;
    if (code >= OsErrorCodeCount) {
        _set_errno(EINVAL);
        return -1;
    }

    errnoCode = g_errorCodeTable[code];
    _set_errno(errnoCode);
    return code == OsOK ? 0 : -1;
}
