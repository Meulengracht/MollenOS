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
#include <os/osdefs.h>

/* Contract definitions, related to some limits
 * that is bound to the contract structure */
#define CONTRACT_MAX_NAME			64

/* The available types of contracts 
 * and denotes the type of device that 
 * is bound to the contract */
typedef enum _MContractType {
	ContractUnknown,
	ContractClock
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
	IpcComm_t			Driver;
	DevId_t				Device;
	MContractType_t		Type;
	size_t				Length;
	int					Version;
	MContractState_t	State;
	char				Name[CONTRACT_MAX_NAME];
} MContract_t;

#endif //!_MCORE_CONTRACT_H_
