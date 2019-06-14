/* MollenOS
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
 * Service Target Definitions
 */

#ifndef __SERVICE_TARGETS_H__
#define __SERVICE_TARGETS_H__

#include <os/osdefs.h>

#define __SERVICE_TARGET(Index)  ((UUId_t)0x8000 + Index)

#define __DEVICEMANAGER_TARGET   __SERVICE_TARGET(0)
#define __FILEMANAGER_TARGET     __SERVICE_TARGET(1)
#define __USBMANAGER_TARGET      __SERVICE_TARGET(2)
#define __SESSIONMANAGER_TARGET  __SERVICE_TARGET(3)
#define __PROCESSMANAGER_TARGET  __SERVICE_TARGET(4)
#define __NETMANAGER_TARGET      __SERVICE_TARGET(5)

_CODE_BEGIN
CRTDECL(OsStatus_t, IsServiceAvailable(UUId_t Target));
_CODE_END

#endif //!__SERVICE_TARGETS_H__
