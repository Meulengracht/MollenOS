/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Buffer Support Definitions & Structures
 * - This header describes the base buffer-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __BUFFER_INTERFACE__
#define __BUFFER_INTERFACE__

#include <ddk/ddkdefs.h>
#include <stdio.h>

// Structured system buffer object
// Contains information about a dma buffer for use with transfers,
// shared memory or hardware interaction.
typedef struct {
    UUId_t BufferHandle;
    void*  Data;
    off_t  Position;
    size_t Length;
    size_t Capacity;
} ManagedBuffer_t;

_CODE_BEGIN
DDKDECL(OsStatus_t,
BufferCreate(
    _In_  size_t  InitialSize,
    _In_  size_t  Capacity,
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut,
    _Out_ void*   BufferOut));

DDKDECL(OsStatus_t,
BufferCreateFrom(
    _In_  UUId_t ExistingHandle,
    _Out_ void** BufferOut));

DDKDECL(OsStatus_t,
BufferDestroy(
    _In_ UUId_t Handle,
    _In_ void*  Buffer));

DDKDECL(OsStatus_t,
BufferResize(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Size));

DDKDECL(OsStatus_t,
BufferGetMetrics(
    _In_  UUId_t  Handle,
    _Out_ size_t* SizeOut,
    _Out_ size_t* CapacityOut));

DDKDECL(OsStatus_t,
BufferGetVectors(
    _In_  UUId_t  Handle,
    _Out_ uintptr_t VectorOut[]));

DDKDECL(OsStatus_t,
BufferCreateManaged(
    _In_  UUId_t            ExistingHandle,
    _Out_ ManagedBuffer_t** BufferOut));

DDKDECL(OsStatus_t,
BufferDestroyManaged(
    _In_ ManagedBuffer_t* Buffer));

DDKDECL(OsStatus_t,
BufferZero(
    _In_ ManagedBuffer_t* Buffer));

DDKDECL(OsStatus_t,
BufferSeek(
    _In_ ManagedBuffer_t* Buffer,
    _In_ off_t            Offset));

DDKDECL(OsStatus_t,
BufferRead(
    _In_  ManagedBuffer_t* Buffer,
    _In_  void*            Data,
    _In_  size_t           Length,
    _Out_ size_t*          BytesReadOut));

DDKDECL(OsStatus_t,
BufferWrite(
    _In_  ManagedBuffer_t* Buffer,
    _In_  const void*      Data,
    _In_  size_t           Length,
    _Out_ size_t*          BytesWrittenOut));

DDKDECL(OsStatus_t,
BufferCombine(
    _In_ ManagedBuffer_t* Destination,
    _In_ ManagedBuffer_t* Source,
    _In_ size_t           BytesToTransfer,
    _In_ size_t*          BytesTransferredOut));
_CODE_END

#endif //!_BUFFER_INTERFACE_H_
