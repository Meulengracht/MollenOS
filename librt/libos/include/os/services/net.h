/**
 * Copyright 2023, Philip Meulengracht
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
 * Net Service Definitions & Structures
 * - This header describes the base structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_SERVICE_NET_H__
#define __OS_SERVICE_NET_H__

#include <os/types/net.h>
#include <os/types/handle.h>

_CODE_BEGIN

/**
 * @brief
 * @param domain
 * @param type
 * @param protocol
 * @param handleOut
 * @return
 */
CRTDECL(oserr_t,
OSSocketOpen(
        _In_  int         domain,
        _In_  int         type,
        _In_  int         protocol,
        _Out_ OSHandle_t* handleOut));

/**
 * @brief
 * @param sock0
 * @param sock1
 * @return
 */
CRTDECL(oserr_t,
OSSocketPair(
        _In_  OSHandle_t* sock0,
        _In_  OSHandle_t* sock1));

/**
 * @brief
 * @param handle
 * @param address
 * @param addressLength
 * @param handleOut
 * @return
 */
CRTDECL(oserr_t,
OSSocketAccept(
        _In_  OSHandle_t*      handle,
        _In_  struct sockaddr* address,
        _In_  socklen_t*       addressLength,
        _Out_ OSHandle_t*      handleOut));

/**
 * @brief
 * @param handle
 * @param address
 * @param addressLength
 * @return
 */
CRTDECL(oserr_t,
OSSocketBind(
        _In_ OSHandle_t*            handle,
        _In_ const struct sockaddr* address,
        _In_ socklen_t              addressLength));

/**
 * @brief
 * @param handle
 * @param address
 * @param addressLength
 * @return
 */
CRTDECL(oserr_t,
OSSocketConnect(
        _In_ OSHandle_t*            handle,
        _In_ const struct sockaddr* address,
        _In_ socklen_t              addressLength));

/**
 * @brief
 * @param handle
 * @param queueSize
 * @return
 */
CRTDECL(oserr_t,
OSSocketListen(
        _In_ OSHandle_t* handle,
        _In_ int         queueSize));

/**
 * @brief
 * @param handle
 * @param type
 * @param address
 * @param addressMaxSize
 * @return
 */
CRTDECL(oserr_t,
OSSocketAddress(
        _In_ OSHandle_t*      handle,
        _In_ int              type,
        _In_ struct sockaddr* address,
        _In_ socklen_t        addressMaxSize));

/**
 * @brief
 * @param handle
 * @param protocol
 * @param option
 * @param data
 * @param length
 * @return
 */
CRTDECL(oserr_t,
OSSocketSetOption(
        _In_ OSHandle_t* handle,
        _In_ int         protocol,
        _In_ int         option,
        _In_ const void* data,
        _In_ socklen_t   length));

/**
 * @brief
 * @param handle
 * @param protocol
 * @param option
 * @param data
 * @param length
 * @return
 */
CRTDECL(oserr_t,
OSSocketOption(
        _In_    OSHandle_t* handle,
        _In_    int         protocol,
        _In_    int         option,
        _In_    void*       data,
        _InOut_ socklen_t*  length));

_CODE_END
#endif //!__OS_SERVICE_NET_H__
