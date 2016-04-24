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
#include <Heap.h>
#include <Log.h>
#include <Vfs/Vfs.h>

/* CLib */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Globals */
char GlbLogStatic[LOG_INITIAL_SIZE];
LogTarget_t GlbLogTarget = LogMemory;
LogLevel_t GlbLogLevel = LogLevel1;
size_t GlbLogSize = 0;
Spinlock_t GlbLogLock;
char *GlbLog = NULL;
int GlbLogIndex = 0;

/* Externs */
extern MCoreVideoDevice_t GlbBootVideo;

/* Instantiates the Log
 * with default params */
void LogInit(void)
{
	/* Save */
	GlbLogTarget = LogMemory;
	GlbLogLevel = LogLevel1;

	/* Set log ptr to initial */
	GlbLog = &GlbLogStatic[0];
	GlbLogSize = LOG_INITIAL_SIZE;

	/* Clear out log */
	memset(GlbLog, 0, GlbLogSize);
	GlbLogIndex = 0;

	/* Initialize Lock */
	SpinlockReset(&GlbLogLock);
}

/* Upgrades the log 
 * with a larger buffer */
void LogUpgrade(size_t Size)
{
	/* Allocate */
	char *nBuffer = (char*)kmalloc(Size);

	/* Zero it */
	memset(nBuffer, 0, Size);

	/* Copy current buffer */
	memcpy(nBuffer, GlbLog, GlbLogIndex);

	/* Free the old if not initial */
	if (GlbLog != &GlbLogStatic[0])
		kfree(GlbLog);

	/* Update */
	GlbLog = nBuffer;
	GlbLogSize = Size;
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
	/* Valid flush targets are:
	 * Console
	 * File */
	char TempBuffer[256];

	if (Output == LogConsole)
	{
		/* Vars */
		int Index = 0;

		/* Iterate */
		while (Index < GlbLogIndex)
		{
			/* Get header information */
			char Type = GlbLog[Index];
			char Length = GlbLog[Index + 1];

			/* Zero buffer */
			memset(TempBuffer, 0, 256);

			/* What kind of line is this? */
			if (Type == LOG_TYPE_RAW)
			{
				/* Copy data */
				memcpy(TempBuffer, &GlbLog[Index + 2], (size_t)Length);

				/* Flush it */
				GlbBootVideo.FgColor = LOG_COLOR_DEFAULT;
				printf("%s", TempBuffer);

				/* Increase */
				Index += 2 + Length;
			}
			else 
			{
				/* We have two chunks to print */
				char *StartPtr = &GlbLog[Index + 2];
				char *StartMsgPtr = strchr(StartPtr, ' ');
				int HeaderLen = (int)StartMsgPtr - (int)StartPtr;

				/* Copy */
				memcpy(TempBuffer, StartPtr, HeaderLen);

				/* Select Color */
				if (Type == LOG_TYPE_INFORMATION)
					GlbBootVideo.FgColor = LOG_COLOR_INFORMATION;
				else if (Type == LOG_TYPE_DEBUG)
					GlbBootVideo.FgColor = LOG_COLOR_DEBUG;
				else if (Type == LOG_TYPE_FATAL)
					GlbBootVideo.FgColor = LOG_COLOR_ERROR;

				/* Print header */
				printf("[%s] ", TempBuffer);

				/* Clear */
				memset(TempBuffer, 0, HeaderLen + 1);

				/* Increament */
				Index += 2 + HeaderLen + 1;

				/* Copy data */
				memcpy(TempBuffer, &GlbLog[Index], (size_t)Length);

				/* Sanity */
				if (Type != LOG_TYPE_FATAL)
					GlbBootVideo.FgColor = LOG_COLOR_DEFAULT;

				/* Finally, flush */
				printf("%s", TempBuffer);

				/* Restore */
				GlbBootVideo.FgColor = LOG_COLOR_DEFAULT;

				/* Increase again */
				Index += Length;
			}
		}
	}
	else if (Output == LogFile)
	{
		/* Temporary set to console */
		GlbLogTarget = LogConsole;

		/* Open log file */
		int Index = 0;
		MCoreFile_t *LogFileHandle = VfsOpen(FILESYSTEM_IDENT_SYS ":/System/Log.txt",
			Read | Write | TruncateIfExists | CreateIfNotExists);

		/* Sanity */
		if (LogFileHandle->Code != VfsOk) {
			VfsClose(LogFileHandle);
			LogFatal("SYST", "Failed to open/create system logfile: %u", 
				(size_t)LogFileHandle->Code);
			return;
		}

		/* Iterate */
		while (Index < GlbLogIndex)
		{
			/* Get header information */
			char Type = GlbLog[Index];
			char Length = GlbLog[Index + 1];

			/* Zero buffer */
			memset(TempBuffer, 0, 256);

			/* What kind of line is this? */
			if (Type == LOG_TYPE_RAW)
			{
				/* Copy data */
				memcpy(TempBuffer, &GlbLog[Index + 2], (size_t)Length);

				/* Write it to file */
				VfsWrite(LogFileHandle, (uint8_t*)TempBuffer, Length);

				/* Increase */
				Index += 2 + Length;
			}
			else
			{
				/* We have two chunks to print */
				char HeaderBuffer[16];
				char *StartPtr = &GlbLog[Index + 2];
				char *StartMsgPtr = strchr(StartPtr, ' ');
				int HeaderLen = (int)StartMsgPtr - (int)StartPtr;

				/* Reset header buffer */
				memset(HeaderBuffer, 0, 16);

				/* Copy */
				memcpy(TempBuffer, StartPtr, HeaderLen);

				/* Format header */
				sprintf(HeaderBuffer, "[%s] ", TempBuffer);
				
				/* Write it to file */
				VfsWrite(LogFileHandle, (uint8_t*)TempBuffer, HeaderLen + 3);

				/* Clear */
				memset(TempBuffer, 0, HeaderLen + 1);

				/* Increament */
				Index += 2 + HeaderLen + 1;

				/* Copy data */
				memcpy(TempBuffer, &GlbLog[Index], (size_t)Length);

				/* Write it to file */
				VfsWrite(LogFileHandle, (uint8_t*)TempBuffer, Length);

				/* Increase again */
				Index += Length;
			}
		}

		/* Done, close file */
		VfsClose(LogFileHandle);

		/* NOW it's ok to log to file */
		GlbLogTarget = LogFile;
	}
}

/* Internal Log Print */
void LogInternalPrint(int LogType, const char *Header, const char *Message)
{
	/* Acquire Lock */
	SpinlockAcquire(&GlbLogLock);

	/* Log it into memory - if we have room */
	if (GlbLogIndex + strlen(Message) < GlbLogSize)
	{
		/* Write header */
		GlbLog[GlbLogIndex] = (char)LogType;
		GlbLog[GlbLogIndex + 1] = (char)strlen(Message);

		/* Increase */
		GlbLogIndex += 2;

		if (LogType != LOG_TYPE_RAW)
		{
			/* Add Header */
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
				GlbBootVideo.FgColor = LOG_COLOR_INFORMATION;
			else if (LogType == LOG_TYPE_DEBUG)
				GlbBootVideo.FgColor = LOG_COLOR_DEBUG;
			else if (LogType == LOG_TYPE_FATAL)
				GlbBootVideo.FgColor = LOG_COLOR_ERROR;

			/* Print */
			printf("[%s] ", Header);
		}

		/* Sanity */
		if (LogType != LOG_TYPE_FATAL)
			GlbBootVideo.FgColor = LOG_COLOR_DEFAULT;

		/* Print */
		if (LogType == LOG_TYPE_RAW)
			printf("%s", Message);
		else
			printf("%s\n", Message);

		/* Restore */
		GlbBootVideo.FgColor = LOG_COLOR_DEFAULT;
	}
	else if (GlbLogTarget == LogFile) {

	}

	/* Release Lock */
	SpinlockRelease(&GlbLogLock);
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