/*
 * MollenOS - Philip Meulengracht, Copyright 2011-2016
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
 * MollenOS CLib - _Exit Function
 * This is like exit, except it does not call any CRT related cleanup
 */

/* Includes */
#include <os/syscall.h>
#include <stdlib.h>

/* Terminate normally, no cleanup. No calls to anything. 
 * And it never returns this function */
void _Exit(int Status)
{
	// Call for terminate and then yield.
	Syscall1(SYSCALL_TERMINATE, SYSCALL_PARAM(Status));
	Syscall0(SYSCALL_THREADYIELD);
	for (;;);
}