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
 * - Library */
#include <os/osdefs.h>
#include <time.h>

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

_CODE_BEGIN
/* MemoryAllocate
 * Allocates a chunk of memory, controlled by the
 * requested size of memory. The returned memory will always
 * be rounded up to nearest page-size */
CRTDECL(
OsStatus_t,
MemoryAllocate(
	_In_ size_t Length,
	_In_ Flags_t Flags,
	_Out_ void **MemoryPointer,
	_Out_Opt_ uintptr_t *PhysicalPointer));

/* MemoryFree
 * Frees previously allocated memory and releases
 * the system resources associated. */
CRTDECL(
OsStatus_t,
MemoryFree(
	_In_ void *MemoryPointer,
	_In_ size_t Length));

/* MemoryQuery
 * Queries the underlying system for memory information 
 * like memory used and the page-size */
CRTDECL(
OsStatus_t,
MemoryQuery(
	_Out_ MemoryDescriptor_t *Descriptor));

/* ScreenQueryGeometry
 * This function returns screen geomemtry
 * descriped as a rectangle structure */
CRTDECL(
OsStatus_t,
ScreenQueryGeometry(
	_Out_ Rect_t *Rectangle));

/* PathQueryWorkingDirectory
 * Queries the current working directory path for the current process (See _MAXPATH) */
CRTDECL(
OsStatus_t,
PathQueryWorkingDirectory(
	_Out_ char *Buffer,
	_In_ size_t MaxLength));

/* PathChangeWorkingDirectory
 * Performs changes to the current working directory
 * by canonicalizing the given path modifier or absolute path */
CRTDECL(
OsStatus_t,
PathChangeWorkingDirectory(
	_In_ __CONST char *Path));

/* PathQueryApplication
 * Queries the application path for the current process (See _MAXPATH) */
CRTDECL(
OsStatus_t,
PathQueryApplication(
	_Out_ char *Buffer,
	_In_ size_t MaxLength));

/* SystemTime
 * Retrieves the system time. This is only ticking
 * if a system clock has been initialized. */
CRTDECL(
OsStatus_t,
SystemTime(
	_Out_ struct tm *time));

/* SystemTick
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
CRTDECL(
OsStatus_t,
SystemTick(
	_Out_ clock_t *clock));

/* QueryPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
CRTDECL(
OsStatus_t,
QueryPerformanceFrequency(
	_Out_ LargeInteger_t *Frequency));

/* QueryPerformanceTimer 
 * Queries the created performance timer and returns the
 * information in the given structure */
CRTDECL(
OsStatus_t,
QueryPerformanceTimer(
	_Out_ LargeInteger_t *Value));

/* __get_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
#if defined(i386)
#if defined(_MSC_VER) || defined(__clang__)
SERVICEAPI
size_t
SERVICEABI
__get_reserved(size_t index) {
	size_t result = 0;
	size_t gs_offset = (index * sizeof(size_t));
	__asm {
		mov ebx, [gs_offset];
		mov eax, gs:[ebx];
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
	size_t gs_offset = (index * sizeof(size_t));
	__asm {
		mov ebx, [gs_offset];
		mov eax, [value];
		mov gs:[ebx], eax;
	}
}
#else
#error "Implement rw for tls for this compiler"
#endif
#else
#error "Implement rw for tls for this architecture"
#endif

/***********************
 * System Misc Prototypes
 * - No functions here should
 *   be called manually
 *   they are automatically used
 *   by systems
 ***********************/
CRTDECL(
OsStatus_t,
WaitForSignal(
	_In_ size_t Timeout));

CRTDECL(
OsStatus_t,
SignalProcess(
	_In_ UUId_t Target));

CRTDECL( 
void,
MollenOSEndBoot(void));

_CODE_END
#endif //!_MOLLENOS_INTERFACE_H_
