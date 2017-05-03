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
#include <os/driver/driver.h>
#include <os/mollenos.h>
#include <os/thread.h>

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

/* CRT Initialization sequence
 * for a shared C/C++ environment
 * call this in all entry points */
void _mCrtInit(ThreadLocalStorage_t *Tls)
{
	__CppInit();
	TLSInitInstance(Tls);
	TLSInit();
	__CppInitVectoredEH();
}

/* Driver Entry Point
 * Use this entry point for drivers/servers/modules */
void _mDrvCrt(void)
{
	// Variables
	ThreadLocalStorage_t Tls;
	MRemoteCall_t Message;
	int IsRunning = 1;

	// Initialize environment
	_mCrtInit(&Tls);

	// Initialize default pipes
	PipeOpen(PIPE_RPCOUT);
	PipeOpen(PIPE_RPCIN);

	// Call the driver load function 
	// - This will be run once, before loop
	if (OnLoad() != OsSuccess) {
		OnUnload();
		goto Cleanup;
	}

	// Initialize the driver event loop
	while (IsRunning) {
		if (RPCListen(&Message) == OsSuccess) {
			switch (Message.Function) {
				case __DRIVER_REGISTERINSTANCE: {
					OnRegister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
				} break;
				case __DRIVER_UNREGISTERINSTANCE: {
					OnUnregister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
				} break;
				case __DRIVER_INTERRUPT: {
					if (OnInterrupt((void*)Message.Arguments[1].Data.Value) == InterruptHandled) {
						AcknowledgeInterrupt((UUId_t)Message.Arguments[0].Data.Value);
					}
				} break;
				case __DRIVER_TIMEOUT: {
					OnTimeout((UUId_t)Message.Arguments[0].Data.Value,
						(void*)Message.Arguments[1].Data.Value);
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
			RPCCleanup(&Message);
		}
		else { }
	}

	// Call unload, so driver can cleanup
	OnUnload();

Cleanup:
	// Cleanup allocated resources
	// and perform a normal exit
	PipeClose(PIPE_RPCOUT);
	PipeClose(PIPE_RPCIN);
	exit(-1);
}
