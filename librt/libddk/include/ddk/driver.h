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
 * Kernel Driver Inteface
 * - Describes the base driver interface, which calls should be implemented and
 *   which calls are available for communicating with drivers
 */

#ifndef __DRIVER_SDK_H__
#define __DRIVER_SDK_H__

#include <ddk/contracts/base.h>
#include <ddk/ipc/ipc.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DRIVER_REGISTERINSTANCE		IPC_DECL_FUNCTION(0)
#define __DRIVER_UNREGISTERINSTANCE		IPC_DECL_FUNCTION(1)
#define __DRIVER_INTERRUPT				IPC_DECL_FUNCTION(2)
#define __DRIVER_QUERY					IPC_DECL_FUNCTION(3)
#define __DRIVER_UNLOAD					IPC_DECL_FUNCTION(4)

/* InterruptDriver
 * Call this to send an interrupt into user-space */
SERVICEAPI OsStatus_t SERVICEABI
InterruptDriver(
	_In_ UUId_t Driver,
    _In_ size_t Argument0,
    _In_ size_t Argument1,
    _In_ size_t Argument2,
    _In_ size_t Argument3)
{
	MRemoteCall_t Request;
	UUId_t        NoId = UUID_INVALID;

	RPCInitialize(&Request, Driver, 1, __DRIVER_INTERRUPT);
	RPCSetArgument(&Request, 0, (const void*)&NoId, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)&Argument0, sizeof(size_t));
    RPCSetArgument(&Request, 2, (const void*)&Argument1, sizeof(size_t));
    RPCSetArgument(&Request, 3, (const void*)&Argument2, sizeof(size_t));
    RPCSetArgument(&Request, 4, (const void*)&Argument3, sizeof(size_t));
	return RPCEvent(&Request);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
SERVICEAPI OsStatus_t SERVICEABI
QueryDriver(
	_In_      MContract_t*  Contract, 
	_In_      int           Function, 
	_In_Opt_  const void*   Arg0,
	_In_Opt_  size_t        Length0,
	_In_Opt_  const void*   Arg1,
	_In_Opt_  size_t        Length1,
	_In_Opt_  const void*   Arg2,
	_In_Opt_  size_t        Length2,
	_Out_Opt_ const void*   ResultBuffer,
	_In_Opt_  size_t        ResultLength)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, Contract->DriverId, Contract->Version, __DRIVER_QUERY);
	RPCSetArgument(&Request, 0, (const void*)&Contract->Type, sizeof(MContractType_t));
	RPCSetArgument(&Request, 1, (const void*)&Function, sizeof(int));
	RPCSetResult(&Request, ResultBuffer, ResultLength);
	// Setup arguments if given
	if (Arg0 != NULL && Length0 != 0) {
		RPCSetArgument(&Request, 2, Arg0, Length0);
	}
	if (Arg1 != NULL && Length1 != 0) {
		RPCSetArgument(&Request, 3, Arg1, Length1);
	}
	if (Arg2 != NULL && Length2 != 0) {
		RPCSetArgument(&Request, 4, Arg2, Length2);
	}
	return RPCExecute(&Request);
}

#endif //!__DRIVER_SDK_H__
