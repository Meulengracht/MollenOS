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

#define STR(str)               str
#define WARNING(...)           SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__)
#define WARNING_IF(cond, ...)  { if ((cond)) { SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__); } }
#define WARNING_ONCE(id, ...)  { \
                                    static _Atomic(int) __gi_ ##id ## _trigger = ATOMIC_VAR_INIT(0); \
                                    if (!atomic_exchange(&__gi_ ##id ## _trigger, 1)) { \
                                        SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__); \
                                    } \
                               }
#define ERROR(...)	           SystemDebug(SYSTEM_DEBUG_ERROR, __VA_ARGS__)
#define TODO(msg)              SystemDebug(SYSTEM_DEBUG_WARNING, "TODO: %s, line %d, %s", __FILE__, __LINE__, msg)

/**
 * Global <toggable> definitions
 * These can be turned on per-source file by pre-defining the __TRACE before inclusion
 */
#ifdef __TRACE
#include <time.h>
#define __TS_DIFF_MS(start, end) (((end.tv_sec - start.tv_sec) * MSEC_PER_SEC) + ((end.tv_nsec - start.tv_nsec) / NSEC_PER_MSEC))

#define TRACE(...) SystemDebug(SYSTEM_DEBUG_TRACE, __VA_ARGS__)
#define ENTRY(...) struct timespec start, end; SystemDebug(SYSTEM_DEBUG_TRACE, __VA_ARGS__); timespec_get(&start, TIME_MONOTONIC)
#define EXIT(msg)  timespec_get(&end, TIME_MONOTONIC); SystemDebug(SYSTEM_DEBUG_TRACE, msg " completed in %llu ms", __TS_DIFF_MS(start, end))
#else
#define TRACE(...)
#define ENTRY(...)
#define EXIT(msg)
#endif

/* Threading Utility
 * Waits for a condition to set in a busy-loop using thrd_sleep */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             SystemDebug(SYSTEM_DEBUG_WARNING, message, __VA_ARGS__);\
             break;\
		}\
        thrd_sleep(&(struct timespec) { .tv_nsec = wait * NSEC_PER_MSEC }, NULL);\
	}

/* Threading Utility
 * Waits for a condition to set in a busy-loop using thrd_sleep */
#define WaitForConditionWithFault(fault, condition, runs, wait)\
	fault = 0; \
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= (runs)) {\
			 (fault) = 1; \
             break;\
		}\
        thrd_sleep(&(struct timespec) { .tv_nsec = wait * NSEC_PER_MSEC }, NULL);\
	}

_CODE_BEGIN

DDKDECL(void, MollenOSEndBoot(void));

/**
 * @brief Debug/trace printing for userspace application and drivers. Writes to the kernel log.
 *
 * @param[In] Type   Log type, see values SYSTEM_DEBUG_*
 * @param[In] Format Sprintf like format, with variadic parameters.
 */
DDKDECL(void,
SystemDebug(
	_In_ int         Type,
	_In_ const char* Format, ...));

/**
 * @brief Requests to have the boot ramdisk mapped into the current memory space and
 * returns a pointer to the ramdisk. The memory can be freed by calling MemoryFree.
 *
 * @param[Out] bufferOut       A pointer to storage of a void* pointer that will be set to the ramdisk.
 * @param[Out] bufferLengthOut Size of the ramdisk buffer
 * @return     Status of the operation.
 */
DDKDECL(oserr_t,
        DdkUtilsMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* bufferLengthOut));

_CODE_END

#endif //!_UTILS_INTERFACE_H_
