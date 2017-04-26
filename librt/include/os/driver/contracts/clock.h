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

/* Clock device query functions that must be implemented
 * by the clock driver - those can then be used by this interface */
#define __CLOCK_QUERY					IPC_DECL_FUNCTION(0)

/* ClockQuery
 * This queries the clock contract for data
 * and must be implemented by all contracts that
 * implement the clock interface */
SERVICEAPI
OsStatus_t
SERVICEABI
ClockQuery(
	_Out_ struct tm *time) 
{
	return QueryContract(ContractClock, __CLOCK_QUERY,
		NULL, 0, NULL, 0, NULL, 0, time, sizeof(struct tm));
}

#endif //!_MCORE_CONTRACT_CLOCK_H_
