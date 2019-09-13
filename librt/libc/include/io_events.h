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

#ifndef __IO_EVENTS_H__
#define __IO_EVENTS_H__

#include <os/osdefs.h>

enum io_events
{
    IOEVTIN  = 0x1,  // Receieved data
    IOEVTOUT = 0x2,  // Sent data
    IOEVTET  = 0x4,  // Edge triggered
    
    IOEVTFRT = 0x8   // Initial event 
};

#define IO_EVT_DESCRIPTOR_ADD 1
#define IO_EVT_DESCRIPTOR_MOD 2
#define IO_EVT_DESCRIPTOR_DEL 3

struct io_event {
    uint32_t events;
    int      iod;
};

_CODE_BEGIN

CRTDECL(int, io_set_create(int flags));
CRTDECL(int, io_set_ctrl(int evt_iod, int op, int iod, struct io_event*));
CRTDECL(int, io_set_wait(int evt_iod, struct io_event*, int max_events, int timeout));

_CODE_END

#endif // !__IO_H__
