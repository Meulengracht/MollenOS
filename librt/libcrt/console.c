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

#include "../libc/threads/tls.h"
#include <stddef.h>
#include <stdlib.h>

extern int    main(int argc, char **argv, char **envp);
extern char** __crt_init(thread_storage_t* threadStorage, int isModule, int* argumentCount);

void
__CrtConsoleEntry(void)
{
	thread_storage_t threadStorage;
	char**           argv;
	int              argc;
	int              exitCode;

	argv = __crt_init(&threadStorage, 0, &argc);
	exitCode = main(argc, argv, NULL);
	free(argv);
	exit(exitCode);
}
