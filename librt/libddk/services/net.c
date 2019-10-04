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
 * Network Service (Protected) Definitions & Structures
 * - This header describes the base networking-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/net.h>

/////////////////////////////////////////////////////
// APPLICATIONS => NetworkService
// The communication between applications and the network service
// consists of the use of streambuffers that are essentially a little more
// complex ringbuffers. They support some advanced use cases to fit the 
// inet/socket.h interface. This also means they are pretty useless for anything
// else than socket communication. Applications both read and write from/to the
// streambuffers, which are read and written by the network service.

/////////////////////////////////////////////////////
// NetworkService => DRIVERS
// The communication between the drivers and the network service are a little more
// dump. The NetworkService allocates two memory pools per driver as shared buffers.
// The first one, the send buffer, is then filled with data received from applications.
// The send buffer is split up into frames of N size (determined by max-packet
// from the driver), and then queued up by the NetworkService.
// The second one, the recv buffer, is filled with data received from the driver.
// The recv buffer is split up into frames of N size (determined by max-packet 
// from the driver), and queued up for listening.

OsStatus_t
CreateSocket(
    _In_  int              Domain,
    _In_  int              Type,
    _In_  int              Protocol,
    _Out_ UUId_t*          HandleOut,
    _Out_ streambuffer_t** RecvQueueOut,
    _Out_ streambuffer_t** SendQueueOut)
{
    return OsNotSupported;
}

OsStatus_t
InheritSocket(
    _In_  UUId_t           Handle,
    _Out_ streambuffer_t** RecvQueueOut,
    _Out_ streambuffer_t** SendQueueOut)
{
    return OsNotSupported;
}

OsStatus_t
BindSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    return OsNotSupported;
}

OsStatus_t
ConnectSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    return OsNotSupported;
}

OsStatus_t
GetSocketAddress(
    _In_    UUId_t                 Handle,
    _Out_   const struct sockaddr* AddressOut,
    _InOut_ socklen_t*             AddressLengthOut)
{
    return OsNotSupported;
}
