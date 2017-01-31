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
 * MollenOS MCore - Contract Definitions & Structures (Clock Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_CLOCK_INTERFACE_H_
#define _CONTRACT_CLOCK_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/contracts/base.h>
#include <os/osdefs.h>
#include <time.h>

/* ClockQuery
 * This queries the clock contract for data
 * and must be implemented by all contracts that
 * implement the clock interface */
__DEVAPI OsStatus_t ClockQuery(struct tm *time)
{
	/* Variables */
	MRemoteCall_t Request;
	MContractType_t Type = ContractClock;

	/* Initialize RPC */
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION,
		PIPE_DEFAULT, __DEVICEMANAGER_QUERYCONTRACT);
	RPCSetArgument(&Request, 0, (const void*)&Type, sizeof(MContractType_t));
	RPCSetResult(&Request, time, sizeof(struct tm));
	return RPCEvaluate(&Request, __DEVICEMANAGER_TARGET);
}

#endif //!_MCORE_CONTRACT_CLOCK_H_
