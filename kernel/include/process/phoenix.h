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
#include <Threading.h>
#include <Events.h>

/* Definitions for special identifiers
 * these can be used to lookup special ashes/processes */
#define PHOENIX_NO_ASH			0xFFFFFFFF
#define PHOENIX_CURRENT			0

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
typedef struct _MCorePhoenixRequest
{
	/* Event Base 
	 * All event-derived systems must
	 * have this member */
	MCoreEvent_t Base;

	/* Event Parameters, the following
	 * must be filled out for some of the
	 * actions that phoenix support */
	MString_t *Path;
	MString_t *Arguments;

	/* This is a combined parameter, for some
	 * actions it acts as a return, other times it
	 * is the parameter for an action */
	PhxId_t AshId;

} MCorePhoenixRequest_t;

/* These are maintience/initializor functions and 
 * should only be called by system processes */
__CRT_EXTERN void PhoenixInit(void);
__CRT_EXTERN void PhoenixReapZombies(void);

/* Methods for supporting events, use these
 * to send requests to the phoenix system */
__CRT_EXTERN void PhoenixCreateRequest(MCorePhoenixRequest_t *Request);
__CRT_EXTERN void PhoenixWaitRequest(MCorePhoenixRequest_t *Request, size_t Timeout);

/* Signal Functions */
__CRT_EXTERN void SignalHandle(ThreadId_t ThreadId);
__CRT_EXTERN int SignalCreate(PhxId_t AshId, int Signal);
__CRT_EXTERN void SignalExecute(MCoreAsh_t *Ash, MCoreSignal_t *Signal);

/* Architecture Specific  
 * Must be implemented in the arch-layer */
__CRT_EXTERN void SignalDispatch(MCoreAsh_t *Ash, MCoreSignal_t *Signal);

#endif //!_MCORE_PHOENIX_H_