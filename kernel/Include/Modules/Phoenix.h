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
#include <Modules/Ash.h>
#include <Events.h>

/* Definitions for special identifiers
 * these can be used to lookup special ashes/processes */
#define PHOENIX_NO_ASH			0xFFFFFFFF
#define PHOENIX_CURRENT			0

/* Process Request Type */
typedef enum _PhoenixRequestType {
	AshSpawn,
	AshKill,
	AshQuery
} PhoenixRequestType_t;

/* Process Request */
typedef struct _MCorePhoenixRequest
{
	/* Event Base */
	MCoreEvent_t Base;

	/* Creation Data */
	MString_t *Path;
	MString_t *Arguments;

	/* Process Id */
	PhxId_t ProcessId;

} MCorePhoenixRequest_t;

/* Phoenix Queries
 * List of the different options
 * for querying of ashes */
typedef enum _PhoenixQueryFunction {
	AshQueryName,
	AshQueryMemory,
	AshQueryParent,
	AshQueryTopMostParent
} PhoenixQueryFunction_t;

/* Prototypes */
__CRT_EXTERN void PhoenixInit(void);
__CRT_EXTERN void PhoenixReapZombies(void);

/* Requests */
__CRT_EXTERN void PhoenixCreateRequest(MCorePhoenixRequest_t *Request);
__CRT_EXTERN void PhoenixWaitRequest(MCorePhoenixRequest_t *Request, size_t Timeout);

/* Phoenix Function Prototypes
 * these are the interesting ones */
__CRT_EXTERN int PhoenixQueryAsh(MCoreAsh_t *Ash, 
	PhoenixQueryFunction_t Function, void *Buffer, size_t Length);
__CRT_EXTERN void PhoenixCleanupAsh(MCoreAsh_t *Ash);
__CRT_EXTERN void PhoenixTerminateAsh(MCoreAsh_t *Ash);

/* Lookup Ash 
 * This function looks up a ash structure 
 * by id, if either PHOENIX_CURRENT or PHOENIX_NO_ASH 
 * is passed, it retrieves the current process */
__CRT_EXTERN MCoreAsh_t *PhoenixGetAsh(PhxId_t AshId);

/* Signal Functions */
__CRT_EXTERN void SignalHandle(ThreadId_t ThreadId);
__CRT_EXTERN int SignalCreate(PhxId_t AshId, int Signal);
__CRT_EXTERN void SignalExecute(MCoreAsh_t *Process, MCoreSignal_t *Signal);

/* Architecture Specific  
 * Must be implemented in the arch-layer */
__CRT_EXTERN void SignalDispatch(MCoreAsh_t *Process, MCoreSignal_t *Signal);

#endif //!_MCORE_PHOENIX_H_