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
 *
 * Syscall Definitions & Structures
 * - This header describes the base syscall-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <os/osdefs.h>

__EXTERN SCTYPE _syscall(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2, SCTYPE Arg3, SCTYPE Arg4);

SCTYPE syscall0(SCTYPE Function) {
	return _syscall(Function, 0, 0, 0, 0, 0);
}

SCTYPE syscall1(SCTYPE Function, SCTYPE Arg0) {
	return _syscall(Function, Arg0, 0, 0, 0, 0);
}

SCTYPE syscall2(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1) {
	return _syscall(Function, Arg0, Arg1, 0, 0, 0);
}

SCTYPE syscall3(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2) {
	return _syscall(Function, Arg0, Arg1, Arg2, 0, 0);
}

SCTYPE syscall4(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2, SCTYPE Arg3) {
	return _syscall(Function, Arg0, Arg1, Arg2, Arg3, 0);
}

SCTYPE syscall5(SCTYPE Function, SCTYPE Arg0, SCTYPE Arg1, SCTYPE Arg2, SCTYPE Arg3, SCTYPE Arg4) {
	return _syscall(Function, Arg0, Arg1, Arg2, Arg3, Arg4);
}
