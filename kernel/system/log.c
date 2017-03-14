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
#include <os/driver/file.h>
#include <system/video.h>
#include <criticalsection.h>
#include <heap.h>
#include <log.h>

/* CLib */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Globals */
UUId_t GlbLogFileHandle = UUID_INVALID;
BufferObject_t *GlbLogBuffer = NULL;
char GlbLogStatic[LOG_INITIAL_SIZE];
LogTarget_t GlbLogTarget = LogMemory;
LogLevel_t GlbLogLevel = LogLevel1;
size_t GlbLogSize = 0;
CriticalSection_t GlbLogLock;
char *GlbLog = NULL;
int GlbLogIndex = 0;

/* Instantiates the Log
 * with default params */
void LogInit(void)
{
	/* Save */
	GlbLogTarget = LogMemory;
	GlbLogLevel = LogLevel1;

	/* Set log ptr to initial */
	GlbLogFileHandle = UUID_INVALID;
	GlbLogBuffer = NULL;
	GlbLog = &GlbLogStatic[0];
	GlbLogSize = LOG_INITIAL_SIZE;

	/* Clear out log */
	memset(GlbLog, 0, GlbLogSize);
	GlbLogIndex = 0;

	/* Initialize Lock */
	CriticalSectionConstruct(&GlbLogLock, CRITICALSECTION_PLAIN);
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
	LogFlush(Output);
}

/* Flushes the log */
void LogFlush(LogTarget_t Output)
{
	/* Valid flush targets are:
	 * Console
	 * File */
	char TempBuffer[256];

	/* If we are flushing to anything 
	 * other than a file, and the logfile is 
	 * opened, we close it */
	if (GlbLogFileHandle != UUID_INVALID
		&& Output != LogFile)  {
		CloseFile(GlbLogFileHandle);
		GlbLogFileHandle = UUID_INVALID;
	}

	/* Flush to console? */
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
				VideoGetTerminal()->FgColor = LOG_COLOR_DEFAULT;
				printf("%s", (const char*)&TempBuffer[0]);

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
					VideoGetTerminal()->FgColor = LOG_COLOR_INFORMATION;
				else if (Type == LOG_TYPE_DEBUG)
					VideoGetTerminal()->FgColor = LOG_COLOR_DEBUG;
				else if (Type == LOG_TYPE_FATAL)
					VideoGetTerminal()->FgColor = LOG_COLOR_ERROR;

				/* Print header */
				printf("[%s] ", (const char*)&TempBuffer[0]);

				/* Clear */
				memset(TempBuffer, 0, HeaderLen + 1);

				/* Increament */
				Index += 2 + HeaderLen + 1;

				/* Copy data */
				memcpy(TempBuffer, &GlbLog[Index], (size_t)Length);

				/* Sanity */
				if (Type != LOG_TYPE_FATAL)
					VideoGetTerminal()->FgColor = LOG_COLOR_DEFAULT;

				/* Finally, flush */
				printf("%s", (const char*)&TempBuffer[0]);

				/* Restore */
				VideoGetTerminal()->FgColor = LOG_COLOR_DEFAULT;

				/* Increase again */
				Index += Length;
			}
		}
	}
	else if (Output == LogFile)
	{
		/* Temporary set to console */
		GlbLogTarget = LogConsole;

		/* Open log file 
		 * But only if handle doesn't exist */
		int Index = 0;
		if (GlbLogFileHandle == UUID_INVALID)  {
			FileSystemCode_t Code =
				OpenFile("%sys%:/system/boot.txt",
					__FILE_CREATE | __FILE_TRUNCATE,
					__FILE_READ_ACCESS | __FILE_WRITE_ACCESS,
					&GlbLogFileHandle);

			if (Code != FsOk) {
				LogFatal("SYST", "Failed to open/create system logfile: %u", Code);
				return;
			}

			if (GlbLogBuffer == NULL) {
				GlbLogBuffer = CreateBuffer(BUFSIZ);
			}
		}

		/* Iterate */
		while (Index < GlbLogIndex)
		{
			/* Get header information */
			size_t BytesCopied = 0;
			char Type = GlbLog[Index];
			char Length = GlbLog[Index + 1];

			/* Zero buffer */
			memset(TempBuffer, 0, 256);

			/* What kind of line is this? */
			if (Type == LOG_TYPE_RAW) {
				WriteBuffer(GlbLogBuffer,
					(__CONST void*)&GlbLog[Index + 2],
					(size_t)Length, &BytesCopied);
				
				/* Flush ? */
				if (BytesCopied != (size_t)Length) {
					WriteFile(GlbLogFileHandle, GlbLogBuffer, NULL);
					WriteBuffer(GlbLogBuffer,
						(__CONST void*)&GlbLog[Index + 2 + BytesCopied],
						((size_t)Length) - BytesCopied, &BytesCopied);
				}

				/* Increase */
				Index += 2 + Length;
			}
			else {
				char HeaderBuffer[16];
				char *StartPtr = &GlbLog[Index + 2];
				char *StartMsgPtr = strchr(StartPtr, ' ');
				int HeaderLen = (int)StartMsgPtr - (int)StartPtr;

				/* Build header in HeaderBuffer */
				memset(HeaderBuffer, 0, 16);
				memcpy(TempBuffer, StartPtr, HeaderLen);
				sprintf(HeaderBuffer, "[%s] ", TempBuffer);

				/* Clear */
				memset(TempBuffer, 0, HeaderLen + 1);
				Index += 2 + HeaderLen + 1;
				memcpy(TempBuffer, &GlbLog[Index], (size_t)Length);

				/* Write */
				WriteBuffer(GlbLogBuffer,
					(__CONST void*)&HeaderBuffer[0],
					strlen(&HeaderBuffer[0]), &BytesCopied);

				/* Flush ? */
				if (BytesCopied != strlen(&HeaderBuffer[0])) {
					WriteFile(GlbLogFileHandle, GlbLogBuffer, NULL);
					WriteBuffer(GlbLogBuffer,
						(__CONST void*)&HeaderBuffer[BytesCopied],
						strlen(&HeaderBuffer[0]) - BytesCopied, &BytesCopied);
				}

				/* Write again */
				WriteBuffer(GlbLogBuffer,
					(__CONST void*)&TempBuffer[0],
					(size_t)Length, &BytesCopied);

				/* Flush ? */
				if (BytesCopied != (size_t)Length) {
					WriteFile(GlbLogFileHandle, GlbLogBuffer, NULL);
					WriteBuffer(GlbLogBuffer,
						(__CONST void*)&TempBuffer[BytesCopied],
						((size_t)Length) - BytesCopied, &BytesCopied);
				}

				/* Increase again */
				Index += Length;
			}
		}

		/* Done, flush */
		FlushFile(GlbLogFileHandle);
		GlbLogTarget = LogFile;
	}
}

/* Internal Log Print */
void LogInternalPrint(int LogType, const char *Header, const char *Message)
{
	/* Temporary format buffer 
	 * used by fileprint */
	char TempBuffer[256];
	int HeaderLen = strlen(Header);
	int MessageLen = strlen(Message);

	/* Acquire Lock */
	CriticalSectionEnter(&GlbLogLock);

	/* Log it into memory - if we have room */
	if (GlbLogIndex + MessageLen < (int)GlbLogSize)
	{
		/* Write header */
		GlbLog[GlbLogIndex] = (char)LogType;
		GlbLog[GlbLogIndex + 1] = (char)MessageLen;

		/* Increase */
		GlbLogIndex += 2;

		if (LogType != LOG_TYPE_RAW)
		{
			/* Add Header */
			memcpy(&GlbLog[GlbLogIndex], Header, HeaderLen);
			GlbLogIndex += HeaderLen;

			/* Add a space */
			GlbLog[GlbLogIndex] = ' ';
			GlbLogIndex++;
		}

		/* Add it */
		memcpy(&GlbLog[GlbLogIndex], Message, MessageLen);
		GlbLogIndex += MessageLen;

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
				VideoGetTerminal()->FgColor = LOG_COLOR_INFORMATION;
			else if (LogType == LOG_TYPE_DEBUG)
				VideoGetTerminal()->FgColor = LOG_COLOR_DEBUG;
			else if (LogType == LOG_TYPE_FATAL)
				VideoGetTerminal()->FgColor = LOG_COLOR_ERROR;

			/* Print */
			printf("[%s] ", Header);
		}

		/* Sanity */
		if (LogType != LOG_TYPE_FATAL)
			VideoGetTerminal()->FgColor = LOG_COLOR_DEFAULT;

		/* Print */
		if (LogType == LOG_TYPE_RAW)
			printf("%s", Message);
		else
			printf("%s\n", Message);

		/* Restore */
		VideoGetTerminal()->FgColor = LOG_COLOR_DEFAULT;
	}
	else if (GlbLogTarget == LogFile) {
		size_t BytesCopied = 0;
		if (GlbLogFileHandle == UUID_INVALID) {
			return;
		}

		/* Zero the buffer */
		memset(TempBuffer, 0, sizeof(TempBuffer));

		/* format it */
		if (LogType == LOG_TYPE_RAW) {
			memcpy(&TempBuffer[0], Message, MessageLen);
		}
		else {
			sprintf(&TempBuffer[0], "[%s] %s\n", Header, Message);
		}

		/* Write again */
		WriteBuffer(GlbLogBuffer,
			(__CONST void*)&TempBuffer[0],
			strlen((const char*)&TempBuffer[0]), &BytesCopied);

		/* Flush ? */
		if (BytesCopied != strlen((const char*)&TempBuffer[0])) {
			WriteFile(GlbLogFileHandle, GlbLogBuffer, NULL);
			WriteBuffer(GlbLogBuffer,
				(__CONST void*)&TempBuffer[BytesCopied],
				strlen((const char*)&TempBuffer[0]) - BytesCopied, &BytesCopied);
		}
	}

	/* Release Lock */
	CriticalSectionLeave(&GlbLogLock);
}

/* Raw Log */
void Log(const char *Message, ...)
{
	/* Output Buffer */
	char oBuffer[256];
	va_list ArgList;

	/* Sanitize arguments */
	if (Message == NULL) {
		return;
	}

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

	/* Sanitize arguments */
	if (Message == NULL) {
		return;
	}

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

	/* Sanitize arguments */
	if (System == NULL
		|| Message == NULL) {
		return;
	}

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

	/* Sanitize arguments */
	if (System == NULL
		|| Message == NULL) {
		return;
	}

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

	/* Sanitize arguments */
	if (System == NULL
		|| Message == NULL) {
		return;
	}

	/* Memset buffer */
	memset(&oBuffer[0], 0, 256);

	/* Format string */
	va_start(ArgList, Message);
	vsprintf(oBuffer, Message, ArgList);
	va_end(ArgList);

	/* Print */
	LogInternalPrint(LOG_TYPE_FATAL, System, oBuffer);
}
