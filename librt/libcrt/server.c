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
 * MollenOS C Library - Server Entry 
 */

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/thread.h>

/* Includes
 * - Driver */
#include <os/driver/server.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <stdlib.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
__EXTERN void __CppInit(void);
__EXTERN void __CppFinit(void);
_MOS_API void __CppInitVectoredEH(void);

/* CRT Initialization sequence
 * for a shared C/C++ environment
 * call this in all entry points */
void _mCrtInit(ThreadLocalStorage_t *Tls)
{
	/* Init Crt */
	__CppInit();

	/* Initialize the TLS */
	TLSInitInstance(Tls);

	/* Init TLS */
	TLSInit();

	/* Init EH */
	__CppInitVectoredEH();
}

/* Server Entry Point
 * Use this entry point for servers */
void _mDrvCrt(void)
{
	/* Variables */
	ThreadLocalStorage_t Tls;
	MRemoteCall_t Message;
	int IsRunning = 1;

	/* Initialize environment */
	_mCrtInit(&Tls);

	/* Initialize default pipes */
	PipeOpen(PIPE_DEFAULT);
	PipeOpen(PIPE_RPC);

	/* Call the driver load function 
	 * - This will be run once, before loop */
	if (OnLoad() != OsNoError) {
		OnUnload();
		goto Cleanup;
	}

	/* Initialize the server event loop */
	while (IsRunning) {
		if (RPCListen(&Message) == OsNoError) {
			OnEvent(&Message);
			RPCCleanup(&Message);
		}
		else {
			/* Something went wrong, wtf? */
		}
	}

	/* Call unload, so driver can cleanup */
	OnUnload();

Cleanup:
	/* Cleanup allocated resources
	 * and perform a normal exit */
	PipeClose(PIPE_DEFAULT);
	PipeClose(PIPE_RPC);
	exit(-1);
}
