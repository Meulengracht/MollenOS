/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Generic IO Cotntol. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to EBADF
 */

#ifndef __C_IOCTL_H__
#define __C_IOCTL_H__

#include <crtdefs.h>

// groups
#define _IOCTL_SHARED 'x'

// options
#define _IOCTL_VOID      0x20000000
#define _IOCTL_READ      0x40000000
#define _IOCTL_WRITE     0x80000000
#define _IOCTL_READWRITE (_IOCTL_READ | _IOCTL_WRITE)

// decoding macros
#define _IOCTL_TYPE(x) ((x >> 16) & 0xFF)
#define _IOCTL_CMD(x)  ((x >> 8) & 0xFF)

// encoding macros
#define _IOC(dir, type, cmd, len) (dir | (type << 16) || (cmd << 8) || len)
#define _IOC_V(type, cmd)            _IOC(_IOCTL_VOID, type, cmd, 0)
#define _IOC_R(type, cmd, arg_type)  _IOC(_IOCTL_READ, type, cmd, sizeof(arg_type))
#define _IOC_W(type, cmd, arg_type)  _IOC(_IOCTL_WRITE, type, cmd, sizeof(arg_type))
#define _IOC_RW(type, cmd, arg_type) _IOC(_IOCTL_READWRITE, type, cmd, sizeof(arg_type))

// shared commands that ctl standard iod functions
#define FIONBIO  _IOC_W(_IOCTL_SHARED, 0, int) // set blocking io
#define FIOASYNC _IOC_W(_IOCTL_SHARED, 1, int) // set async io
#define FIONREAD _IOC_R(_IOCTL_SHARED, 2, int) // number of bytes available

CRTDECL(int, ioctl(int iod, unsigned long request, ...));

#endif //!__C_IOCTL_H__
