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
#include <os/process.h>

/* Includes
 * - Library */
#include "../libc/threads/tls.h"
#include <stdlib.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
#ifdef __clang__
__EXTERN void __CrtCxxInitialize(void);
__EXTERN void __CrtCxxFinalize(void);
#else
__EXTERN void __CppInit(void);
__EXTERN void __CppFinit(void);
CRTDECL(void, __CppInitVectoredEH(void));
#endif

/* ProcessGetModuleEntryPoints
 * Retrieves a list of loaded modules for the process and
 * their entry points. */
CRTDECL(OsStatus_t,
ProcessGetModuleEntryPoints(
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]));

/* StdioInitialize
 * Initializes default handles and resources */
CRTDECL(void, StdioInitialize(void));

/* StdSignalInitialize
 * Initializes the default signal-handler for the process. */
CRTDECL(void, StdSignalInitialize(void));

/* CRT Initialization sequence
 * for a shared C/C++ environment call this in all entry points */
void
__CrtInitialize(
    _In_ thread_storage_t *Tls)
{
    // Variables
    Handle_t ModuleList[PROCESS_MAXMODULES];

    // Initialize C/CPP
#ifdef __clang__
    __CrtCxxInitialize();
#else
    __CppInit();
#endif

    // Initialize the TLS System
    tls_create(Tls);
    tls_initialize();

    // Get modules available
    if (ProcessGetModuleEntryPoints(ModuleList) == OsSuccess) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(0);
        }
    }

    // Initialize STD-C
    StdioInitialize();
    StdSignalInitialize();

    // If msc, initialize the vectored-eh
#ifndef __clang__
    __CppInitVectoredEH();
#endif
}

/* Driver Entry Point
 * Use this entry point for drivers/servers/modules */
void _mDrvCrt(void)
{
    // Variables
    thread_storage_t        Tls;
    MRemoteCall_t           Message;
    char *ArgumentBuffer    = NULL;
    int IsRunning           = 1;

    // Initialize environment
    __CrtInitialize(&Tls);

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
    ArgumentBuffer = (char*)malloc(IPC_MAX_MESSAGELENGTH);
    while (IsRunning) {
        if (RPCListen(&Message, ArgumentBuffer) == OsSuccess) {
            switch (Message.Function) {
                case __DRIVER_REGISTERINSTANCE: {
                    OnRegister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
                } break;
                case __DRIVER_UNREGISTERINSTANCE: {
                    OnUnregister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
                } break;
                case __DRIVER_INTERRUPT: {
                    OnInterrupt((void*)Message.Arguments[1].Data.Value,
                        Message.Arguments[2].Data.Value,
                        Message.Arguments[3].Data.Value,
                        Message.Arguments[4].Data.Value);
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
        }
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
