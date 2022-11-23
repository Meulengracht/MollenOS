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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <internal/_tls.h>
#include <os/usched/xunit.h>
#include <stddef.h>
#include <stdlib.h>

extern int    main(int argc, char **argv, char **envp);
extern void   __crt_initialize(thread_storage_t* threadStorage, int isPhoenix);
extern char** __crt_argv(int* argcOut);

static void
__ProgramMain(void* argument, void* cancellationToken)
{
    char** argv;
    char** envp;
    int    argc;
    int    exitCode;

    _CRT_UNUSED(argument);
    _CRT_UNUSED(cancellationToken);

    argv = __crt_argv(&argc);
    envp = (char**)__tls_current()->env_block;
    exitCode = main(argc, argv, envp);
    free(argv);
    exit(exitCode);
}

void
__CrtConsoleEntry(void)
{
    struct thread_storage tls;
	__crt_initialize(&tls, 0);
    usched_xunit_main_loop(__ProgramMain, NULL);
}
