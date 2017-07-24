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
 * MollenOS MCore - Contract Definitions & Structures
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_INTERFACE_H_
#define _CONTRACT_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/device.h>
#include <os/osdefs.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>

/* Contract definitions, related to some limits
 * that is bound to the contract structure */
#define CONTRACT_MAX_NAME			64

/* The available types of contracts 
 * and denotes the type of device that 
 * is bound to the contract */
typedef enum _MContractType {
	ContractUnknown,
	ContractController,
	ContractClock,
	ContractTimer,
	ContractTimerPerformance,
	ContractInput,
	ContractStorage
} MContractType_t;

/* The available contract status's that
 * a contract can be in */
typedef enum _MContractState {
	ContractIdle,
	ContractActive,
	ContractInactive
} MContractState_t;

/* The base of a contract, it contains
 * information related to the driver that
 * controls a device */
PACKED_TYPESTRUCT(MContract, {
	UUId_t				ContractId;
	UUId_t				DriverId;
	UUId_t				DeviceId;
	MContractType_t		Type;
	int					Version;
	MContractState_t	State;
	char				Name[CONTRACT_MAX_NAME];
});

/* InitializeContract
 * Helper function to initialize an instance of 
 * the contract structure */
SERVICEAPI
void
SERVICEABI
InitializeContract(
	_In_ MContract_t *Contract, 
	_In_ UUId_t Device, 
	_In_ int Version, 
	_In_ MContractType_t Type, 
	_In_ __CONST char *ContractName)
{
	// Reset structure to 0
	memset(Contract, 0, sizeof(MContract_t));

	// Initialize basic state
	Contract->DriverId = UUID_INVALID;
	Contract->DeviceId = Device;
	Contract->Version = Version;
	Contract->Type = Type;

	// Initialize name
	memcpy(&Contract->Name[0], ContractName, 
		MIN(CONTRACT_MAX_NAME - 1, strlen(ContractName)));
}

/* RegisterContract 
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
#ifdef __DEVICEMANAGER_IMPL
__DEVAPI
OsStatus_t
RegisterContract(
	_In_ MContract_t *Contract,
	_Out_ UUId_t *Id);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
RegisterContract(
	_In_ MContract_t *Contract)
{
	// Variables
	MRemoteCall_t Request;
	OsStatus_t Result = OsSuccess;
	UUId_t ContractId = UUID_INVALID;

	// Initialize static RPC variables like
	// type of RPC, pipe and version
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __DEVICEMANAGER_REGISTERCONTRACT);
	RPCSetArgument(&Request, 0, (const void*)Contract, sizeof(MContract_t));
	RPCSetResult(&Request, &ContractId, sizeof(UUId_t));
	Result = RPCExecute(&Request, __DEVICEMANAGER_TARGET);

	// Update the contract-id
	Contract->ContractId = ContractId;
	return Result;
}
#endif

/* QueryContract 
 * Handles the generic query function, by resolving
 * the correct driver and asking for data */
#ifdef __DEVICEMANAGER_IMPL
__EXTERN
OsStatus_t 
QueryContract(
	_In_ MContractType_t Type, 
	_In_ int Function,
	_In_Opt_ __CONST void *Arg0,
	_In_Opt_ size_t Length0,
	_In_Opt_ __CONST void *Arg1,
	_In_Opt_ size_t Length1,
	_In_Opt_ __CONST void *Arg2,
	_In_Opt_ size_t Length2,
	_Out_Opt_ __CONST void *ResultBuffer,
	_In_Opt_ size_t ResultLength);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
QueryContract(
	_In_ MContractType_t Type, 
	_In_ int Function,
	_In_Opt_ __CONST void *Arg0,
	_In_Opt_ size_t Length0,
	_In_Opt_ __CONST void *Arg1,
	_In_Opt_ size_t Length1,
	_In_Opt_ __CONST void *Arg2,
	_In_Opt_ size_t Length2,
	_Out_Opt_ __CONST void *ResultBuffer,
	_In_Opt_ size_t ResultLength)
{
	// Variables
	MRemoteCall_t Request;
	OsStatus_t Result = OsSuccess;

	// Initialize static RPC variables like
	// type of RPC, pipe and version
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __DEVICEMANAGER_QUERYCONTRACT);
	RPCSetArgument(&Request, 0, (__CONST void*)&Type, sizeof(MContractType_t));
	RPCSetArgument(&Request, 1, (__CONST void*)&Function, sizeof(int));

	// Handle arguments
	if (Arg0 != NULL && Length0 != 0) {
		RPCSetArgument(&Request, 2, Arg0, Length0);
	}
	if (Arg1 != NULL && Length1 != 0) {
		RPCSetArgument(&Request, 3, Arg1, Length1);
	}
	if (Arg2 != NULL && Length2 != 0) {
		RPCSetArgument(&Request, 4, Arg2, Length2);
	}

	// Handle result - if none is given we must always
	// get a osstatus - we also execute the rpc here
	if (ResultBuffer != NULL && ResultLength != 0) {
		RPCSetResult(&Request, ResultBuffer, ResultLength);
		return RPCExecute(&Request, __DEVICEMANAGER_TARGET);
	}
	else {
		RPCSetResult(&Request, &Result, sizeof(OsStatus_t));
		if (RPCExecute(&Request, __DEVICEMANAGER_TARGET) != OsSuccess) {
			return OsError;
		}
		else {
			return Result;
		}
	}
}
#endif

#endif //!_CONTRACT_INTERFACE_H_
