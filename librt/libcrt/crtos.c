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

#include <ctype.h>
#include <internal/_syscalls.h>
#include "../libc/threads/tls.h"
#include <os/process.h>
#include <stdlib.h>
#include <string.h>

extern int main(int argc, char **argv, char **envp);
extern void __cxa_module_global_init(void);
extern void __cxa_module_global_finit(void);
extern void __cxa_module_tls_thread_init(void);
extern void __cxa_module_tls_thread_finit(void);
#ifndef __clang__
CRTDECL(void, __CppInitVectoredEH(void));
#endif

CRTDECL(void,        __cxa_runinitializers(ProcessStartupInformation_t*, 
	void (*module_init)(void), void (*module_cleanup)(void), 
    void (*module_thread_init)(void), void (*module_thread_finit)(void)));
CRTDECL(void,        InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation));
CRTDECL(const char*, GetInternalCommandLine(void));

void
UnEscapeQuotes(
    _InOut_ char* string)
{
	char* lastCharacter = NULL;

	while (*string) {
		if (*string == '"' && (lastCharacter != NULL && *lastCharacter == '\\')) {
			char* currentCharacter     = string;
			char* currentLastCharacter = lastCharacter;

			while (*currentCharacter) {
				*currentLastCharacter = *currentCharacter;
				currentLastCharacter = currentCharacter;
				currentCharacter++;
			}
			*currentLastCharacter = '\0';
		}
		
		lastCharacter = string;
		string++;
	}
}

/* Parse a command line buffer into arguments 
 * If called with NULL in argv, it simply counts */
int
ParseCommandLine(
    _In_ char*  CmdLine,
    _In_ char** ArgBuffer)
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
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount)
{
    ProcessStartupInformation_t StartupInformation;
	char**                      Arguments = NULL;
    
	tls_create(Tls);
    memset(&StartupInformation, 0, sizeof(ProcessStartupInformation_t));
    InitializeProcess(IsModule, &StartupInformation);

    // If msc, initialize the vectored-eh
#ifndef __clang__
    __CppInitVectoredEH();
#endif

    // Handle process arguments
    if (ArgumentCount != NULL) {
        if (strlen(GetInternalCommandLine()) != 0) {
            *ArgumentCount  = ParseCommandLine((char*)GetInternalCommandLine(), NULL);
            Arguments       = (char**)calloc(sizeof(char*), (*ArgumentCount) + 1);
            ParseCommandLine((char*)GetInternalCommandLine(), Arguments);
        }
        else {
            *ArgumentCount = 0;
        }
    }
    __cxa_runinitializers(&StartupInformation,
    	__cxa_module_global_init, __cxa_module_global_finit, 
        __cxa_module_tls_thread_init, __cxa_module_tls_thread_finit);
    return Arguments;
}

#if 0
int main()
{
    string cmdLine;
    
    cout << "please enter command line: ";
    getline (cin, cmdLine);
    
    char*  cCmdLine      = (char*)malloc(strlen(cmdLine.c_str()) + 1);
    memcpy(cCmdLine, cmdLine.c_str(), strlen(cmdLine.c_str()));
    cCmdLine[strlen(cmdLine.c_str())] = '\0';
    
    int    ArgumentCount = ParseCommandLine(cCmdLine, NULL);
    char** Arguments     = (char**)calloc(sizeof(char*), ArgumentCount + 1);
    
    ParseCommandLine(cCmdLine, Arguments);
    
    for (int i = 0; i < ArgumentCount; i++) {
        cout << Arguments[i] << endl;
    }

    return 0;
}
#endif
