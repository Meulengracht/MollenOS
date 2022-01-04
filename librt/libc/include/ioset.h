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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic IO Events. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

#ifndef __IOSET_H__
#define __IOSET_H__

#include <os/osdefs.h>

enum ioset_flags
{
    IOSETIN  = 0x1,  // Receieved data
    IOSETOUT = 0x2,  // Sent data
    IOSETCTL = 0x4,  // Control event
    IOSETSYN = 0x8,  // Synchronization event
    IOSETTIM = 0x10, // Timeout event

    IOSETLVT = 0x1000  // Level triggered
};

#define IOSET_ADD 1
#define IOSET_MOD 2
#define IOSET_DEL 3

union ioset_data {
    int      iod;
    UUId_t   handle;
    void*    context;
    uint32_t val32;
    uint64_t val64;
};

struct ioset_event {
    unsigned int     events;
    union ioset_data data;
};

_CODE_BEGIN

CRTDECL(int, ioset(int flags));
CRTDECL(int, ioset_ctrl(int set_iod, int op, int iod, struct ioset_event*));
CRTDECL(int, ioset_wait(int set_iod, struct ioset_event*, int max_events, int timeout));

_CODE_END

#endif // !__IOSET_H__
