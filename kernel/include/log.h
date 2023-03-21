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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Logging Interface
 * - Contains the shared kernel log interface for logging-usage
 */

#ifndef __LOGGING_INTERFACE__
#define __LOGGING_INTERFACE__

#include <os/osdefs.h>

#define LOG_INITIAL_SIZE   (1024 * 4)
#define LOG_PREFFERED_SIZE (1024 * 65)

/**
 * @brief Initializes the logging system. This will setup a temporary buffer
 * to log into, until LogInitializeFull is called. This can be called before
 * memory systems are initialized.
 */
KERNELAPI void KERNELABI
LogInitialize(void);

/* LogInitializeFull
 * Upgrades the log to a larger buffer, initializing pipes and installs the message thread. */
KERNELAPI void KERNELABI
LogInitializeFull(void);

/**
 * @brief
 * @param buffer
 * @param bufferSize
 * @param bytesRead
 * @return
 */
KERNELAPI oserr_t KERNELABI
LogMigrate(
        _In_ void*   buffer,
        _In_ size_t  bufferSize,
        _In_ size_t* bytesRead);

/* LogSetRenderMode
 * Enables or disables the log from rendering to the screen. This can be used at start to 
 * indicate when rendering is available, and at end to disable kernel from modifying screen. */
KERNELAPI void KERNELABI
LogSetRenderMode(
    _In_ int enable);

/* LogAppendMessage
 * Appends a new message of the given parameters to the global log object. If the buffer
 * reaches the end wrap-around will happen. */
KERNELAPI void KERNELABI
LogAppendMessage(
        _In_ enum OSSysLogLevel level,
        _In_ const char*        format, ...);

#endif // !__LOGGING_INTERFACE__
