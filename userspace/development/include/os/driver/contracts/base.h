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

#ifndef _MCORE_CONTRACT_H_
#define _MCORE_CONTRACT_H_

/* Includes 
 * - System */
#include <os/driver/device.h>
#include <os/osdefs.h>

/* Includes
 * - Library */
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
	ContractTimer
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
typedef struct _MContract {
	UUId_t				ContractId;
	UUId_t				DriverId;
	UUId_t				DeviceId;
	MContractType_t		Type;
	int					Version;
	MContractState_t	State;
	char				Name[CONTRACT_MAX_NAME];
} MContract_t;

/* InitializeContract
 * Helper function to initialize an instance of 
 * the contract structure */
static __CRT_INLINE void InitializeContract(MContract_t *Contract, UUId_t Device, 
	int Version, MContractType_t Type, const char *ContractName)
{
	/* Clean out structure */
	memset(Contract, 0, sizeof(MContract_t));

	/* Initialize initial status */
	Contract->DriverId = UUID_INVALID;
	Contract->DeviceId = Device;
	Contract->Version = Version;
	Contract->Type = Type;

	/* Initialize name */
	memcpy(&Contract->Name[0], ContractName, 
		MIN(CONTRACT_MAX_NAME - 1, strlen(ContractName)));
}

/* RegisterContract 
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
#ifdef __DEVICEMANAGER_IMPL
__DEVAPI OsStatus_t RegisterContract(MContract_t *Contract);
#else
__DEVAPI OsStatus_t RegisterContract(MContract_t *Contract)
{
	/* Variables */
	MRemoteCall_t Request;
	OsStatus_t Result;
	UUId_t ContractId;

	/* Initialize RPC */
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION,
		PIPE_DEFAULT, __DEVICEMANAGER_REGISTERCONTRACT);
	RPCSetArgument(&Request, 0, (const void*)Contract, sizeof(MContract_t));
	RPCSetResult(&Request, &ContractId, sizeof(UUId_t));
	Result = RPCEvaluate(&Request, __DEVICEMANAGER_TARGET);

	/* Update result, return */
	Contract->ContractId = ContractId;
	return Result;
}
#endif

#endif //!_MCORE_CONTRACT_H_
