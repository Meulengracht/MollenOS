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
* MollenOS C Library - Entry Points
*/

/* Includes */
#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/thread.h>
#include <os/ipc/ipc.h>

/* Includes
 * - C-Library */
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>

/* Private Definitions */
#ifdef _X86_32
#define MOLLENOS_ARGUMENT_ADDR	0x1F000000
#elif defined(X86_64)
#define MOLLENOS_ARGUMENT_ADDR	0x1F000000
#endif

/* Extern */
__CRT_EXTERN int ServerMain(void *Data);
__CRT_EXTERN void __CppInit(void);
__CRT_EXTERN void __CppFinit(void);
_MOS_API void __CppInitVectoredEH(void);

/* Unescape Quotes in arguments */
void UnEscapeQuotes(char *Arg)
{
	/* Placeholder */
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
int ParseCommandLine(char *CmdLine, char **ArgBuffer)
{
	/* Variables */
	char *BufPtr;
	char *lastp = NULL;
	int ArgCount, LastArgC;

	ArgCount = LastArgC = 0;
	for (BufPtr = CmdLine; *BufPtr;) {
		/* Skip leading whitespace */
		while (isspace(*BufPtr)) {
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
			while (*BufPtr && !isspace(*BufPtr)) {
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

	/* Done! */
	return ArgCount;
}

/* CRT Initialization sequence
 * for a shared C/C++ environment
 * call this in all entry points */
void _mCrtInit(ThreadLocalStorage_t *Tls)
{
	/* Init Crt */
	__CppInit();

	/* Initialize the TLS */
	TLSInitInstance(Tls);

	/* Init TLS */
	TLSInit();

	/* Init EH */
	__CppInitVectoredEH();
}

/* Driver Entry Point
 * Use this entry point for drivers/servers/modules */
void _mDrvCrt(void)
{
	/* Variables */
	ThreadLocalStorage_t Tls;
	int RetValue = 0;

	/* Initialize environment */
	_mCrtInit(&Tls);

	/* Initialize default pipes */
	PipeOpen(PIPE_DEFAULT);

	/* Call main */
	RetValue = ServerMain(NULL);

	/* Cleanup pipes */
	PipeClose(PIPE_DEFAULT);

	/* Exit cleanly, calling atexit() functions */
	exit(RetValue);
}
