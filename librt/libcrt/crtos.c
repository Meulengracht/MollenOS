/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS C Environment - Shared Routines
 */

#include <os/syscall.h>
#include <os/process.h>
#include <os/ipc/ipc.h>

#include "../libc/threads/tls.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
__EXTERN int main(int argc, char **argv, char **envp);
__EXTERN void __CrtCxxInitialize(void);
__EXTERN void __CrtCxxFinalize(void);
__EXTERN void __CrtAttachTlsBlock(void);
#ifndef __clang__
CRTDECL(void, __CppInitVectoredEH(void));
#endif

/* StdioInitialize
 * Initializes default handles and resources */
CRTDECL(void, StdioInitialize(void *InheritanceBlock, size_t InheritanceBlockLength));

/* StdSignalInitialize
 * Initializes the default signal-handler for the process. */
CRTDECL(void, StdSignalInitialize(void));

/* __cxa_runinitializers 
 * C++ Initializes library C++ runtime for all loaded modules */
CRTDECL(void, __cxa_runinitializers(
    _In_ void (*Initializer)(void), 
    _In_ void (*Finalizer)(void), 
    _In_ void (*TlsAttachFunction)(void)));

/* Globals
 * Static buffer to avoid allocations for process startup information. */
static char StartupArgumentBuffer[512];
static char StartupInheritanceBuffer[512];

/* Unescape Quotes in arguments */
void
UnEscapeQuotes(
    _InOut_ char *Arg)
{
	char *LastChar = NULL;

	while (*Arg) {
		if (*Arg == '"' && (LastChar != NULL && *LastChar == '\\')) {
			char *CurrChar = Arg;
			char *CurrLast = LastChar;

			while (*CurrChar) {
				*CurrLast = *CurrChar;
				CurrLast = CurrChar;
				CurrChar++;
			}
			*CurrLast = '\0';
		}
		LastChar = Arg;
		Arg++;
	}
}

/* Parse a command line buffer into arguments 
 * If called with NULL in argv, it simply counts */
int
ParseCommandLine(
    _In_ char *CmdLine,
    _In_ char **ArgBuffer)
{
	char *BufPtr;
	char *lastp = NULL;
	int ArgCount, LastArgC;

	ArgCount = LastArgC = 0;
	for (BufPtr = CmdLine; *BufPtr;) {
		/* Skip leading whitespace */
		while (isspace((int)(*BufPtr))) {
			++BufPtr;
		}
		/* Skip over argument */
		if (*BufPtr == '"') {
			++BufPtr;
			if (*BufPtr) {
				if (ArgBuffer) {
					ArgBuffer[ArgCount] = BufPtr;
				}
				++ArgCount;
			}
			/* Skip over word */
			lastp = BufPtr;
			while (*BufPtr && (*BufPtr != '"' || *lastp == '\\')) {
				lastp = BufPtr;
				++BufPtr;
			}
		}
		else {
			if (*BufPtr) {
				if (ArgBuffer) {
					ArgBuffer[ArgCount] = BufPtr;
				}
				++ArgCount;
			}
			/* Skip over word */
			while (*BufPtr && !isspace((int)(*BufPtr))) {
				++BufPtr;
			}
		}
		if (*BufPtr) {
			if (ArgBuffer) {
				*BufPtr = '\0';
			}
			++BufPtr;
		}

		/* Strip out \ from \" sequences */
		if (ArgBuffer && LastArgC != ArgCount) {
			UnEscapeQuotes(ArgBuffer[LastArgC]);
		}
		LastArgC = ArgCount;
	}
	if (ArgBuffer) {
		ArgBuffer[ArgCount] = NULL;
	}
	return ArgCount;
}

/* CRT Initialization sequence
 * for a shared C/C++ environment call this in all entry points */
char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               StartupInfoEnabled,
    _Out_ int*              ArgumentCount)
{
    ProcessStartupInformation_t StartupInformation;
	char**                      Arguments = NULL;
    
	tls_create(Tls);

    // Get startup information
    memset(&StartupInformation, 0, sizeof(ProcessStartupInformation_t));
    if (StartupInfoEnabled) {
        memset(&StartupArgumentBuffer[0], 0, sizeof(StartupArgumentBuffer));
        memset(&StartupInheritanceBuffer[0], 0, sizeof(StartupInheritanceBuffer));
        StartupInformation.ArgumentPointer = &StartupArgumentBuffer[0];
        StartupInformation.ArgumentLength = sizeof(StartupArgumentBuffer);
        StartupInformation.InheritanceBlockPointer = &StartupInheritanceBuffer[0];
        StartupInformation.InheritanceBlockLength = sizeof(StartupInheritanceBuffer);
        GetStartupInformation(&StartupInformation);
    }

	// Initialize STD-C
	StdioInitialize((void*)StartupInformation.InheritanceBlockPointer, StartupInformation.InheritanceBlockLength);
    StdSignalInitialize();
 
    // If msc, initialize the vectored-eh
#ifndef __clang__
    __CppInitVectoredEH();
#endif

    // Handle process arguments
    if (ArgumentCount != NULL) {
        if (StartupInformation.ArgumentLength != 0) {
            *ArgumentCount  = ParseCommandLine(&StartupArgumentBuffer[0], NULL);
            Arguments       = (char**)calloc(sizeof(char*), (*ArgumentCount) + 1);
            ParseCommandLine(&StartupArgumentBuffer[0], Arguments);
        }
        else {
            *ArgumentCount = 0;
        }
    }
    __cxa_runinitializers(__CrtCxxInitialize, __CrtCxxFinalize, __CrtAttachTlsBlock);
    return Arguments;
}
