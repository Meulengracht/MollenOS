/**
 * Copyright 2022, Philip Meulengracht
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
 * Process Service Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_SERVICES_PROCESS_H__
#define __OS_SERVICES_PROCESS_H__

#include <os/osdefs.h>
#include <os/types/process.h>
#include <time.h>

_CODE_BEGIN

/**
 * @brief Resets all values of the startup information structure to default values.
 *
 * @param Configuration
 */
CRTDECL(void,
ProcessConfigurationInitialize(
	_In_ ProcessConfiguration_t* configuration));

/**
 * @brief Spawn a new process with default parameters. The process will inherit the current process's environment
 * block, but run in it's own context (no io-descriptor share and does not inherit std handles).
 *
 * @param path
 * @param arguments
 * @param handleOut
 * @return
 */
CRTDECL(oserr_t,
ProcessSpawn(
	_In_     const char* path,
	_In_Opt_ const char* arguments,
    _Out_    uuid_t*     handleOut));

/**
 * @brief Spawn a new process with a more detailed configuration. Allows for customization of io
 * descriptors, environmental block and custom limitations.
 *
 * @param path
 * @param arguments
 * @param configuration
 * @param handleOut
 * @return
 */
CRTDECL(oserr_t,
ProcessSpawnEx(
    _In_     const char*             path,
    _In_Opt_ const char*             arguments,
    _In_Opt_ const char* const*      environment,
    _In_     ProcessConfiguration_t* configuration,
    _Out_    uuid_t*                 handleOut));

/**
 * @brief Wait for a process to terminate, and retrieve the exit code of the process.
 *
 * @param handle The handle of the process to wait for.
 * @param timeout The timeout for this operation. If 0 is given the operation returns immediately.
 * @param exitCode If this function returns OS_EOK the exit code will be a valid value.
 * @return OsTimeout if the timeout was reached without the process terminating
 *         OS_EOK if the process has terminated within the given timeout or at the time at the call
 *         OsError in any other case.
 */
CRTDECL(oserr_t,
ProcessJoin(
	_In_  uuid_t handle,
    _In_  size_t timeout,
    _Out_ int*   exitCodeOut));

/**
 * @brief Dispatches a signal to the target process, the target process must be listening to asynchronous signals
 * otherwise the signal is ignored. Both SIGKILL and SIGQUIT will terminate the process in any event, if security
 * checks are passed.
 *
 * @param handle The handle of the target process
 * @param signal The signal that should be sent to the process
 * @return       The status of the operation
 */
CRTDECL(oserr_t,
ProcessSignal(
    _In_ uuid_t handle,
    _In_ int    signal));

/**
 * @brief Retrieves the current process identifier.
 *
 * @return The ID of the current process.
 */
CRTDECL(uuid_t,
ProcessGetCurrentId(void));

/**
 * @brief Signals to the process server that this process is terminating. This does not
 * exit the current process, but it should exit as quickly as possible after invoking this.
 * @param exitCode The exit code this process resulted with.
 * @return The status of the termination.
 */
CRTDECL(oserr_t,
ProcessTerminate(int exitCode));

/**
 * @brief Retrieves the current process tick base. The tick base is set upon process startup. The
 * frequency can be retrieved by CLOCKS_PER_SEC in time.h
 *
 * @param tickOut
 * @return
 */
CRTDECL(oserr_t,
ProcessGetTickBase(
    _Out_ clock_t* tickOut));

/**
 * @brief Retrieves a copy of the command line that the current process was invoked with.
 *
 * @param buffer The buffer to store the command line in.
 * @param length If buffer is NULL then length will be set the length of the command line. Otherwise
 *               this shall be the max length of the buffer provided. This parameter will also be updated
 *               to the actual length of data stored into the buffer.
 * @return OsInvalidParameters if both parameters are invalid.
 */
CRTDECL(oserr_t,
        GetProcessCommandLine(
        _In_    char*   buffer,
        _InOut_ size_t* length));

/**
 * @brief Retrieves the current process name.
 *
 * @param buffer The buffer to store the name in.
 * @param maxLength The maximum number of bytes to be stored in the provided buffer.
 * @return OsInvalidParameters if either of the inputs are nil.
 */
CRTDECL(oserr_t,
        ProcessGetCurrentName(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief Retrieves the current assembly directory of a process handle. Use UUID_INVALID for the
 * current process.
 *
 * @param handle
 * @param buffer The buffer to store the path in.
 * @param maxLength The maximum number of bytes to be stored in the provided buffer.
 * @return OsNotExists if the handle was invalid,
 *         OsInvalidParameters if either of buffer/length are invalid.
 */
CRTDECL(oserr_t,
        ProcessGetAssemblyDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief Retrieves the current working directory of a process handle. Use UUID_INVALID for the
 * current process.
 *
 * @param handle
 * @param buffer The buffer to store the path in.
 * @param maxLength The maximum number of bytes to be stored in the provided buffer.
 * @return OsNotExists if the handle was invalid,
 *         OsInvalidParameters if either of buffer/length are invalid.
 */
CRTDECL(oserr_t,
        ProcessGetWorkingDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief Sets the working directory of the current process.
 *
 * @param path
 * @return
 */
CRTDECL(oserr_t,
        ProcessSetWorkingDirectory(
    _In_ const char* path));

_CODE_END
#endif //!__OS_SERVICES_PROCESS_H__
