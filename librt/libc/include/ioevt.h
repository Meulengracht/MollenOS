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
 * Generic IO Events. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

#ifndef __IOEVT_H__
#define __IOEVT_H__

#include <os/osdefs.h>

enum ioevt_flags
{
    IOEVTIN  = 0x1,  // Receieved data
    IOEVTOUT = 0x2,  // Sent data
    IOEVTCTL = 0x4,  // Control event
    
    IOEVTLVT = 0x1000  // Level triggered
};

#define IOEVT_ADD 1
#define IOEVT_MOD 2
#define IOEVT_DEL 3

union ioevt_data {
    int      iod;
    UUId_t   handle;
    void*    context;
    uint32_t val32;
    uint64_t val64;
};

struct ioevt_event {
    int              events;
    union ioevt_data data;
};

_CODE_BEGIN

CRTDECL(int, ioevt(int flags));
CRTDECL(int, ioevt_ctrl(int evt_iod, int op, int iod, struct ioevt_event*));
CRTDECL(int, ioevt_wait(int evt_iod, struct ioevt_event*, int max_events, int timeout));

_CODE_END

#endif // !__IOEVT_H__
