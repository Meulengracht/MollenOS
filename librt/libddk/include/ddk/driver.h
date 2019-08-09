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
#include <os/ipc.h>

#define __DRIVER_REGISTERINSTANCE		(int)0
#define __DRIVER_UNREGISTERINSTANCE		(int)1
#define __DRIVER_QUERY					(int)2
#define __DRIVER_UNLOAD					(int)3

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
	_In_Opt_  void*         ResultBuffer,
	_In_Opt_  size_t        ResultLength)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Contract) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __DRIVER_QUERY);
	IPC_SET_TYPED(&Request, 1, Contract->Type);
	IPC_SET_TYPED(&Request, 2, Function);
	
	if (Arg0 != NULL && Length0 != 0) {
		IpcSetUntypedArgument(&Request, 0, (void*)Arg0, Length0);
	}
	if (Arg1 != NULL && Length1 != 0) {
		IpcSetUntypedArgument(&Request, 1, (void*)Arg1, Length1);
	}
	if (Arg2 != NULL && Length2 != 0) {
		IpcSetUntypedArgument(&Request, 2, (void*)Arg2, Length2);
	}
	
	Status = IpcInvoke(Contract->DriverId, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	if (ResultBuffer && ResultLength) {
		memcpy(ResultBuffer, Result, ResultLength);
	}
	return OsSuccess;
}

#endif //!__DRIVER_SDK_H__
