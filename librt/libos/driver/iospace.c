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
 * MollenOS MCore - Device I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - System */
#include <os/driver/io.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <assert.h>

/* Externs, this is i/o
 * access and varies from architecture */
__EXTERN uint8_t __readbyte(uint16_t Port);
__EXTERN void __writebyte(uint16_t Port, uint8_t Value);
__EXTERN uint16_t __readword(uint16_t Port);
__EXTERN void __writeword(uint16_t Port, uint16_t Value);
__EXTERN uint32_t __readlong(uint16_t Port);
__EXTERN void __writelong(uint16_t Port, uint32_t Value);

/* Creates a new io-space and registers it with
 * the operation system, returns OsSuccess if it's 
 * a valid io-space */
OsStatus_t CreateIoSpace(DeviceIoSpace_t *IoSpace)
{
	/* Simple redirect */
	return (OsStatus_t)Syscall1(SYSCALL_IOSREGISTER, SYSCALL_PARAM(IoSpace));
}

/* Tries to claim a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t AcquireIoSpace(DeviceIoSpace_t *IoSpace)
{
	/* Simple redirect */
	return (OsStatus_t)Syscall1(SYSCALL_IOSACQUIRE, SYSCALL_PARAM(IoSpace));
}

/* Tries to release a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t ReleaseIoSpace(DeviceIoSpace_t *IoSpace)
{
	/* Simple redirect */
	return (OsStatus_t)Syscall1(SYSCALL_IOSRELEASE, SYSCALL_PARAM(IoSpace));
}

/* Destroys the io-space with the given id and removes
 * it from the io-manage in the operation system, it
 * can only be removed if its not already acquired */
OsStatus_t DestroyIoSpace(UUId_t IoSpace)
{
	/* Simple redirect */
	return (OsStatus_t)Syscall1(SYSCALL_IOSDESTROY, SYSCALL_PARAM(IoSpace));
}

/* Read data from the given io-space at <offset> with 
 * the given <length>, the offset and length must be below 
 * the size of the io-space */
size_t ReadIoSpace(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length)
{
	/* Variables */
	size_t Result = 0;

	/* Assert that its a valid offset
	 * we are trying to read */
	assert((Offset + Length) <= IoSpace->Size);

	/* Sanity */
	if (IoSpace->Type == IO_SPACE_IO) {
		uint16_t IoPort = (uint16_t)IoSpace->PhysicalBase + (uint16_t)Offset;
		switch (Length) {
			case 1:
				Result = __readbyte(IoPort);
				break;
			case 2:
				Result = __readword(IoPort);
				break;
			case 4:
				Result = __readlong(IoPort);
				break;
			default:
				break;
		}
	}
	else if (IoSpace->Type == IO_SPACE_MMIO) {
		uintptr_t MmAddr = IoSpace->VirtualBase + Offset;
		assert(IoSpace->VirtualBase != 0);
		switch (Length) {
			case 1:
				Result = *(uint8_t*)MmAddr;
				break;
			case 2:
				Result = *(uint16_t*)MmAddr;
				break;
			case 4:
				Result = *(uint32_t*)MmAddr;
				break;
#if defined(_X86_64)
			case 8:
				Result = *(uint64_t*)MmAddr;
				break;
#endif
			default:
				break;
		}
	}

	/* Done! */
	return Result;
}

/* Write data from the given io-space at <offset> with 
 * the given <length>, the offset and length must be below 
 * the size of the io-space */
void WriteIoSpace(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length)
{
	/* Assert that its a valid offset
	* we are trying to write to */
	assert((Offset + Length) <= IoSpace->Size);

	/* Sanity */
	if (IoSpace->Type == IO_SPACE_IO) {
		uint16_t IoPort = (uint16_t)IoSpace->PhysicalBase + (uint16_t)Offset;
		switch (Length) {
			case 1:
				__writebyte(IoPort, (uint8_t)(Value & 0xFF));
				break;
			case 2:
				__writeword(IoPort, (uint16_t)(Value & 0xFFFF));
				break;
			case 4:
				__writelong(IoPort, (uint32_t)(Value & 0xFFFFFFFF));
				break;
			default:
				break;
		}
	}
	else if (IoSpace->Type == IO_SPACE_MMIO) {
		uintptr_t MmAddr = IoSpace->VirtualBase + Offset;
		assert(IoSpace->VirtualBase != 0);
		switch (Length) {
			case 1:
				*(uint8_t*)MmAddr = (uint8_t)(Value & 0xFF);
				break;
			case 2:
				*(uint16_t*)MmAddr = (uint16_t)(Value & 0xFFFF);
				break;
			case 4:
				*(uint32_t*)MmAddr = (uint32_t)(Value & 0xFFFFFFFF);
				break;
#if defined(_X86_64)
			case 8:
				*(uint64_t*)MmAddr = (uint64_t)(Value & 0xFFFFFFFFFFFFFFFF);
				break;
#endif
			default:
				break;
		}
	}
}
