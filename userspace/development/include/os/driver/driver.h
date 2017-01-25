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

#ifndef _DRIVER_SDK_H_
#define _DRIVER_SDK_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/acpi.h>
#include <os/driver/interrupt.h>
#include <os/driver/io.h>
#include <os/driver/device.h>
#include <os/driver/contracts/base.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DRIVER_REGISTERINSTANCE		IPC_DECL_FUNCTION(0)
#define __DRIVER_UNREGISTERINSTANCE		IPC_DECL_FUNCTION(1)
#define __DRIVER_INTERRUPT				IPC_DECL_FUNCTION(2)
#define __DRIVER_QUERY					IPC_DECL_FUNCTION(3)
#define __DRIVER_UNLOAD					IPC_DECL_FUNCTION(4)

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
#ifdef __DRIVER_IMPL
__CRT_EXTERN OsStatus_t OnLoad(void);
#endif

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
#ifdef __DRIVER_IMPL
__CRT_EXTERN OsStatus_t OnUnload(void);
#else

#endif

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
#ifdef __DRIVER_IMPL
__CRT_EXTERN OsStatus_t OnRegister(MCoreDevice_t *Device);
#else

#endif

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
#ifdef __DRIVER_IMPL
__CRT_EXTERN OsStatus_t OnUnregister(MCoreDevice_t *Device);
#else

#endif

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
#ifdef __DRIVER_IMPL
__CRT_EXTERN OsStatus_t OnQuery(MContractType_t QueryType, UUId_t QueryTarget, int Port);
#else
static __CRT_INLINE OsStatus_t QueryDriver(MContract_t *Contract, void *Buffer, size_t Length)
{
	/* Variables */
	MRemoteCall_t Request;

	/* Initialize RPC */
	RPCInitialize(&Request, Contract->Version, PIPE_DEFAULT, __DRIVER_QUERY);
	RPCSetArgument(&Request, 0, (const void*)&Contract->Type, sizeof(MContractType_t));
	RPCSetResult(&Request, Buffer, Length);
	return RPCEvaluate(&Request, Contract->DriverId);
}
#endif

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
#ifdef __DRIVER_IMPL
__CRT_EXTERN InterruptStatus_t OnInterrupt(void *InterruptData);
#endif

#endif //!DRIVER_SDK