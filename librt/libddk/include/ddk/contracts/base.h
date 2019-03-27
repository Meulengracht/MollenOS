/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Contract Definitions & Structures
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_INTERFACE_H_
#define _CONTRACT_INTERFACE_H_

#include <ddk/ddkdefs.h>
#include <ddk/device.h>
#include <string.h>

#define CONTRACT_MAX_NAME 64

typedef enum _MContractType {
    ContractUnknown,
    ContractController,
    ContractInput,
    ContractStorage
} MContractType_t;

typedef enum _MContractState {
    ContractIdle,
    ContractActive,
    ContractInactive
} MContractState_t;

PACKED_TYPESTRUCT(MContract, {
    UUId_t           ContractId;
    UUId_t           DriverId;
    UUId_t           DeviceId;
    MContractType_t  Type;
    int              Version;
    MContractState_t State;
    char             Name[CONTRACT_MAX_NAME];
});

/* InitializeContract
 * Helper function to initialize an instance of the contract structure */
SERVICEAPI void SERVICEABI
InitializeContract(
    _In_ MContract_t*       Contract, 
    _In_ UUId_t             Device, 
    _In_ int                Version, 
    _In_ MContractType_t    Type, 
    _In_ const char*        ContractName)
{
    // Reset structure to 0
    memset(Contract, 0, sizeof(MContract_t));

    // Initialize basic state
    Contract->DriverId = UUID_INVALID;
    Contract->DeviceId = Device;
    Contract->Version = Version;
    Contract->Type = Type;

    // Initialize name
    memcpy(&Contract->Name[0], ContractName, MIN(CONTRACT_MAX_NAME - 1, strlen(ContractName)));
}

/* RegisterContract 
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
DDKDECL(
OsStatus_t,
RegisterContract(
    _In_ MContract_t *Contract));

/* QueryContract 
 * Handles the generic query function, by resolving the correct driver and asking for data */
DDKDECL(
OsStatus_t,
QueryContract(
    _In_      MContractType_t   Type, 
    _In_      int               Function,
    _In_Opt_  const void*       Arg0,
    _In_Opt_  size_t            Length0,
    _In_Opt_  const void*       Arg1,
    _In_Opt_  size_t            Length1,
    _In_Opt_  const void*       Arg2,
    _In_Opt_  size_t            Length2,
    _Out_Opt_ const void*       ResultBuffer,
    _In_Opt_  size_t            ResultLength));

#endif //!_CONTRACT_INTERFACE_H_
