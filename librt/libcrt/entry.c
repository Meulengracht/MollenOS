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
 * MollenOS C Library - Entry Points
 */

/* Includes 
 * - Library */
#include "../libc/threads/tls.h"
#include <stddef.h>
#include <stdlib.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
extern int main(int argc, char **argv, char **envp);

/* CRT Initialization sequence
 * for a shared C/C++ environment call this in all entry points */
extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               StartupInfoEnabled,
    _Out_ int*              ArgumentCount);

/* __CrtConsoleEntry
 * Console crt initialization routine. This spawns a new console
 * if no inheritance is given. */
void
__CrtConsoleEntry(void)
{
	// Variables
	thread_storage_t        Tls;
	char **Arguments        = NULL;
	int ArgumentCount       = 0;
	int ExitCode            = 0;

	// Initialize run-time
	Arguments = __CrtInitialize(&Tls, 1, &ArgumentCount);

    // Call user-process entry routine
	ExitCode = main(ArgumentCount, Arguments, NULL);

	// Exit cleanly, calling atexit() functions
	free(Arguments);
	exit(ExitCode);
}
