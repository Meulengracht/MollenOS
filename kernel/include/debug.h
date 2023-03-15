/**
 * Copyright 2023, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Debugging Interface
 * - Contains the shared kernel debugging interface and tools
 *   available for tracing and debugging
 *
 * - To use the interface, and tracing utilities the following defines
 *   can be enabled for the various tools:
 * - __MODULE           (Sets the module information)
 * - __TRACE            (Enables Tracing Tools)
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <component/memory.h>
#include <os/context.h>
#include <os/osdefs.h>
#include <log.h>

#ifdef TESTING
#include <stdio.h>
#define PRIxIN "zx"
#endif

// __func__ 
/* Global <toggable> definitions
 * These can be turned on per-source file by pre-defining the __TRACE before inclusion */
#if defined(__TRACE) && defined(__OSCONFIG_LOGGING_KTRACE)
#define TRACE(...) LogAppendMessage(LOG_TRACE, __VA_ARGS__)
#define TRACE2(...) LogAppendMessage(LOG_TRACE, "[" __MODULE "] [" __FUNC__ "] " __VA_ARGS__)
#elif defined(__TRACE) && defined(TESTING)
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

/* Global <always-on> definitions
 * These are enabled no matter which kind of debugging is enabled */
#define FATAL_SCOPE_KERNEL        	0x00000001
#define FATAL_SCOPE_PROCESS       	0x00000002
#define FATAL_SCOPE_THREAD         	0x00000003

#ifndef TESTING
#define DEBUG(...)              LogAppendMessage(LOG_DEBUG, __VA_ARGS__)
#define WARNING(...)            LogAppendMessage(LOG_WARNING, __VA_ARGS__)
#define WARNING_IF(cond, ...)   { if ((cond)) { LogAppendMessage(LOG_WARNING, __VA_ARGS__); } }
#define ERROR(...)              LogAppendMessage(LOG_ERROR, __VA_ARGS__)
#define FATAL(Scope, ...)       DebugPanic(Scope, NULL, __VA_ARGS__)
#define NOTIMPLEMENTED(Message) DebugPanic(FATAL_SCOPE_KERNEL, NULL, "NOT-IMPLEMENTED: %s, line %d, %s", __FILE__, __LINE__, Message)
#define TODO(Message)           LogAppendMessage(LOG_WARNING, "TODO: %s, line %d, %s", __FILE__, __LINE__, Message)
#else //!TESTING
#define WARNING(...)            printf(__VA_ARGS__)
#define ERROR(...)              fprintf(stderr, __VA_ARGS__)
#define FATAL(Scope, ...)       fprintf(stderr, __VA_ARGS__)
#endif //!TESTING

enum PageFaultResult {
    // Indicate that a standard allocated page was hit, and the address was
    // successfully mapped.
    PAGEFAULT_RESULT_MAPPED,
    // Indicate that a guard page was hit, meaning a likely stack overflow
    // has occurred in a stack region. No mapping was created and this is a fatal
    // error code.
    PAGEFAULT_RESULT_OVERFLOW,
    // Indicate that a trap page was hit and successfully mapped. This is
    // a separate error code as there should still be an exception propagated
    // to userspace, to inform of the trap.
    PAGEFAULT_RESULT_TRAP,
    // Indicate that the page-fault resulted in a failed mapping
    // and that it could not be fixed.
    PAGEFAULT_RESULT_FAULT,
};

/* DebugSingleStep
 * Handles the SingleStep trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
KERNELAPI oserr_t KERNELABI
DebugSingleStep(
    _In_ Context_t* Context);

/* DebugBreakpoint
 * Handles the Breakpoint trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
KERNELAPI oserr_t KERNELABI
DebugBreakpoint(
    _In_ Context_t* Context);

/**
 * @brief Handles page-fault and either validates or invalidates
 * that the address is valid.
 * @param context
 * @param address
 * @return
 */
KERNELAPI enum PageFaultResult KERNELABI
DebugPageFault(
    _In_ Context_t* context,
    _In_ uintptr_t  address);

/* DebugPanic
 * Kernel panic function - Call this to enter panic mode
 * and disrupt normal functioning. This function does not return again */
KERNELAPI oserr_t KERNELABI
DebugPanic(
    _In_ int         FatalityScope,
    _In_ Context_t*  Context,
    _In_ const char* Message, ...);

/* DebugStackTrace
 * Performs a verbose stack trace in the current context 
 * Goes back a maximum of <MaxFrames> in the stack */
KERNELAPI oserr_t KERNELABI
DebugStackTrace(
    _In_ Context_t* context,
    _In_ size_t     maxFrames);

/* DebugMemory 
 * Dumps memory in the form of <data> <string> at the
 * given address and length of memory dump */
KERNELAPI oserr_t KERNELABI
DebugMemory(
    _In_Opt_ const char* Description,
    _In_     void*       Address,
    _In_     size_t      Length);

#endif //!_DEBUG_H_
