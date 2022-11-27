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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * IP-Communication
 * - Implementation of inter-thread communication. 
 */

#ifndef __VALI_IPC_CONTEXT_H__
#define __VALI_IPC_CONTEXT_H__

#include <os/osdefs.h>
#include <os/types/ipc.h>

/**
 * IpcContextCreate
 * * 
 */
KERNELAPI oserr_t KERNELABI
IpcContextCreate(
        _In_  size_t  Size,
        _Out_ uuid_t* HandleOut,
        _Out_ void**  UserContextOut);

/**
 * @brief
 */
KERNELAPI oserr_t KERNELABI
IpcContextSendMultiple(
        _In_ IPCMessage_t**    messages,
        _In_ int               messageCount,
        _In_ OSTimestamp_t*    deadline,
        _In_ OSAsyncContext_t* asyncContext);

#endif //!__VALI_IPC_CONTEXT_H__
