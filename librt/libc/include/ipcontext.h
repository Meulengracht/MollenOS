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

/**
 * @brief Recieve an IPC message from an IPC stream. Unless IPC_DONTWAIT is passed in flags this
 * is a blocking operation, and will wait for a message to be available.
 * @param[In] iod The io descriptor the ipc stream is bound to.
 * @param[In] buffer The buffer to store the message in.
 * @param[In] len The maximum number of bytes to read from the IPC message. It's important to note
 *                that if the message is longer than this value, then the message is truncated to this
 *                length.
 * @param[In] flags Flags supported are IPC_* flags, like IPC_DONTWAIT.
 * @param[Out] fromHandle If a message was received, then this is set to the sender's address.
 *                        This handle can then be used in conjunction with IPC_ADDRESS_HANDLE_INIT.
 * @return If successful, returns the number of bytes received in buffer
 *         If an error occurs, this function returns -1, and sets a status code
 *         in errno.
 */
CRTDECL(int, iprecv(int iod, void* buffer, unsigned int len, int flags, uuid_t* fromHandle));

_CODE_END
#endif //!__IPCONTEXT_H__
