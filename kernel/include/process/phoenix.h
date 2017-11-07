/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 */

#ifndef _MCORE_PHOENIX_H_
#define _MCORE_PHOENIX_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <process/ash.h>
#include <threading.h>
#include <events.h>

/* Definitions for special identifiers
 * these can be used to lookup special ashes/processes */
#define PHOENIX_MAX_ASHES		512
#define PHOENIX_ALIAS_BASE		0x8000

/* The type of action that are supported
 * by Phoenix, each of these actions support
 * different parameters as well, especially 
 * the spawn command */
typedef enum _PhoenixRequestType {
	AshSpawnProcess,
	AshSpawnServer,
	AshPing,
	AshKill,
	AshQuery
} PhoenixRequestType_t;

/* The Phoenix request structure, it builds
 * on the event system we have created, using
 * it as an base, and then supports parameters
 * for requests in PhoneixRequestType */
typedef struct _MCorePhoenixRequest {
	MCoreEvent_t             Base;
    MString_t               *Path;
    
	union {
		MString_t           *String;
		struct {
			void            *Data;
			size_t           Length;
		} Raw;
	} Arguments;

	// This is a combined parameter, for some
	// actions it acts as a return, other times it
	// is the parameter for an action
	UUId_t AshId;
} MCorePhoenixRequest_t;

/* PhoenixInitialize
 * Initialize the Phoenix environment and 
 * start the event-handler loop, it handles all requests 
 * and nothing happens if it isn't started */
KERNELAPI
void
KERNELABI
PhoenixInitialize(void);

/* PhoenixCreateRequest
 * Creates and queues up a new request for the process-manager. */
KERNELAPI
void
KERNELABI
PhoenixCreateRequest(
    _In_ MCorePhoenixRequest_t *Request);

/* PhoenixWaitRequest
 * Wait for a request to finish. A timeout can be specified. */
KERNELAPI
void
KERNELABI
PhoenixWaitRequest(
    _In_ MCorePhoenixRequest_t *Request,
    _In_ size_t Timeout);

/* PhoenixRegisterAlias
 * Allows a server to register an alias for its id
 * which means that id (must be above SERVER_ALIAS_BASE)
 * will always refer the calling process */
KERNELAPI
OsStatus_t
KERNELABI
PhoenixRegisterAlias(
	_In_ MCoreAsh_t *Ash, 
    _In_ UUId_t Alias);
    
/* PhoenixUpdateAlias
 * Checks if the given process-id has an registered alias.
 * If it has, the given process-id will be overwritten. */
KERNELAPI
OsStatus_t
KERNELABI
PhoenixUpdateAlias(
    _InOut_ UUId_t *AshId);

/* PhoenixRegisterAsh
 * Registers a new ash by adding it to the process-list */
KERNELAPI
OsStatus_t
KERNELABI
PhoenixRegisterAsh(
    _In_ MCoreAsh_t *Ash);
    
/* PhoenixGetProcesses 
 * Retrieves access to the list of processes. */
KERNELAPI
List_t*
KERNELABI
PhoenixGetProcesses(void);

/* PhoenixGetNextId 
 * Retrieves the next process-id. */
KERNELAPI
UUId_t
KERNELABI
PhoenixGetNextId(void);

/* SignalReturn
 * Call upon returning from a signal, this will finish
 * the signal-call and enter a new signal if any is queued up */
KERNELAPI
OsStatus_t
KERNELABI
SignalReturn(void);

/* Handle Signal 
 * This checks if the process has any waiting signals
 * and if it has, it executes the first in list */
KERNELAPI
OsStatus_t
KERNELABI
SignalHandle(
	_In_ UUId_t ThreadId);

__EXTERN int SignalCreate(UUId_t AshId, int Signal);
__EXTERN void SignalExecute(MCoreAsh_t *Ash, MCoreSignal_t *Signal);

/* Architecture Specific  
 * Must be implemented in the arch-layer */
KERNELAPI
OsStatus_t
KERNELABI
SignalDispatch(
	_In_ MCoreAsh_t *Ash, 
	_In_ MCoreSignal_t *Signal);

#endif //!_MCORE_PHOENIX_H_