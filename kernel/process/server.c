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
#define __MODULE "SERV"

#include <system/thread.h>
#include <system/utils.h>
#include <process/server.h>
#include <garbagecollector.h>
#include <threading.h>
#include <scheduler.h>
#include <debug.h>
#include <heap.h>

#include <ds/mstring.h>
#include <string.h>

/* PhoenixCreateServer
 * This function loads the executable and
 * prepares the ash-server-environment, at this point
 * it won't be completely running yet, it needs its own thread for that */
UUId_t
PhoenixCreateServer(
	_In_ MString_t *Path)
{
	// Variables
    SystemInformation_t SystemInformation;
	MCoreServer_t *Server = NULL;

	// Allocate and initiate new instance
	Server = (MCoreServer_t*)kmalloc(sizeof(MCoreServer_t));
	if (PhoenixInitializeAsh(&Server->Base, Path) != OsSuccess) {
		ERROR("Failed to spawn server %s", MStringRaw(Path));
		kfree(Server);
		return UUID_INVALID;
	}

    // Get memory information
    SystemInformationQuery(&SystemInformation);

	// Initialize the server io-space memory
	Server->DriverMemory = BlockBitmapCreate(SystemInformation.MemoryOverview.UserDriverMemoryStart,
		SystemInformation.MemoryOverview.UserDriverMemoryStart + SystemInformation.MemoryOverview.UserDriverMemorySize, 
        SystemInformation.AllocationGranularity);

	// Register ash
	Server->Base.Type = AshServer;
	PhoenixRegisterAsh(&Server->Base);

    // Create the loader
	ThreadingCreateThread(MStringRaw(Server->Base.Name),
		PhoenixStartupEntry, Server, THREADING_DRIVERMODE);
	return Server->Base.Id;
}

/* PhoenixCleanupServer
 * Cleans up all the server-specific resources allocated
 * this this AshServer, and afterwards call the base-cleanup */
void
PhoenixCleanupServer(
    _In_ MCoreServer_t *Server)
{
    // Destroy memory bitmap and do base cleanup
	BlockBitmapDestroy(Server->DriverMemory);
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
	return (MCoreServer_t*)Ash;
}
