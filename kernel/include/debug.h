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
 * MollenOS Debugging Interface
 * - Contains the shared kernel debugging interface and tools
 *   available for tracing and debugging
 *
 * - To use the interface, and tracing utilities a bunch of defines
 *   can be enabled for the various tools:
 * - __MODULE(Str4)		(4 Letter Module Identification String)
 * - __TRACE			(Enables Tracing Tools)
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/context.h>

/* Includes
 * - Systems */
#include <log.h>

/* Sanitize the module definition, must be 
 * present to identify the source-code that include this */
#ifndef __MODULE
#error "__MODULE Must be defined before including debug.h"
#endif

/* Global <toggable> definitions
 * These can be turned on per-source file by pre-defining
 * the __TRACE before inclusion */
#if defined(__TRACE) && defined(__OSCONFIG_LOGGING_KTRACE)
#define TRACE(...)					LogInformation(__MODULE, __VA_ARGS__)
#else
#define TRACE(...)
#endif

/* Global <always-on> definitions
 * These are enabled no matter which kind of debugging is enabled */
#define FATAL_SCOPE_KERNEL			0x00000001
#define FATAL_SCOPE_PROCESS			0x00000002
#define FATAL_SCOPE_THREAD			0x00000003

#define WRITELINE(...)              LogDebug(__MODULE, __VA_ARGS__)
#define WARNING(...)				LogDebug(__MODULE, __VA_ARGS__)
#define ERROR(...)					LogFatal(__MODULE, __VA_ARGS__)
#define FATAL(Scope, ...)			DebugPanic(Scope, __MODULE, __VA_ARGS__)
#define NOTIMPLEMENTED(...)

/* DebugInstallPageFaultHandlers
 * Install page-fault handlers. Should be called as soon as memory setup phase is done */
KERNELAPI
OsStatus_t
KERNELABI
DebugInstallPageFaultHandlers(void);

/* DebugSingleStep
 * Handles the SingleStep trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
KERNELAPI
OsStatus_t
KERNELABI
DebugSingleStep(
	_In_ Context_t *Context);

/* DebugBreakpoint
 * Handles the Breakpoint trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
KERNELAPI
OsStatus_t
KERNELABI
DebugBreakpoint(
	_In_ Context_t *Context);

/* DebugPageFault
 * Handles page-fault and either validates or invalidates
 * that the address is valid. In case of valid address it automatically
 * maps in the page and returns OsSuccess */
KERNELAPI
OsStatus_t
KERNELABI
DebugPageFault(
	_In_ Context_t *Context,
	_In_ uintptr_t Address);

/* DebugPanic
 * Kernel panic function - Call this to enter panic mode
 * and disrupt normal functioning. This function does not
 * return again */
KERNELAPI
OsStatus_t
KERNELABI
DebugPanic(
	_In_ int FatalityScope,
	_In_ __CONST char *Module,
	_In_ __CONST char *Message, ...);

/* DebugGetModuleByAddress
 * Retrieves the module (Executable) at the given address */
KERNELAPI
OsStatus_t
KERNELABI
DebugGetModuleByAddress(
	_In_ uintptr_t Address, 
	_Out_ uintptr_t *Base, 
	_Out_ char **Name);

/* DebugStackTrace
 * Performs a verbose stack trace in the current context 
 * Goes back a maximum of <MaxFrames> in the stack */
KERNELAPI
OsStatus_t
KERNELABI
DebugStackTrace(
	_In_ size_t MaxFrames);

/* DebugMemory 
 * Dumps memory in the form of <data> <string> at the
 * given address and length of memory dump */
KERNELAPI
OsStatus_t
KERNELABI
DebugMemory(
	_In_Opt_ __CONST char *Description,
	_In_ void *Address,
	_In_ size_t Length);

/* DebugContext 
 * Dumps the contents of the given context for debugging */
KERNELAPI
OsStatus_t
KERNELABI
DebugContext(
	_In_ Context_t *Context);

#endif //!_DEBUG_H_
