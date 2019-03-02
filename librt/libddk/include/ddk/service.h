/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * Service Definitions & Structures
 * - This header describes the base service-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SERVICE_H_
#define _SERVICE_H_

#include <ddk/ipc/ipc.h>
#include <os/osdefs.h>

#define __SERVICE_TARGET(Index)  ((UUId_t)0x8000 + Index)

#define __DEVICEMANAGER_TARGET   __SERVICE_TARGET(0)
#define __FILEMANAGER_TARGET     __SERVICE_TARGET(1)
#define __WINDOWMANAGER_TARGET   __SERVICE_TARGET(2)
#define __USBMANAGER_TARGET      __SERVICE_TARGET(3)
#define __SESSIONMANAGER_TARGET  __SERVICE_TARGET(4)
#define __PROCESSMANAGER_TARGET  __SERVICE_TARGET(5)

_CODE_BEGIN
CRTDECL(OsStatus_t, RegisterService(UUId_t Alias));
CRTDECL(OsStatus_t, IsServiceAvailable(UUId_t Alias));
CRTDECL(OsStatus_t, WaitForService(UUId_t Alias, size_t Timeout));
_CODE_END

#endif //!_SERVICE_H_
