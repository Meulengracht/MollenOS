/**
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS System Interface
 */

#include <internal/_syscalls.h>
#include <os/mollenos.h>

oserr_t
SystemQuery(
	_In_ SystemDescriptor_t* descriptor)
{
	if (!descriptor) {
		return OsInvalidParameters;
	}
	return Syscall_SystemQuery(descriptor);
}

oserr_t
FlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length)
{
    return Syscall_FlushHardwareCache(Cache, Start, Length);
}
