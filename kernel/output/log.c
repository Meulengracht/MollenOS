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
 */

#include <arch/output.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/timer.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <os/types/syscall.h>
#include <spinlock.h>
#include <log.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threading.h>

typedef struct SystemLog {
    uintptr_t*          StartOfData;
    size_t              DataSize;
    int                 NumberOfLines;
    OSKernelLogEntry_t* Lines;
    Spinlock_t          SyncObject;
    
    int LineIndex;
    int MigrationIndex;
    int RenderIndex;
    int AllowRender;
} SystemLog_t;

static char* g_typeNames[] = {
    "trace",
    "debug",
    "warn",
    "error"
};

static uint32_t g_typeColors[] = {
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
    SpinlockConstruct(&g_kernelLog.SyncObject);
    g_kernelLog.StartOfData = (uintptr_t*)&g_bootLogSpace[0];
    g_kernelLog.DataSize    = LOG_INITIAL_SIZE;

    g_kernelLog.Lines         = (OSKernelLogEntry_t*)&g_bootLogSpace[0];
    g_kernelLog.NumberOfLines = LOG_INITIAL_SIZE / sizeof(OSKernelLogEntry_t);

    // Initialize the serial interface if any
#ifdef __OSCONFIG_HAS_UART
    oserr_t osStatus = SerialPortInitialize();
    if (osStatus != OS_EOK) {
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

    SpinlockAcquireIrq(&g_kernelLog.SyncObject);
    memcpy(upgradeBuffer, (const void*)g_kernelLog.StartOfData, g_kernelLog.DataSize);
    g_kernelLog.StartOfData   = (uintptr_t*)upgradeBuffer;
    g_kernelLog.DataSize      = LOG_PREFFERED_SIZE;
    g_kernelLog.Lines         = (OSKernelLogEntry_t*)upgradeBuffer;
    g_kernelLog.NumberOfLines = LOG_PREFFERED_SIZE / sizeof(OSKernelLogEntry_t);
    SpinlockReleaseIrq(&g_kernelLog.SyncObject);
}

oserr_t
LogMigrate(
        _In_ void*   buffer,
        _In_ size_t  bufferSize,
        _In_ size_t* bytesRead)
{
    int lines = (int)(bufferSize / sizeof(OSKernelLogEntry_t));
    if (buffer == NULL || lines == 0) {
        return OS_EINVALPARAMS;
    }

    // Once this is called, we disable logging.
    SpinlockAcquireIrq(&g_kernelLog.SyncObject);

    // Clamp the number of lines we are going to read
    if (lines > (g_kernelLog.NumberOfLines - g_kernelLog.MigrationIndex)) {
        lines = g_kernelLog.NumberOfLines - g_kernelLog.MigrationIndex;
        if (lines == 0) {
            SpinlockReleaseIrq(&g_kernelLog.SyncObject);
            // Migration done
            *bytesRead = 0;
            return OS_EOK;
        }
    }

    // Migrate those lines
    memcpy(
            buffer,
            &g_kernelLog.Lines[g_kernelLog.MigrationIndex],
            sizeof(OSKernelLogEntry_t) * lines
    );
    g_kernelLog.MigrationIndex += lines;
    SpinlockReleaseIrq(&g_kernelLog.SyncObject);
    *bytesRead = sizeof(OSKernelLogEntry_t) * lines;
    return OS_EOK;
}

void
LogRenderMessages(void)
{
    OSKernelLogEntry_t* logLine;
    Thread_t*           thread;
    char                sprintBuffer[256];
    int                 written;

    while (g_kernelLog.RenderIndex != g_kernelLog.LineIndex) {

        // Get next line to be rendered
        logLine = &g_kernelLog.Lines[g_kernelLog.RenderIndex++];
        if (g_kernelLog.RenderIndex == g_kernelLog.NumberOfLines) {
            g_kernelLog.RenderIndex = 0;
        }
        thread = THREAD_GET(logLine->ThreadID);

        VideoGetTerminal()->FgColor = g_typeColors[(int)logLine->Level];
        written = snprintf(&sprintBuffer[0], sizeof(sprintBuffer) - 1,
                           "%09llu [%s-%u-%s] %s\n",
                           logLine->Timestamp,
                           g_typeNames[(int)logLine->Level],
                           logLine->CoreID,
                           thread ? ThreadName(thread) : "boot",
                           &logLine->Message[0]
        );
        sprintBuffer[written] = '\0';
        __WriteMessageToSerial(&sprintBuffer[0]);
        if ((int)logLine->Level >= (int)OSSYSLOGLEVEL_DEBUG && g_kernelLog.AllowRender) {
            __WriteMessageToScreen(&sprintBuffer[0]);
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
        SpinlockAcquireIrq(&g_kernelLog.SyncObject);
        LogRenderMessages();
        SpinlockReleaseIrq(&g_kernelLog.SyncObject);
    }
}

void
LogAppendMessage(
        _In_ enum OSSysLogLevel level,
        _In_ const char*        format, ...)
{
    OSKernelLogEntry_t* logLine;
	va_list             arguments;
	uuid_t              coreId = ArchGetProcessorCoreId();
    int                 written;

	if (!format) {
	    return;
	}

    // Get a new line object
    SpinlockAcquireIrq(&g_kernelLog.SyncObject);
	if ((g_kernelLog.LineIndex + 1) % g_kernelLog.NumberOfLines == g_kernelLog.RenderIndex) {
	    LogRenderMessages();
	}

    logLine = &g_kernelLog.Lines[g_kernelLog.LineIndex++];
    if (g_kernelLog.LineIndex == g_kernelLog.NumberOfLines) {
        g_kernelLog.LineIndex = 0;
    }
    
    memset((void*)logLine, 0, sizeof(OSKernelLogEntry_t));
    logLine->Level    = level;
    logLine->CoreID   = coreId;
    logLine->ThreadID = ThreadCurrentHandle();
    SystemTimerGetTimestamp(&logLine->Timestamp);
    logLine->Timestamp /= NSEC_PER_MSEC;
    
	va_start(arguments, format);
    written = vsnprintf(
            &logLine->Message[0],
            sizeof(logLine->Message) - 1,
            format,
            arguments
    );
    va_end(arguments);
    logLine->Message[written] = '\0';

	LogRenderMessages();
    SpinlockReleaseIrq(&g_kernelLog.SyncObject);
}
