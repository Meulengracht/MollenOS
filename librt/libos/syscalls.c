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
 * MollenOS MCore - Syscall Definitions & Structures
 * - This header describes the base syscall-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Extern 
 * - Access to assembler for the platform */
__EXTERN int _syscall(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);

int Syscall0(int Function)
{
	return _syscall(Function, 0, 0, 0, 0, 0);
}

int Syscall1(int Function, int Arg0)
{
	return _syscall(Function, Arg0, 0, 0, 0, 0);
}

int Syscall2(int Function, int Arg0, int Arg1)
{
	return _syscall(Function, Arg0, Arg1, 0, 0, 0);
}

int Syscall3(int Function, int Arg0, int Arg1, int Arg2)
{
	return _syscall(Function, Arg0, Arg1, Arg2, 0, 0);
}

int Syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3)
{
	return _syscall(Function, Arg0, Arg1, Arg2, Arg3, 0);
}

int Syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4)
{
	return _syscall(Function, Arg0, Arg1, Arg2, Arg3, Arg4);
}
