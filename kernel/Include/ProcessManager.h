/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Process Manager
*/
#ifndef _MCORE_PROCESS_MANAGER_H_
#define _MCORE_PROCESS_MANAGER_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#include <RingBuffer.h>
#include <MString.h>
#include <Heap.h>

/* Definitions */
typedef unsigned int PId_t;

/* Structures */
typedef struct _MCoreProcess
{
	/* Id */
	PId_t Id;

	/* Name */
	MString_t *Name;

	/* Working Directory */
	MString_t *WorkingDirectory;

	/* Pipes */
	RingBuffer_t *iPipe;
	RingBuffer_t *oPipe;

	/* Heap */
	Heap_t *Heap;

	/* Address Space */
	AddressSpace_t *AddrSpace;

	/* Arch Specific */
	void *ArchData;

} MCoreProcess_t;

/* Process Request Type */
typedef enum _ProcessRequestType
{
	ProcessSpawn,
	ProcessKill,
	ProcessQuery

} ProcessRequestType_t;

/* Process Request State */
typedef enum _ProcessRequestState
{
	ProcessRequestPending,
	ProcessRequestInProgress,
	ProcessRequestOk,
	ProcessRequestFailed

} ProcessRequestState_t;

/* Process Request */
typedef struct _MCoreProcessRequest
{
	/* Type */
	ProcessRequestType_t Type;

	/* State */
	ProcessRequestState_t State;

	/* Data */
	void *Data;

} MCoreProcessRequest_t;

/* Prototypes */
_CRT_EXTERN void PmInit(void);

/* Process Functions */
_CRT_EXTERN MCoreProcess_t *PmGetProcess(PId_t ProcessId);

/* Requests */
_CRT_EXTERN void PmCreateRequest(MCoreProcessRequest_t *Request);
_CRT_EXTERN void PmWaitRequest(MCoreProcessRequest_t *Request);

#endif //!_MCORE_PROCESS_MANAGER_H_