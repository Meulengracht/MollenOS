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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Session Type Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_SESSION_H__
#define __TYPES_SESSION_H__

#include <os/osdefs.h>
#include <time.h>

typedef enum {
    WindowingService = 0x1,
} ServiceCapabilities_t;

PACKED_TYPESTRUCT(ServiceObject, {
    char                  Name[32];
    ServiceCapabilities_t Capabilities;
    UUId_t                ChannelHandle;
});

PACKED_TYPESTRUCT(SessionObject, {
    OsStatus_t    Status;

    char          SessionId[16];
    time_t        LastLogin;
 /* UserProfile_t Profile; */
});

#endif //!__SERVICES_SESSIONS_H__
