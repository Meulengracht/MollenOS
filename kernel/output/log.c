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
 * MollenOS Log Interface
 * - Contains the shared kernel log interface for logging-usage
 */

#include <system/video.h>
#include <threading.h>
#include <heap.h>
#include <log.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Globals */
static MCoreLog_t LogObject                     = { 0 };
static char StaticLogSpace[LOG_INITIAL_SIZE]    = { 0 };
static UUId_t PipeThreads[2]                    = { 0 };

/* LogPipeHandler
 * The handler function that will get spawned twice to listen for new messages
 * on the debug-pipes. */
void
LogPipeHandler(
    _In_ void *PipeInstance)
{
    // Variables
    SystemPipe_t *Pipe = (SystemPipe_t*)PipeInstance;
    char MessageBuffer[256];
    int i;

    // Listen forever
    while (1) {
        // Reset buffer
        memset((void*)MessageBuffer, 0, sizeof(MessageBuffer));
        i = 0;

        // Read untill newline
        while (1) {
            ReadSystemPipe(Pipe, (uint8_t*)&MessageBuffer[i], 1);
            if (MessageBuffer[i] == '\n') {
                MessageBuffer[i] = '\0'; // Skip newlines, automatically added
                break;
            }
            i++;
        }
        LogAppendMessage(LogPipe, "PIPE", (const char*)&MessageBuffer[0]);
    }
}

/* LogInitialize
 * Initializes loggin data-structures and global variables
 * by setting everything to sane value */
void
LogInitialize(void)
{
    // Setup initial log space
    LogObject.StartOfData   = (uintptr_t*)&StaticLogSpace[0];
    LogObject.DataSize      = LOG_INITIAL_SIZE;
    
    LogObject.Lines         = (MCoreLogLine_t*)&StaticLogSpace[0];
    LogObject.NumberOfLines = LOG_INITIAL_SIZE / sizeof(MCoreLogLine_t);
	CriticalSectionConstruct(&LogObject.SyncObject, CRITICALSECTION_PLAIN);
}

/* LogInitializeFull
 * Upgrades the log to a larger buffer, initializing pipes and installs the message thread. */
void
LogInitializeFull(void)
{
    // Variables
    void *UpgradeBuffer = NULL;

    // Upgrade the buffer
    UpgradeBuffer = kmalloc(LOG_PREFFERED_SIZE);
    memset(UpgradeBuffer, 0, LOG_PREFFERED_SIZE);

	CriticalSectionEnter(&LogObject.SyncObject);
    memcpy(UpgradeBuffer, (const void*)LogObject.StartOfData, LogObject.DataSize);
    LogObject.StartOfData   = (uintptr_t*)UpgradeBuffer;
    LogObject.DataSize      = LOG_PREFFERED_SIZE;

    LogObject.Lines         = (MCoreLogLine_t*)UpgradeBuffer;
    LogObject.NumberOfLines = LOG_PREFFERED_SIZE / sizeof(MCoreLogLine_t);
	CriticalSectionLeave(&LogObject.SyncObject);

    // Create 4kb pipes
    LogObject.STDOUT = CreateSystemPipe(0, 6); // 1 << 6, 64 entries, 1 << 12 is 4kb
    LogObject.STDERR = CreateSystemPipe(0, 6); // 1 << 6, 64 entries, 1 << 12 is 4kb

    // Create the threads that will echo the pipes
    PipeThreads[0] = ThreadingCreateThread("log-stdout", LogPipeHandler, (void*)LogObject.STDOUT, 0);
    PipeThreads[1] = ThreadingCreateThread("log-stderr", LogPipeHandler, (void*)LogObject.STDERR, 0);
}

/* LogRenderMessages
 * Makes sure RenderIndex catches up to the LineIndex by rendering all unrendered messages
 * to the screen. */
void
LogRenderMessages(void)
{
    // Variables
    MCoreLogLine_t *Line = NULL;

	CriticalSectionEnter(&LogObject.SyncObject);
    while (LogObject.RenderIndex != LogObject.LineIndex) {

        // Get next line to be rendered
        Line = &LogObject.Lines[LogObject.RenderIndex++];
        if (LogObject.RenderIndex == LogObject.NumberOfLines) {
            LogObject.RenderIndex = 0;
        }

        // Don't give raw any special handling
        if (Line->Type == LogRaw) {
            VideoGetTerminal()->FgColor = 0;
            printf("%s", &Line->Data[0]);
        }
        else {
            VideoGetTerminal()->FgColor = (uint32_t)Line->Type;
            printf("%s", &Line->System[0]);
            if (Line->Type != LogError) {
                VideoGetTerminal()->FgColor = 0;
            }
            printf("%s\n", &Line->Data[0]);
        }
    }
	CriticalSectionLeave(&LogObject.SyncObject);
}

/* LogSetRenderMode
 * Enables or disables the log from rendering to the screen. This can be used at start to 
 * indicate when rendering is available, and at end to disable kernel from modifying screen. */
void
LogSetRenderMode(
    _In_ int            Enable)
{
    // Update status, flush log
    LogObject.AllowRender = Enable;
    if (Enable) {
        LogRenderMessages();
    }
}

/* LogAppendMessage
 * Appends a new message of the given parameters to the global log object. If the buffer
 * reaches the end wrap-around will happen. */
void
LogAppendMessage(
    _In_ MCoreLogType_t Type,
    _In_ const char*    Header,
    _In_ const char*    Message,
    ...)
{
    // Variables
    MCoreLogLine_t *Line = NULL;
	va_list Arguments;

    // Sanitize
    assert(Header != NULL);
    assert(Message != NULL);

    // Get a new line object
	CriticalSectionEnter(&LogObject.SyncObject);
    Line = &LogObject.Lines[LogObject.LineIndex++];
    if (LogObject.LineIndex == LogObject.NumberOfLines) {
        LogObject.LineIndex = 0;
    }
	CriticalSectionLeave(&LogObject.SyncObject);
    memset((void*)Line, 0, sizeof(MCoreLogLine_t));
    Line->Type = Type;
    snprintf(&Line->System[0], sizeof(Line->System), "[%s] ", Header);
    
	va_start(Arguments, Message);
    vsnprintf(&Line->Data[0], sizeof(Line->Data) - 1, Message, Arguments);
    va_end(Arguments);

    // Render messages
    if (LogObject.AllowRender) {
        LogRenderMessages();
    }
}

/* LogPipeStdout
 * The log pipe for stdout when no windowing system is running. */
SystemPipe_t *LogPipeStdout(void) {
    return LogObject.STDOUT;
}

/* LogPipeStderr
 * The log pipe for stderr when no windowing system is running. */
SystemPipe_t *LogPipeStderr(void) {
    return LogObject.STDERR;
}
