/**
 * Copyright 2020, Philip Meulengracht
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
 * Process Manager - Debugger
 *  Contains the implementation of debugging facilities for the process manager which is
 *  invoked once a process crashes
 */

#define __TRACE

#include <ds/mstring.h>
#include <ddk/debug.h>
#include <ddk/utils.h>
#include <os/context.h>
#include <os/memory.h>
#include "pe.h"
#include "process.h"
#include "symbols.h"
#include <stdlib.h>

void
PmDebuggerInitialize(void)
{
    SymbolInitialize();
}

static oserr_t
__DoStackTrace(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ uuid_t                     threadHandle,
        _In_ uintptr_t                  stackAtCrash)
{
    oserr_t    oserr;
    void*      stack;
    void*      topOfStack;
    uintptr_t* stackAddress;
    uintptr_t* stackLimit;
    uintptr_t  moduleBase;
    mstring_t* moduleName;
    int        i = 0, max = 12;

    oserr = MapThreadMemoryRegion(
            threadHandle,
            stackAtCrash,
            &topOfStack,
            &stack
    );
    if (oserr != OS_EOK) {
        ERROR("__DoStackTrace failed to map thread stack: %u", oserr);
        return oserr;
    }

    // Traverse the memory region up to stack max
    stackAddress = (uintptr_t*)stack;
    stackLimit   = (uintptr_t*)topOfStack;
    ERROR("__DoStackTrace 0x%llx => 0x%llx", stackAddress, stackLimit);
    while (stackAddress < stackLimit && i < max) {
        uintptr_t stackValue = *stackAddress;
        oserr = PEImageLoadContextImageDetailsByAddress(
                loadContext,
                stackValue,
                &moduleBase,
                &moduleName
        );

        if (oserr == OS_EOK) {
            const char* symbolName;
            uintptr_t   symbolOffset;
            if (SymbolLookup(moduleName, stackValue - moduleBase, &symbolName, &symbolOffset) == OS_EOK) {
                ERROR("%i: %s+%x in module %ms", i, symbolName, symbolOffset, moduleName);
            } else {
                ERROR("%i: At offset 0x%" PRIxIN " in module %ms (0x%" PRIxIN ")",
                      i, stackValue - moduleBase, moduleName, stackValue);
            }
            i++;
        }
        stackAddress++;
    }
    ERROR("__DoStackTrace end of stack trace");
    return MemoryFree(stack, (uintptr_t)topOfStack - (uintptr_t)stack);
}

oserr_t
HandleProcessCrashReport(
        _In_ Process_t*       process,
        _In_ uuid_t           threadHandle,
        _In_ const Context_t* crashContext,
        _In_ int              crashReason)
{
    uintptr_t  moduleBase;
    mstring_t* moduleName;
    mstring_t* programName;
    uintptr_t  crashAddress;
    oserr_t    oserr;
    TRACE("HandleProcessCrashReport(%i)", crashReason);

    if (!crashContext) {
        return OS_EINVALPARAMS;
    }

    crashAddress = CONTEXT_IP(crashContext);
    programName  = process->load_context->RootModule;

    oserr = PEImageLoadContextImageDetailsByAddress(
            process->load_context,
            crashAddress,
            &moduleBase,
            &moduleName
    );
    if (oserr != OS_EOK) {
        ERROR("%ms: Crashed at address 0x%" PRIxIN " with reason %i  [lookup failed]",
              programName, crashAddress, crashReason);
        return oserr;
    }
    ERROR("%ms: Crashed in module %ms, at offset 0x%" PRIxIN " (0x%" PRIxIN ") with reason %i",
          programName, moduleName, crashAddress - moduleBase, crashAddress, crashReason);

    // Verify the stack pointer of the context
    if (!CONTEXT_USERSP(crashContext)) {
        ERROR("HandleProcessCrashReport failed to load user stack value: 0x%" PRIxIN, CONTEXT_USERSP(crashContext));
        return OS_EUNKNOWN;
    }

    oserr = __DoStackTrace(process->load_context, threadHandle, CONTEXT_USERSP(crashContext));
    return oserr;
}
