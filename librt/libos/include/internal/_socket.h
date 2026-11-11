/**
 * Copyright, Philip Meulengracht
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
 */

#ifndef __OS_INTERNAL_SOCKET_H__
#define __OS_INTERNAL_SOCKET_H__

#include <ds/streambuffer.h>
#include <stdatomic.h>

/**
 * Shared state at the start of a socket's send mapping, followed by its ring.
 * Netd publishes the connected socket ID before completing connect or pair.
 * Clients copy that ID into each packet so reconnects cannot redirect queued
 * packets. UUID_INVALID means that no peer is connected.
 */
struct OSSocketSendState {
    _Alignas(streambuffer_t) _Atomic(uuid_t) Peer;
};

/**
 * @brief Returns the send ring following an attached socket state header.
 *
 * @param state The start of the mapped send buffer. Must not be NULL.
 * @return The shared send ring.
 */
static inline streambuffer_t*
__SocketSendStream(
    _In_ struct OSSocketSendState* state)
{
    return (streambuffer_t*)(state + 1);
}

#endif //!__OS_INTERNAL_SOCKET_H__
