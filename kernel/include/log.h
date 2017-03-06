/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Logging System
*/
#ifndef _MCORE_LOG_H_
#define _MCORE_LOG_H_

/* Includes */
#include <os/osdefs.h>

/* Definitions */
typedef enum _LogTarget
{
	LogMemory,
	LogConsole,
	LogFile
} LogTarget_t;

typedef enum _LogLevel
{
	LogLevel1,
	LogLevel2,
	LogLevel3
} LogLevel_t;

/* Colors */
#define LOG_COLOR_INFORMATION		0x2ECC71
#define LOG_COLOR_DEBUG				0x9B59B6
#define LOG_COLOR_ERROR				0xFF392B
#define LOG_COLOR_DEFAULT			0x0

/* Default size to 4kb */
#define LOG_INITIAL_SIZE			(1024 * 4)
#define LOG_PREFFERED_SIZE			(1024 * 65)

/* Log Types */
#define LOG_TYPE_RAW				0x00
#define LOG_TYPE_INFORMATION		0x01
#define LOG_TYPE_DEBUG				0x02
#define LOG_TYPE_FATAL				0x03

/* Functions */
KERNELAPI void LogInit(void);
KERNELAPI void LogUpgrade(size_t Size);
KERNELAPI void LogRedirect(LogTarget_t Output);
KERNELAPI void LogFlush(LogTarget_t Output);

/* The log functions */
KERNELAPI void Log(__CONST char *Message, ...);
KERNELAPI void LogRaw(__CONST char *Message, ...);
KERNELAPI void LogInformation(__CONST char *System, __CONST char *Message, ...);
KERNELAPI void LogDebug(__CONST char *System, __CONST char *Message, ...);
KERNELAPI void LogFatal(__CONST char *System, __CONST char *Message, ...);

#endif
