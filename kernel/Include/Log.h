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
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
typedef enum _LogTarget
{
	LogConsole
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

/* Functions */
_CRT_EXTERN void LogInit(LogTarget_t Output, LogLevel_t Level);

/* The log functions */
_CRT_EXPORT void Log(const char *Message, ...);
_CRT_EXPORT void LogInformation(const char *System, const char *Message, ...);
_CRT_EXPORT void LogDebug(const char *System, const char *Message, ...);
_CRT_EXTERN void LogModule(const char *Module, const char *Message, ...);
_CRT_EXPORT void LogFatal(const char *System, const char *Message, ...);

#endif