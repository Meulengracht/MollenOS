/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

/* Includes
* - System */
#include <system/thread.h>
#include <process/server.h>
#include <garbagecollector.h>
#include <threading.h>
#include <semaphore.h>
#include <scheduler.h>
#include <heap.h>
#include <log.h>

/* Includes
* - C-Library */
#include <stddef.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Externs, we need access to the list of
 * servers and the list of alias's */
__EXTERN List_t *GlbAshes;

/* This is the finalizor function for starting
 * up a new Ash Server, it finishes setting up the environment
 * and memory mappings, must be called on it's own thread */
void
PhoenixBootServer(
	_In_ void *Args)
{
	// Initiate pointer
	MCoreServer_t *Server = (MCoreServer_t*)Args;

	// Finish the standard setup of the ash
	PhoenixFinishAsh(&Server->Base);

	// Initialize the server io-space memory
	Server->DriverMemory = BitmapCreate(MEMORY_LOCATION_RING3_IOSPACE,
		MEMORY_LOCATION_RING3_IOSPACE_END, PAGE_SIZE);

	// Map in arguments
	if (Server->ArgumentLength != 0) {
		AddressSpaceMap(AddressSpaceGetCurrent(),
			MEMORY_LOCATION_RING3_ARGS, MAX(PAGE_SIZE, Server->ArgumentLength),
			__MASK, AS_FLAG_APPLICATION, NULL);

		// Copy arguments
		memcpy((void*)MEMORY_LOCATION_RING3_ARGS,
			Server->ArgumentBuffer, Server->ArgumentLength);
	}

	// Enter userland - should never return
	ThreadingEnterUserMode(Server);

	// Yield and safety catch
	ThreadingYield();
	for (;;);
}

/* This function loads the executable and
 * prepares the ash-server-environment, at this point
 * it won't be completely running yet, it needs its own thread for that */
UUId_t
PhoenixCreateServer(
	_In_ MString_t *Path,
	_In_ void *Arguments, 
	_In_ size_t Length)
{
	// Variables
	MCoreServer_t *Server = NULL;
	DataKey_t Key;

	/* Allocate the structure */
	Server = (MCoreServer_t*)kmalloc(sizeof(MCoreServer_t));

	/* Sanitize the created Ash */
	if (PhoenixInitializeAsh(&Server->Base, Path)) {
		LogFatal("SERV", "Failed to spawn server %s", MStringRaw(Path));
		kfree(Server);
		return UUID_INVALID;
	}

	/* Set type of base to process */
	Server->Base.Type = AshServer;

	/* Save arguments */
	if (Arguments != NULL && Length != 0) {
		Server->ArgumentBuffer = kmalloc(Length);
		memcpy(Server->ArgumentBuffer, Arguments, Length);
	}
	else {
		Server->ArgumentBuffer = NULL;
		Server->ArgumentLength = 0;
	}
	
	/* Add process to list */
	Key.Value = (int)Server->Base.Id;
	ListAppend(GlbAshes, ListCreateNode(Key, Key, Server));

	/* Create the loader thread */
	ThreadingCreateThread(MStringRaw(Server->Base.Name),
		PhoenixBootServer, Server, THREADING_DRIVERMODE);

	/* Done */
	return Server->Base.Id;
}

/* Cleans up all the server-specific resources allocated
 * this this AshServer, and afterwards call the base-cleanup */
void PhoenixCleanupServer(MCoreServer_t *Server)
{
	/* Cleanup Strings */
	if (Server->ArgumentBuffer != NULL) {
		MStringDestroy(Server->ArgumentBuffer);
	}

	/* Cleanup bitmap */
	BitmapDestroy(Server->DriverMemory);

	/* Now that we have cleaned up all
	* process-specifics, we want to just use the base
	* cleanup */
	PhoenixCleanupAsh((MCoreAsh_t*)Server);
}

/* PhoenixGetServer
 * This function looks up a server structure by id */
MCoreServer_t*
PhoenixGetServer(
	_In_ UUId_t ServerId)
{
	// Use the default ash-lookup
	MCoreAsh_t *Ash = PhoenixGetAsh(ServerId);

	// Do a null check and type-check
	if (Ash != NULL && Ash->Type != AshServer) {
		return NULL;
	}

	// Return the result, but cast it to
	// the process structure
	return (MCoreServer_t*)Ash;
}

/* PhoenixGetCurrentServer
 * If the current running process is a server then it
 * returns the server structure, otherwise NULL */
MCoreServer_t*
PhoenixGetCurrentServer(void)
{
	// Use the default get current
	MCoreAsh_t *Ash = PhoenixGetCurrentAsh();

	// Do a null check and type-check
	if (Ash != NULL && Ash->Type != AshServer) {
		return NULL;
	}

	// Return the result, but cast it to
	// the process structure
	return (MCoreServer_t*)Ash;
}

/* GetServerByDriver
 * Retrieves a running server by driver-information
 * to avoid spawning multiple servers */
MCoreServer_t *PhoenixGetServerByDriver(DevInfo_t VendorId,
	DevInfo_t DeviceId, DevInfo_t DeviceClass, DevInfo_t DeviceSubClass)
{
	/* Iterate the list and try
	 * to locate the ash we have */
	foreach(pNode, GlbAshes) {
		MCoreAsh_t *Ash = (MCoreAsh_t*)pNode->Data;
		if (Ash->Type == AshServer) {
			MCoreServer_t *Server = (MCoreServer_t*)Ash;

			/* Check vendorid/deviceid first */
			if (Server->VendorId == VendorId
				&& Server->DeviceId == DeviceId) {
				return Server;
			}

			/* Check class/subclass combination then 
			 * However vendor can't be fixed */
			if (Server->VendorId != 0xFFEF
				&& Server->DeviceClass == DeviceClass
				&& Server->DeviceSubClass == DeviceSubClass) {
				return Server;
			}
		}
	}

	/* Not found */
	return NULL;
}
