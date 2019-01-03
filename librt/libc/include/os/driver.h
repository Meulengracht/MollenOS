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
 * MollenOS Driver Inteface
 * - MollenOS SDK 
 */

#ifndef __DRIVER_SDK_H__
#define __DRIVER_SDK_H__

#include <os/contracts/base.h>
#include <os/interrupt.h>
#include <os/ipc/ipc.h>
#include <os/osdefs.h>
#include <os/device.h>
#include <os/acpi.h>
#include <os/io.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DRIVER_REGISTERINSTANCE		IPC_DECL_FUNCTION(0)
#define __DRIVER_UNREGISTERINSTANCE		IPC_DECL_FUNCTION(1)
#define __DRIVER_INTERRUPT				IPC_DECL_FUNCTION(2)
#define __DRIVER_QUERY					IPC_DECL_FUNCTION(3)
#define __DRIVER_TIMEOUT				IPC_DECL_FUNCTION(4)
#define __DRIVER_UNLOAD					IPC_DECL_FUNCTION(5)

#ifdef __DRIVER_IMPL

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
__EXTERN OsStatus_t
OnLoad(void);

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
__EXTERN OsStatus_t
OnUnload(void);

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
__EXTERN OsStatus_t
OnRegister(
	_In_ MCoreDevice_t*				Device);

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
__EXTERN OsStatus_t
OnUnregister(
	_In_ MCoreDevice_t*				Device);

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
__EXTERN InterruptStatus_t
OnInterrupt(
    _In_Opt_ void*					InterruptData,
    _In_Opt_ size_t 				Arg0,
    _In_Opt_ size_t 				Arg1,
    _In_Opt_ size_t 				Arg2);

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed
 * on to the below handler */
__EXTERN OsStatus_t
OnTimeout(
	_In_ UUId_t 					Timer,
	_In_ void*						Data);

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
__EXTERN OsStatus_t
OnQuery(
	_In_     MContractType_t        QueryType, 
	_In_     int                    QueryFunction, 
	_In_Opt_ MRemoteCallArgument_t* Arg0,
	_In_Opt_ MRemoteCallArgument_t* Arg1,
	_In_Opt_ MRemoteCallArgument_t* Arg2,
    _In_     MRemoteCallAddress_t*  Address);

#endif

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
