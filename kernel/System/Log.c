/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Includes */
#include <Devices/Video.h>
#include <Log.h>

/* CLib */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Globals */
LogTarget_t GlbLogTarget = LogConsole;
LogLevel_t GlbLogLevel = LogLevel1;

/* Externs */
extern MCoreVideoDevice_t BootVideo;

/* Instantiates the Log */
void LogInit(LogTarget_t Output, LogLevel_t Level)
{
	/* Save */
	GlbLogTarget = Output;
	GlbLogLevel = Level;
}

/* Raw Log */
void Log(const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[512];
	va_list ArgList;

	/* Memset buffer */
	memset(oBuffer, 0, sizeof(oBuffer));

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print it */
	BootVideo.FgColor = LOG_COLOR_DEFAULT;
	printf("%s\n", oBuffer);
}

/* Output information to log */
void LogInformation(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[512];
	va_list ArgList;

	/* Memset buffer */
	memset(oBuffer, 0, sizeof(oBuffer));

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print System */
	BootVideo.FgColor = LOG_COLOR_INFORMATION;
	printf("[%s] ", System);
	BootVideo.FgColor = LOG_COLOR_DEFAULT;
	printf("%s\n", oBuffer);
}

/* Output debug to log */
void LogDebug(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[512];
	va_list ArgList;

	/* Memset buffer */
	memset(oBuffer, 0, sizeof(oBuffer));

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print System */
	BootVideo.FgColor = LOG_COLOR_DEBUG;
	printf("[%s] ", System);
	BootVideo.FgColor = LOG_COLOR_DEFAULT;
	printf("%s\n", oBuffer);
}

/* Output Error to log */
void LogFatal(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[512];
	va_list ArgList;

	/* Memset buffer */
	memset(oBuffer, 0, sizeof(oBuffer));

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print System */
	BootVideo.FgColor = LOG_COLOR_ERROR;
	printf("[%s] ", System);
	printf("%s\n", oBuffer);
	BootVideo.FgColor = LOG_COLOR_DEFAULT;
}