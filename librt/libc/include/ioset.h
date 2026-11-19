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
#include <time.h>

enum ioset_flags
{
    IOSETIN  = 0x1,  // Receieved data
    IOSETOUT = 0x2,  // Sent data
    IOSETCTL = 0x4,  // Control event
    IOSETSYN = 0x8,  // Synchronization event
    IOSETTIM = 0x10, // Timeout event

    IOSETCREDIT = 0x20, // Socket receive capacity returned (netd only)

    IOSETLVT = 0x1000  // Level triggered
};

#define IOSET_ADD 1
#define IOSET_MOD 2
#define IOSET_DEL 3

union ioset_data {
    int      iod;
    uuid_t   handle;
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

/**
 * @brief Adds, changes or removes an I/O descriptor in an I/O set.
 * @param set_iod The I/O set descriptor, created by ioset().
 * @param op IOSET_ADD, IOSET_MOD or IOSET_DEL.
 * @param iod The descriptor to add, change or remove.
 * @param event The events to watch and the data to report with them. Required for
 *              IOSET_ADD and IOSET_MOD. May be NULL for IOSET_DEL, which ignores it.
 *              IOSET_MOD replaces both the events and the data of the entry. Events
 *              that are queued but not yet returned by ioset_wait also report the
 *              new data, because the data is read when ioset_wait returns them.
 * @return 0 on success.
 *         -1 on any errors, consult errno. EINVAL for an unknown operation or a NULL
 *         event with IOSET_ADD or IOSET_MOD, EBADF for an invalid descriptor and
 *         ENOENT when IOSET_MOD or IOSET_DEL cannot find the descriptor in the set.
 *         Other values come from the kernel's handle set call. On failure the set
 *         is unchanged, except when IOSET_DEL fails with ENOENT because the kernel
 *         already dropped the entry. The entry is then removed here as well.
 */
CRTDECL(int, ioset_ctrl(int set_iod, int op, int iod, struct ioset_event*));
CRTDECL(int, ioset_wait(int set_iod, struct ioset_event*, int max_events, const struct timespec* until));

_CODE_END

#endif // !__IOSET_H__
