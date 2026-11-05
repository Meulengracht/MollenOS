/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __OS_TYPES_IPCCONTEXT_H__
#define	__OS_TYPES_IPCCONTEXT_H__

#include <os/osdefs.h>

#define IPC_ADDRESS_HANDLE 0
#define IPC_ADDRESS_PATH   1

/**
 * @brief Identifies the destination IPC endpoint by path or global handle ID.
 * The kernel can send to this endpoint without giving the caller a grant for
 * its buffer.
 */
typedef struct IPCAddress {
    int Type;
    union {
        uuid_t      GlobalHandleID;
        const char* Path;
    } Data;
} IPCAddress_t;

/**
 * @brief Describes an outgoing message and its reply endpoint.
 * SenderHandleID must identify an IPC endpoint in the caller's grant table.
 * Use UUID_INVALID when no reply endpoint is supplied.
 */
typedef struct IPCMessage {
    uuid_t        SenderHandleID;
    IPCAddress_t* Address;
    const void*   Payload;
    size_t        Length;
} IPCMessage_t;

/**
 * @brief Stores the sender's security group, capabilities and reply endpoint.
 * The kernel supplies the group and capabilities and checks the sender endpoint.
 * SenderGlobalHandleID allows replies without a grant for the sender's buffer.
 */
typedef struct IPCMessageContext {
    uint64_t SenderGroupGlobalHandleID;
    uint32_t Capabilities;
    uuid_t   SenderGlobalHandleID;
} IPCMessageContext_t;

#define IPC_ADDRESS_HANDLE_INIT(handle) { IPC_ADDRESS_HANDLE, { handle } }

#define IPC_DONTWAIT 0x1

#endif //!__OS_TYPES_IPCCONTEXT_H__
