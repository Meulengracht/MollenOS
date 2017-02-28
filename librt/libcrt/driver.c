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
 * MollenOS C Library - Driver Entry 
 */

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/thread.h>

/* Includes
 * - Driver */
#include <os/driver/driver.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <stdlib.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
__EXTERN void __CppInit(void);
__EXTERN void __CppFinit(void);
MOSAPI void __CppInitVectoredEH(void);

/* Globals
 * We want to keep a list of all registered
 * driver-instances, to keep track of what is running */
List_t *__GlbInstances = NULL;

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

/* Driver Entry Point
 * Use this entry point for drivers/servers/modules */
void _mDrvCrt(void)
{
	/* Variables */
	ThreadLocalStorage_t Tls;
	MRemoteCall_t Message;
	DataKey_t Key;
	int IsRunning = 1;

	/* Initialize environment */
	_mCrtInit(&Tls);

	/* Initialize default pipes */
	PipeOpen(PIPE_DEFAULT);
	PipeOpen(PIPE_RPC);

	/* Allocate the list of running instances */
	__GlbInstances = ListCreate(KeyInteger, LIST_NORMAL);

	/* Call the driver load function 
	 * - This will be run once, before loop */
	if (OnLoad() != OsNoError) {
		OnUnload();
		goto Cleanup;
	}

	/* Initialize the driver event loop */
	while (IsRunning) 
	{
		/* Wait for call */
		if (RPCListen(&Message) == OsNoError) {
			/* Which function is called? */
			switch (Message.Function) {
				case __DRIVER_REGISTERINSTANCE: {
					Key.Value = 0;
					Message.Arguments[0].Type = ARGUMENT_NOTUSED;
					OnRegister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
					ListAppend(__GlbInstances, 
						ListCreateNode(Key, Key, (void*)Message.Arguments[0].Data.Buffer));
				} break;
				case __DRIVER_UNREGISTERINSTANCE: {
					OnUnregister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
				} break;
				case __DRIVER_INTERRUPT: {
					if (OnInterrupt((void*)Message.Arguments[0].Data.Value)
						== InterruptHandled) {
						/* InterruptAcknowledge() */
					}
				} break;
				case __DRIVER_QUERY: {
					OnQuery((MContractType_t)Message.Arguments[0].Data.Value, 
						(int)Message.Arguments[1].Data.Value, 
						&Message.Arguments[2], &Message.Arguments[3], 
						&Message.Arguments[4], Message.Sender, Message.ResponsePort);
				} break;
				case __DRIVER_UNLOAD: {
					IsRunning = 0;
				} break;

				default: {
					break;
				}
			}

			/* Cleanup message */
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
	ListDestroy(__GlbInstances);
	PipeClose(PIPE_DEFAULT);
	PipeClose(PIPE_RPC);
	exit(-1);
}
