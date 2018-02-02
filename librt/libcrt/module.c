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

/* Includes
 * - Library */
#include "../libc/threads/tls.h"
#include <stdlib.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
__EXTERN void __CrtCxxInitialize(void);
__EXTERN void __CrtCxxFinalize(void);
__EXTERN void __CrtAttachTlsBlock(void);
#ifndef __clang__
CRTDECL(void, __CppInitVectoredEH(void));
#endif

/* StdioInitialize
 * Initializes default handles and resources */
CRTDECL(void, StdioInitialize(void *InheritanceBlock, size_t InheritanceBlockLength));

/* StdSignalInitialize
 * Initializes the default signal-handler for the process. */
CRTDECL(void, StdSignalInitialize(void));

/* __cxa_runinitializers 
 * C++ Initializes library C++ runtime for all loaded modules */
CRTDECL(void, __cxa_runinitializers(
    _In_ void (*Initializer)(void), 
    _In_ void (*Finalizer)(void), 
    _In_ void (*TlsAttachFunction)(void)));

/* CRT Initialization sequence
 * for a shared C/C++ environment call this in all entry points */
void
__CrtInitialize(
    _In_ thread_storage_t *Tls)
{
    // Initialize the TLS System
    tls_create(Tls);
    tls_initialize();

    // Initialize C/CPP
    __cxa_runinitializers(__CrtCxxInitialize, __CrtCxxFinalize, __CrtAttachTlsBlock);

    // Initialize STD-C
    StdioInitialize(NULL, 0);
    StdSignalInitialize();

    // If msc, initialize the vectored-eh
#ifndef __clang__
    __CppInitVectoredEH();
#endif
}

/* __CrtModuleEntry
 * Use this entry point for modules. */
void __CrtModuleEntry(void)
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
                        &Message.Arguments[4], Message.From.Process, Message.From.Port);
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
