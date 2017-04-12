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
 * MollenOS MCore - Definitions & Structures
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _MOLLENOS_INTERFACE_H_
#define _MOLLENOS_INTERFACE_H_

/* Includes
 * System */
#include <os/osdefs.h>

/* Structures */
typedef struct _MollenOSVideoDescriptor
{
	/* Framebuffer */
	void *FrameBufferAddr;

	/* Mode Information */
	size_t BytesPerScanline;
	size_t Height;
	size_t Width;
	int Depth;

	/* Pixel Information */
	unsigned RedPosition;
	unsigned BluePosition;
	unsigned GreenPosition;
	unsigned ReservedPosition;

	unsigned RedMask;
	unsigned BlueMask;
	unsigned GreenMask;
	unsigned ReservedMask;

} OSVideoDescriptor_t;

/* Memory Allocation Definitions
 * Flags that can be used when requesting virtual memory */
#define MEMORY_COMMIT					0x00000001
#define MEMORY_CONTIGIOUS				0x00000002
#define MEMORY_LOWFIRST					0x00000004
#define MEMORY_CLEAN					0x00000008
#define MEMORY_UNCHACHEABLE				0x00000010

/* Memory Descriptor
 * Describes the current memory state and setup
 * thats available on the current machine */
PACKED_TYPESTRUCT(MemoryDescriptor, {
	size_t			PagesTotal;
	size_t			PagesUsed;
	size_t			PageSizeBytes;
});

/* Cpp Guard */
_CODE_BEGIN

/* MemoryAllocate
 * Allocates a chunk of memory, controlled by the
 * requested size of memory. The returned memory will always
 * be rounded up to nearest page-size */
MOSAPI
OsStatus_t
MOSABI
MemoryAllocate(
	_In_ size_t Length,
	_In_ Flags_t Flags,
	_Out_ void **MemoryPointer,
	_Out_Opt_ uintptr_t *PhysicalPointer);

/* MemoryFree
 * Frees previously allocated memory and releases
 * the system resources associated. */
MOSAPI
OsStatus_t
MOSABI
MemoryFree(
	_In_ void *MemoryPointer,
	_In_ size_t Length);

/* MemoryQuery
 * Queries the underlying system for memory information 
 * like memory used and the page-size */
MOSAPI
OsStatus_t
MOSABI
MemoryQuery(
	_Out_ MemoryDescriptor_t *Descriptor);

/* ScreenQueryGeometry
 * This function returns screen geomemtry
 * descriped as a rectangle structure */
MOSAPI
OsStatus_t
MOSABI
ScreenQueryGeometry(
	_Out_ Rect_t *Rectangle);

/* PathQueryWorkingDirectory
 * Queries the current working directory path
 * for the current process (See _MAXPATH) */
MOSAPI
OsStatus_t
MOSABI
PathQueryWorkingDirectory(
	_Out_ char *Buffer,
	_In_ size_t MaxLength);

/* PathChangeWorkingDirectory
 * Performs changes to the current working directory
 * by canonicalizing the given path modifier or absolute
 * path */
MOSAPI
OsStatus_t
MOSABI
PathChangeWorkingDirectory(
	_In_ __CONST char *Path);

/* PathQueryApplication
 * Queries the application path for
 * the current process (See _MAXPATH) */
MOSAPI
OsStatus_t
MOSABI
PathQueryApplication(
	_Out_ char *Buffer,
	_In_ size_t MaxLength);

/* __get_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
#ifdef _MSC_VER
SERVICEAPI
size_t
SERVICEABI
__get_reserved(size_t index) {
	size_t result = 0;
	size_t base = (0 - ((index * sizeof(size_t)) + sizeof(size_t)));
	__asm {
		mov ebx, [base];
		mov eax, ss:[ebx];
		mov [result], eax;
	}
	return result;
}

/* __set_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
SERVICEAPI
void
SERVICEABI
__set_reserved(size_t index, size_t value) {
	size_t base = (0 - ((index * sizeof(size_t)) + sizeof(size_t)));
	__asm {
		mov ebx, [base];
		mov eax, [value];
		mov ss:[ebx], eax;
	}
}
#else
#error "Implement rw for tls"
#endif

/***********************
 * System Misc Prototypes
 * - No functions here should
 *   be called manually
 *   they are automatically used
 *   by systems
 ***********************/
MOSAPI
OsStatus_t
MOSABI
WaitForSignal(
	_In_ size_t Timeout);

MOSAPI
OsStatus_t
MOSABI
SignalProcess(
	_In_ UUId_t Target);

MOSAPI 
void
MOSABI
MollenOSEndBoot(void);

_CODE_END

#endif //!_MOLLENOS_INTERFACE_H_
