/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __IPCONTEXT_H__
#define	__IPCONTEXT_H__

#include <os/types/ipc.h>

_CODE_BEGIN
CRTDECL(int, ipcontext(unsigned int len, IPCAddress_t* addr));
CRTDECL(int, ipsend(int iod, IPCAddress_t* addr, const void* data, unsigned int len, int timeout));
CRTDECL(int, iprecv(int iod, void* buffer, unsigned int len, int flags, uuid_t* fromHandle));
_CODE_END

#endif //!__IPCONTEXT_H__
