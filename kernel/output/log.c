/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Logging Interface
 * - Contains the shared kernel log interface for logging-usage
 */

#include <arch/output.h>
#include <arch/utils.h>
#include <assert.h>
#include <handle.h>
#include <heap.h>
#include <irq_spinlock.h>
#include <log.h>
#include <machine.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threading.h>

typedef struct SystemLogLine {
    SystemLogType_t Type;
    UUId_t          CoreId;
    UUId_t          ThreadHandle;
    char            Data[128]; // Message
} SystemLogLine_t;

typedef struct SystemLog {
    uintptr_t*       StartOfData;
    size_t           DataSize;
    int              NumberOfLines;
    SystemLogLine_t* Lines;
    IrqSpinlock_t    SyncObject;
    
    int LineIndex;
    int RenderIndex;
    int AllowRender;
} SystemLog_t;

static SystemLog_t LogObject                 = { 0 };
static char StaticLogSpace[LOG_INITIAL_SIZE] = { 0 };

void
LogInitialize(void)
{
    // Setup initial log space
    LogObject.StartOfData   = (uintptr_t*)&StaticLogSpace[0];
    LogObject.DataSize      = LOG_INITIAL_SIZE;
    
    LogObject.Lines         = (SystemLogLine_t*)&StaticLogSpace[0];
    LogObject.NumberOfLines = LOG_INITIAL_SIZE / sizeof(SystemLogLine_t);
}

void
LogInitializeFull(void)
{
    void* UpgradeBuffer;

    // Upgrade the buffer
    UpgradeBuffer = kmalloc(LOG_PREFFERED_SIZE);
    memset(UpgradeBuffer, 0, LOG_PREFFERED_SIZE);

	IrqSpinlockAcquire(&LogObject.SyncObject);
    memcpy(UpgradeBuffer, (const void*)LogObject.StartOfData, LogObject.DataSize);
    LogObject.StartOfData   = (uintptr_t*)UpgradeBuffer;
    LogObject.DataSize      = LOG_PREFFERED_SIZE;
    LogObject.Lines         = (SystemLogLine_t*)UpgradeBuffer;
    LogObject.NumberOfLines = LOG_PREFFERED_SIZE / sizeof(SystemLogLine_t);
	IrqSpinlockRelease(&LogObject.SyncObject);
}

void
LogRenderMessages(void)
{
    SystemLogLine_t* Line;
    MCoreThread_t*   Thread;
    
    if (!LogObject.AllowRender) {
        return;
    }

	IrqSpinlockAcquire(&LogObject.SyncObject);
    while (LogObject.RenderIndex != LogObject.LineIndex) {

        // Get next line to be rendered
        Line = &LogObject.Lines[LogObject.RenderIndex++];
        if (LogObject.RenderIndex == LogObject.NumberOfLines) {
            LogObject.RenderIndex = 0;
        }
        Thread = LookupHandleOfType(Line->ThreadHandle, HandleTypeThread);

        // Don't give raw any special handling
        if (Line->Type == LogRaw) {
            VideoGetTerminal()->FgColor = 0;
            printf("%s", &Line->Data[0]);
        }
        else {
            VideoGetTerminal()->FgColor = (uint32_t)Line->Type;
            printf("[%u-%s] ", Line->CoreId, Thread ? Thread->Name : "boot");
            if (Line->Type != LogError) {
                VideoGetTerminal()->FgColor = 0;
            }
            printf("%s\n", &Line->Data[0]);
        }
    }
	IrqSpinlockRelease(&LogObject.SyncObject);
}

void
LogSetRenderMode(
    _In_ int Enable)
{
    // Update status, flush log
    LogObject.AllowRender = Enable;
    if (Enable) {
        LogRenderMessages();
    }
}

void
LogAppendMessage(
    _In_ SystemLogType_t Type,
    _In_ const char*     Header,
    _In_ const char*     Message,
    ...)
{
    SystemLogLine_t* Line;
	va_list          Arguments;
	UUId_t           CoreId = ArchGetProcessorCoreId();

    assert(Header != NULL);
    assert(Message != NULL);
    
    // Get a new line object
	IrqSpinlockAcquire(&LogObject.SyncObject);
	if ((LogObject.LineIndex + 1) % LogObject.NumberOfLines == LogObject.RenderIndex) {
	    IrqSpinlockRelease(&LogObject.SyncObject);
	    LogRenderMessages();
	    IrqSpinlockAcquire(&LogObject.SyncObject);
	}
	
    Line = &LogObject.Lines[LogObject.LineIndex++];
    if (LogObject.LineIndex == LogObject.NumberOfLines) {
        LogObject.LineIndex = 0;
    }
    
    memset((void*)Line, 0, sizeof(SystemLogLine_t));
    Line->Type         = Type;
    Line->CoreId       = CoreId;
    Line->ThreadHandle = GetCurrentThreadId();
    
	va_start(Arguments, Message);
    vsnprintf(&Line->Data[0], sizeof(Line->Data) - 1, Message, Arguments);
    va_end(Arguments);
	IrqSpinlockRelease(&LogObject.SyncObject);
	LogRenderMessages();
}
