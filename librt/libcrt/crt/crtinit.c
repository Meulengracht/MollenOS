/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Startup routines
 */

#include <ctype.h>
#include "../libc/threads/tls.h"
#include <os/process.h>
#include <stdlib.h>
#include <string.h>

extern int  main(int argc, char **argv, char **envp);
extern void __cxa_module_global_init(void);
extern void __cxa_module_global_finit(void);
extern void __cxa_module_tls_thread_init(void);
extern void __cxa_module_tls_thread_finit(void);
extern int  __parse_cmdline(char* rawCommandLine, char** argv);
#ifndef __clang__
CRTDECL(void, __CppInitVectoredEH(void));
#endif

CRTDECL(void,        __cxa_runinitializers(ProcessStartupInformation_t*, 
	void (*module_init)(void), void (*module_cleanup)(void), 
    void (*module_thread_init)(void), void (*module_thread_finit)(void)));
CRTDECL(void,        InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation));
CRTDECL(const char*, GetInternalCommandLine(void));

char**
__crt_init(
    _In_  thread_storage_t* threadStorage,
    _In_  int               isModule,
    _Out_ int*              argumentCount)
{
    ProcessStartupInformation_t startupInformation = { 0 };
	char**                      argv = NULL;
    
	tls_create(threadStorage);
    InitializeProcess(isModule, &startupInformation);

    // Handle process arguments
    if (argumentCount != NULL) {
        int argc = 0;

        if (strlen((const char*)startupInformation.Arguments) != 0) {
            argc = __parse_cmdline(startupInformation.Arguments, NULL);
            argv = (char**)calloc(sizeof(char*), argc + 1);
            __parse_cmdline(startupInformation.Arguments, argv);
        }

        *argumentCount = argc;
    }

    __cxa_runinitializers(
            &startupInformation,
    	    __cxa_module_global_init,
    	    __cxa_module_global_finit,
            __cxa_module_tls_thread_init,
            __cxa_module_tls_thread_finit);
    return argv;
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
