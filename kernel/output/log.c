/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Logging Interface
 * - Contains the shared kernel log interface for logging-usage
 */

#include <arch/output.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/timer.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <irq_spinlock.h>
#include <log.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threading.h>

typedef struct SystemLogLine {
    int     level;
    UUId_t  coreId;
    UUId_t  threadHandle;
    clock_t timeStamp;
    char    data[128]; // Message
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

static char* g_typeNames[] = {
    "raw",
    "trace",
    "debug",
    "warn",
    "error"
};

static uint32_t g_typeColors[] = {
    0x111111,
    0x99E600,
    0x2ECC71,
    0x9B59B6,
    0xFF392B
};

static SystemLog_t g_kernelLog                      = { 0 };
static char        g_bootLogSpace[LOG_INITIAL_SIZE] = { 0 };

static inline void __WriteMessageToScreen(const char* message)
{
    int index = 0;
    while (message[index]) {
        VideoPutCharacter(message[index]);
        index++;
    }
}

static inline void __WriteMessageToSerial(const char* message)
{
    int index = 0;
    while (message[index]) {
        SerialPutCharacter(message[index]);
        index++;
    }
}

void
LogInitialize(void)
{
    // Setup initial log space
    IrqSpinlockConstruct(&g_kernelLog.SyncObject);
    g_kernelLog.StartOfData = (uintptr_t*)&g_bootLogSpace[0];
    g_kernelLog.DataSize    = LOG_INITIAL_SIZE;

    g_kernelLog.Lines         = (SystemLogLine_t*)&g_bootLogSpace[0];
    g_kernelLog.NumberOfLines = LOG_INITIAL_SIZE / sizeof(SystemLogLine_t);

    // Initialize the serial interface if any
#ifdef __OSCONFIG_HAS_UART
    OsStatus_t osStatus = SerialPortInitialize();
    if (osStatus != OsSuccess) {
        WARNING("LogInitialize failed to initialize serial output!");
    }
#endif
}

void
LogInitializeFull(void)
{
    void* upgradeBuffer;

    // Upgrade the buffer
    upgradeBuffer = kmalloc(LOG_PREFFERED_SIZE);
    assert(upgradeBuffer != NULL);

    memset(upgradeBuffer, 0, LOG_PREFFERED_SIZE);

	IrqSpinlockAcquire(&g_kernelLog.SyncObject);
    memcpy(upgradeBuffer, (const void*)g_kernelLog.StartOfData, g_kernelLog.DataSize);
    g_kernelLog.StartOfData   = (uintptr_t*)upgradeBuffer;
    g_kernelLog.DataSize      = LOG_PREFFERED_SIZE;
    g_kernelLog.Lines         = (SystemLogLine_t*)upgradeBuffer;
    g_kernelLog.NumberOfLines = LOG_PREFFERED_SIZE / sizeof(SystemLogLine_t);
	IrqSpinlockRelease(&g_kernelLog.SyncObject);
}

void
LogRenderMessages(void)
{
    SystemLogLine_t* logLine;
    Thread_t*        thread;
    char             sprintBuffer[256];

    while (g_kernelLog.RenderIndex != g_kernelLog.LineIndex) {

        // Get next line to be rendered
        logLine = &g_kernelLog.Lines[g_kernelLog.RenderIndex++];
        if (g_kernelLog.RenderIndex == g_kernelLog.NumberOfLines) {
            g_kernelLog.RenderIndex = 0;
        }
        thread = THREAD_GET(logLine->threadHandle);

        // Don't give raw any special handling
        if (logLine->level == LOG_RAW) {
            VideoGetTerminal()->FgColor = 0;
            __WriteMessageToSerial(&logLine->data[0]);
        }
        else {
            VideoGetTerminal()->FgColor = g_typeColors[logLine->level];
            snprintf(&sprintBuffer[0], sizeof(sprintBuffer) - 1,
                     "%09" PRIuIN " [%s-%u-%s] %s\n",
                     logLine->timeStamp,
                     g_typeNames[logLine->level],
                     logLine->coreId,
                     thread ? ThreadName(thread) : "boot",
                     &logLine->data[0]);
            __WriteMessageToSerial(&sprintBuffer[0]);
            if (logLine->level >= LOG_DEBUG && g_kernelLog.AllowRender) {
                __WriteMessageToScreen(&sprintBuffer[0]);
            }
        }
    }
}

void
LogSetRenderMode(
    _In_ int enable)
{
    // Update status, flush log
    g_kernelLog.AllowRender = enable;
    if (enable) {
	    IrqSpinlockAcquire(&g_kernelLog.SyncObject);
        LogRenderMessages();
	    IrqSpinlockRelease(&g_kernelLog.SyncObject);
    }
}

void
LogAppendMessage(
    _In_ int         level,
    _In_ const char* format,
    ...)
{
    SystemLogLine_t* logLine;
	va_list          arguments;
	UUId_t           coreId = ArchGetProcessorCoreId();

	if (!format) {
	    return;
	}

    // Get a new line object
	IrqSpinlockAcquire(&g_kernelLog.SyncObject);
	if ((g_kernelLog.LineIndex + 1) % g_kernelLog.NumberOfLines == g_kernelLog.RenderIndex) {
	    LogRenderMessages();
	}

    logLine = &g_kernelLog.Lines[g_kernelLog.LineIndex++];
    if (g_kernelLog.LineIndex == g_kernelLog.NumberOfLines) {
        g_kernelLog.LineIndex = 0;
    }
    
    memset((void*)logLine, 0, sizeof(SystemLogLine_t));
    logLine->level        = level;
    logLine->coreId       = coreId;
    logLine->threadHandle = ThreadCurrentHandle();
    SystemTimerGetTimestamp(&logLine->timeStamp);
    
	va_start(arguments, format);
    vsnprintf(&logLine->data[0], sizeof(logLine->data) - 1, format, arguments);
    va_end(arguments);

	LogRenderMessages();
	IrqSpinlockRelease(&g_kernelLog.SyncObject);
}
