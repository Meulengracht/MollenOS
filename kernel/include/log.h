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

#ifndef __LOGGING_INTERFACE__
#define __LOGGING_INTERFACE__

#include <os/osdefs.h>
#include <ds/ds.h>

#define LOG_INITIAL_SIZE        (1024 * 4)
#define LOG_PREFFERED_SIZE      (1024 * 65)

typedef enum _SystemLogType {
    LogTrace   = 0x99E600,
    LogRaw     = 0x111111,
    LogPipe    = 0xEE2244,
    LogDebug   = 0x2ECC71,
    LogWarning = 0x9B59B6,
    LogError   = 0xFF392B
} SystemLogType_t;

typedef struct _SystemLogLine {
    SystemLogType_t Type;
    char            System[10]; // [TYPE  ]
    char            Data[118]; // Message
} SystemLogLine_t;

typedef struct _SystemLog {
    uintptr_t*       StartOfData;
    size_t           DataSize;
    int              NumberOfLines;
    SystemLogLine_t* Lines;
    SafeMemoryLock_t SyncObject;
    
    int LineIndex;
    int RenderIndex;
    int AllowRender;
} SystemLog_t;

/* LogInitialize
 * Initializes loggin data-structures and global variables
 * by setting everything to sane value */
KERNELAPI void KERNELABI
LogInitialize(void);

/* LogInitializeFull
 * Upgrades the log to a larger buffer, initializing pipes and installs the message thread. */
KERNELAPI void KERNELABI
LogInitializeFull(void);

/* LogSetRenderMode
 * Enables or disables the log from rendering to the screen. This can be used at start to 
 * indicate when rendering is available, and at end to disable kernel from modifying screen. */
KERNELAPI void KERNELABI
LogSetRenderMode(
    _In_ int Enable);

/* LogAppendMessage
 * Appends a new message of the given parameters to the global log object. If the buffer
 * reaches the end wrap-around will happen. */
KERNELAPI void KERNELABI
LogAppendMessage(
    _In_ SystemLogType_t Type,
    _In_ const char*     Header,
    _In_ const char*     Message,
    ...);

#endif // !__LOGGING_INTERFACE__
