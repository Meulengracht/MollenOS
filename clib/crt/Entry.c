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
#include <os/MollenOS.h>
#include <os/Ui.h>
#include <os/Syscall.h>
#include <os/Thread.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef LIBC_KERNEL
void __EntryLibCEmpty(void)
{
}
#else

/* Private Definitions */
#ifdef _X86_32
#define MOLLENOS_ARGUMENT_ADDR	0x60000000
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#elif defined(X86_64)
#define MOLLENOS_ARGUMENT_ADDR	0x60000000
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#endif

/* Extern */
extern int main(int argc, char** argv);
extern void __CppInit(void);
extern void __CppFinit(void);
extern void __CppInitVectoredEH(void);

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

/* Service Entry Point 
 * Use this entry point for programs
 * that don't require a window/console */
void _mSrvCrt(void)
{
	/* Variables */
	ThreadLocalStorage_t Tls;
	uint64_t *ReservedSpace;
	char **Args = NULL;
	int ArgCount = 0;
	int RetValue = 0;

	/* Initialize the 8 bytes
	* of storage */
	ReservedSpace = (uint64_t*)MOLLENOS_RESERVED_SPACE;
	*ReservedSpace = (uint64_t)(size_t)&Tls;

	/* Init Crt */
	__CppInit();
	
	/* Initialize the TLS */
	TLSInitInstance(&Tls);

	/* Init TLS */
	TLSInit();	

	/* Init EH */
	__CppInitVectoredEH();

	/* Init Cmd */
	ArgCount = ParseCommandLine((char*)MOLLENOS_ARGUMENT_ADDR, NULL);
	Args = (char**)calloc(sizeof(char*), ArgCount + 1);
	ParseCommandLine((char*)MOLLENOS_ARGUMENT_ADDR, Args);

	/* Call main */
	RetValue = main(ArgCount, Args);

	/* Cleanup */
	free(Args);

	/* Exit cleanly, calling atexit() functions */
	exit(RetValue);
}

/* Console Entry Point 
 * Use this entry point for 
 * programs that require a console */
void _mConCrt(void)
{
	/* Variables */
	ThreadLocalStorage_t Tls;
	uint64_t *ReservedSpace;
	char **Args = NULL;
	int ArgCount = 0;
	int RetValue = 0;

	/* Initialize the 8 bytes
	 * of storage */
	ReservedSpace = (uint64_t*)MOLLENOS_RESERVED_SPACE;
	*ReservedSpace = (uint64_t)(size_t)&Tls;

	/* Init Crt */
	__CppInit();

	/* Initialize the TLS */
	TLSInitInstance(&Tls);

	/* Init TLS */
	TLSInit();

	/* Init EH */
	__CppInitVectoredEH();

	/* Initialize Ui */
	UiConnect(UiConsole);

	/* Init Cmd */
	ArgCount = ParseCommandLine((char*)MOLLENOS_ARGUMENT_ADDR, NULL);
	Args = (char**)calloc(sizeof(char*), ArgCount + 1);
	ParseCommandLine((char*)MOLLENOS_ARGUMENT_ADDR, Args);

	/* Call main */
	RetValue = main(ArgCount, Args);

	/* Cleanup */
	free(Args);

	/* Destroy Ui */
	UiDisconnect();

	/* Exit cleanly, calling atexit() functions */
	exit(RetValue);
}

#endif