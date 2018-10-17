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
#include <machine.h>
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
	MCoreServer_t*  Server;
    UUId_t          ThreadId;

	Server = (MCoreServer_t*)kmalloc(sizeof(MCoreServer_t));
	if (PhoenixInitializeAsh(&Server->Base, Path) != OsSuccess) {
		ERROR("Failed to spawn server %s", MStringRaw(Path));
		kfree(Server);
		return UUID_INVALID;
	}
	Server->Base.Type = AshServer;
	PhoenixRegisterAsh(&Server->Base);

    // Create the loader
	ThreadId = ThreadingCreateThread(MStringRaw(Server->Base.Name), PhoenixStartupEntry, Server, THREADING_DRIVERMODE);
    ThreadingDetachThread(ThreadId);
	return Server->Base.Id;
}

/* PhoenixGetServer
 * This function looks up a server structure by id */
MCoreServer_t*
PhoenixGetServer(
	_In_ UUId_t ServerId)
{
	MCoreAsh_t *Ash = PhoenixGetAsh(ServerId);
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
	MCoreAsh_t *Ash = PhoenixGetCurrentAsh();
	if (Ash != NULL && Ash->Type != AshServer) {
		return NULL;
	}
	return (MCoreServer_t*)Ash;
}
