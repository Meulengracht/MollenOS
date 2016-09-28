/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Server System
* We have two kinds of servers, internal servers and
* external servers.
*
* Internal Servers:
* - PE Dll extensions
* - Run in kernel space
* - MCore Trusted Code
* - Has own event threads
*
*
* External Servers:
* - PE Dll drivers
* - Run in user space
* - Untrusted
* - Has their own event threads
*
*/

#ifndef _MCORE_SERVER_H_
#define _MCORE_SERVER_H_

/* Includes */
#include <Arch.h>
#include <os/osdefs.h>

/* This definition controls how many arguments should 
 * be allowed by a cross-over package, default: 5 */
#define CROSSOVER_MAX_ARGUMENTS				5

/* Cross-over package, this allows for a server
 * to communicate with another server with arguments */
typedef struct _MCoreCrossoverPackage
{
	/* Contains a reference to an 
	 * address space that we need to enter */
	AddressSpace_t *AddressSpace;

	/* Contains a function reference, this
	 * takes a package of arguments descriptors */
	Addr_t *Function;

	/* The argument descriptors, they descripe 
	 * the value, length and whether they should be
	 * cross-mapped */
	Addr_t Argument[CROSSOVER_MAX_ARGUMENTS];
	size_t ArgumentLength[CROSSOVER_MAX_ARGUMENTS];
	int ArgumentMap[CROSSOVER_MAX_ARGUMENTS];

} MCoreCrossoverPackage_t;


/* Crossover functions, this is for the system calls 
 * so processes can request crossover */
_CRT_EXTERN int ServerCrossEnter(MCoreCrossoverPackage_t *Package);

#endif //!_MCORE_SERVER_H_
