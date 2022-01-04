/**
 * Copyright 2017, Philip Meulengracht
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
 * Utils Definitions & Structures
 * - This header describes the base utils-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _UTILS_INTERFACE_H_
#define _UTILS_INTERFACE_H_

#include <ddk/ddkdefs.h>

/* Global <always-on> definitions
 * These are enabled no matter which kind of debugging is enabled */
#define SYSTEM_DEBUG_TRACE			0x00000000
#define SYSTEM_DEBUG_WARNING		0x00000001
#define SYSTEM_DEBUG_ERROR			0x00000002

#define STR(str)                    str
#define WARNING(...)				SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__)
#define WARNING_IF(cond, ...)       { if ((cond)) { SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__); } }
#define ERROR(...)					SystemDebug(SYSTEM_DEBUG_ERROR, __VA_ARGS__)
#define TODO(str)                   SystemDebug(SYSTEM_DEBUG_WARNING, "TODO: %s, line %d, %s", __FILE__, __LINE__, str)

/**
 * Global <toggable> definitions
 * These can be turned on per-source file by pre-defining the __TRACE before inclusion
 */
#ifdef __TRACE
#define TRACE(...)					SystemDebug(SYSTEM_DEBUG_TRACE, __VA_ARGS__)
#define ENTRY(...)                  LargeUInteger_t start, end; SystemDebug(SYSTEM_DEBUG_TRACE, __VA_ARGS__); GetSystemTick(TIME_UTC, &start)
#define EXIT(str)                   GetSystemTick(TIME_UTC, &end); SystemDebug(SYSTEM_DEBUG_TRACE, str " completed in %llu ms", end.QuadPart - start.QuadPart)
#else
#define TRACE(...)
#define ENTRY(...)
#define EXIT(str)
#endif

/* Threading Utility
 * Waits for a condition to set in a busy-loop using
 * thrd_sleepex */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             SystemDebug(SYSTEM_DEBUG_WARNING, message, __VA_ARGS__);\
             break;\
		}\
        thrd_sleepex(wait);\
	}

/* Threading Utility
 * Waits for a condition to set in a busy-loop using
 * thrd_sleepex */
#define WaitForConditionWithFault(fault, condition, runs, wait)\
	fault = 0; \
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
			 fault = 1; \
             break;\
		}\
        thrd_sleepex(wait);\
	}

_CODE_BEGIN

#if defined(i386) || defined(__i386__)
#define TLS_VALUE   uint32_t
#define TLS_READ    __asm { __asm mov ebx, [Offset] __asm mov eax, gs:[ebx] __asm mov [Value], eax }
#define TLS_WRITE   __asm { __asm mov ebx, [Offset] __asm mov eax, [Value] __asm mov gs:[ebx], eax }
#elif defined(amd64) || defined(__amd64__)
#define TLS_VALUE   uint64_t
#define TLS_READ    __asm { __asm mov rbx, [Offset] __asm mov rax, gs:[rbx] __asm mov [Value], rax }
#define TLS_WRITE   __asm { __asm mov rbx, [Offset] __asm mov rax, [Value] __asm mov gs:[rbx], rax }
#else
#error "Implement rw for tls for this architecture"
#endif

/* __get_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
SERVICEAPI size_t SERVICEABI
__get_reserved(size_t Index) {
    TLS_VALUE Value = 0;
    size_t Offset   = (Index * sizeof(TLS_VALUE));
    TLS_READ;
    return (size_t)Value;
}

/* __set_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
SERVICEAPI void SERVICEABI
__set_reserved(size_t Index, TLS_VALUE Value) {
    size_t Offset = (Index * sizeof(TLS_VALUE));
    TLS_WRITE;
}

DDKDECL(void, MollenOSEndBoot(void));

/* SystemDebug 
 * Debug/trace printing for userspace application and drivers */
DDKDECL(void,
SystemDebug(
	_In_ int         Type,
	_In_ const char* Format, ...));

/**
 * @brief Requests to have the boot ramdisk mapped into the current memory space and
 * returns a pointer to the ramdisk. The memory can be freed by calling MemoryFree.
 *
 * @param bufferOut       [Out] A pointer to storage of a void* pointer that will be set to the ramdisk.
 * @param bufferLengthOut [Out] Size of the ramdisk buffer
 * @return                Status of the operation.
 */
DDKDECL(OsStatus_t,
DdkUtilsMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* bufferLengthOut));

_CODE_END

#endif //!_UTILS_INTERFACE_H_
