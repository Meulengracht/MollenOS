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
 * MollenOS C++ Library - Entry Points
 */

#include "../../libc/threads/tls.h"
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>

extern "C" {
    extern int    main(int argc, char **argv, char **envp);
    extern char** __CrtInitialize(thread_storage_t* Tls, int IsModule, int* ArgumentCount);
}

/* __CrtConsoleEntry
 * Console crt initialization routine. This spawns a new console
 * if no inheritance is given. */
extern "C" void
__CrtConsoleEntry(void)
{
	thread_storage_t  Tls;
	int               ArgumentCount = 0;
	char**            Arguments;
	int               ExitCode;

	Arguments = __CrtInitialize(&Tls, 0, &ArgumentCount);
	ExitCode  = main(ArgumentCount, Arguments, NULL);

	// Exit cleanly, calling atexit() functions
	free(Arguments);
	exit(ExitCode);
}
