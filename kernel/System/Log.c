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

/* Includes */
#include <Devices/Video.h>
#include <Log.h>

/* CLib */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Globals */
LogTarget_t GlbLogTarget = LogMemory;
LogLevel_t GlbLogLevel = LogLevel1;
char GlbLog[LOG_INITIAL_SIZE];
int GlbLogIndex = 0;

/* Externs */
extern MCoreVideoDevice_t BootVideo;

/* Instantiates the Log */
void LogInit(LogTarget_t Output, LogLevel_t Level)
{
	/* Save */
	GlbLogTarget = Output;
	GlbLogLevel = Level;

	/* Clear out log */
	memset(GlbLog, 0, LOG_INITIAL_SIZE);
	GlbLogIndex = 0;
}

/* Switches target */
void LogRedirect(LogTarget_t Output)
{
	/* Ignore if already */
	if (GlbLogTarget == Output)
		return;

	/* Update target */
	GlbLogTarget = Output;

	/* If we redirect to anything else than
	 * memory, flush the log */
	if (Output != LogMemory)
		LogFlush(Output);
}

/* Flushes the log */
void LogFlush(LogTarget_t Output)
{
	_CRT_UNUSED(Output);
}

/* Internal Log Print */
void LogInternalPrint(int LogType, const char *Header, const char *Message)
{
	/* Log it into memory - if we have room */
	if (GlbLogIndex + strlen(Message) < LOG_INITIAL_SIZE)
	{
		/* Write header */
		GlbLog[GlbLogIndex] = (char)LogType;
		GlbLog[GlbLogIndex + 1] = (char)strlen(Message);

		/* Increase */
		GlbLogIndex += 2;

		if (LogType != LOG_TYPE_RAW)
		{
			/* Add System */
			memcpy(&GlbLog[GlbLogIndex], Header, strlen(Header));
			GlbLogIndex += strlen(Header);

			/* Add a space */
			GlbLog[GlbLogIndex] = ' ';
			GlbLogIndex++;
		}

		/* Add it */
		memcpy(&GlbLog[GlbLogIndex], Message, strlen(Message));
		GlbLogIndex += strlen(Message);

		if (LogType != LOG_TYPE_RAW)
		{
			/* Add a newline */
			GlbLog[GlbLogIndex] = '\n';
			GlbLogIndex++;
		}
	}

	/* Print it */
	if (GlbLogTarget == LogConsole) 
	{
		/* Header first */
		if (LogType != LOG_TYPE_RAW)
		{
			/* Select Color */
			if (LogType == LOG_TYPE_INFORMATION)
				BootVideo.FgColor = LOG_COLOR_INFORMATION;
			else if (LogType == LOG_TYPE_DEBUG)
				BootVideo.FgColor = LOG_COLOR_DEBUG;
			else if (LogType == LOG_TYPE_FATAL)
				BootVideo.FgColor = LOG_COLOR_ERROR;

			/* Print */
			printf("[%s] ", Header);
		}

		/* Sanity */
		if (LogType != LOG_TYPE_FATAL)
			BootVideo.FgColor = LOG_COLOR_DEFAULT;

		/* Print */
		if (LogType == LOG_TYPE_RAW)
			printf("%s", Message);
		else
			printf("%s\n", Message);

		/* Restore */
		BootVideo.FgColor = LOG_COLOR_DEFAULT;
	}
	else if (GlbLogTarget == LogFile) {

	}
}

/* Raw Log */
void Log(const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Append newline */
	strcat(oBuffer, "\n");

	/* Print */
	LogInternalPrint(LOG_TYPE_RAW, NULL, oBuffer);
}

/* Raw Log */
void LogRaw(const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print */
	LogInternalPrint(LOG_TYPE_RAW, NULL, oBuffer);
}

/* Output information to log */
void LogInformation(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print */
	LogInternalPrint(LOG_TYPE_INFORMATION, System, oBuffer);
}

/* Output debug to log */
void LogDebug(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print */
	LogInternalPrint(LOG_TYPE_DEBUG, System, oBuffer);
}

/* Output Error to log */
void LogFatal(const char *System, const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print */
	LogInternalPrint(LOG_TYPE_FATAL, System, oBuffer);
}